#include "net/display_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "cJSON.h"
#include "epd/panel.h"
#include "util/debug_log.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "mbedtls/sha256.h"

#define MANIFEST_BODY_MAX 8192
#define HTTP_TIMEOUT_MS   20000
#define RETRY_BASE_SECONDS 15U
#define RETRY_MAX_SECONDS  960U
/* Mono EPD ghosting control: after this many partials, force one full refresh. */
#define PARTIAL_BEFORE_FULL 3U

static const char *TAG = "display_api";
static uint32_t s_failures;
static bool s_force_full_manifest = true;
static uint8_t s_partial_since_full;

typedef struct {
    char *body;
    size_t capacity;
    size_t length;
    char content_sha256[65];
    bool overflow;
} http_context_t;

/* One partial region parsed from the manifest; `data` is filled by
 * download_region() with the [old][new] plane blob. */
typedef struct {
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
    uint16_t stride;
    size_t plane_byte_size;   /* stride * height, one plane */
    size_t byte_size;         /* 2 * plane_byte_size ([old][new]) */
    char sha256[65];
    char url[512];
    uint8_t *data;
} region_manifest_t;

typedef struct {
    char snapshot_id[DEVICE_SNAPSHOT_ID_MAX + 1];
    char base_snapshot_id[DEVICE_SNAPSHOT_ID_MAX + 1];
    char update_type[8];      /* "full" / "partial" */
    char frame_url[512];
    char sha256[65];
    uint32_t refresh_seconds;
    size_t byte_size;
    region_manifest_t *regions;
    size_t region_count;
} manifest_t;

static esp_err_t http_event(esp_http_client_event_t *event)
{
    http_context_t *context = event->user_data;
    if (context == NULL) {
        return ESP_OK;
    }
    if (event->event_id == HTTP_EVENT_ON_DATA && context->body != NULL) {
        if (context->length + event->data_len >= context->capacity) {
            context->overflow = true;
            return ESP_FAIL;
        }
        memcpy(context->body + context->length, event->data, event->data_len);
        context->length += event->data_len;
        context->body[context->length] = '\0';
    } else if (event->event_id == HTTP_EVENT_ON_HEADER &&
               event->header_key != NULL && event->header_value != NULL &&
               strcasecmp(event->header_key, "X-Content-SHA256") == 0) {
        snprintf(context->content_sha256, sizeof(context->content_sha256), "%s",
                 event->header_value);
    }
    return ESP_OK;
}

static esp_http_client_handle_t create_client(const char *url, http_context_t *context)
{
    esp_http_client_config_t options = {
        .url = url,
        .event_handler = http_event,
        .user_data = context,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .buffer_size = 2048,
        .buffer_size_tx = 1024,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .keep_alive_enable = true,
    };
    return esp_http_client_init(&options);
}

static bool json_string(cJSON *root, const char *key, char *dest, size_t size)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsString(item) || item->valuestring == NULL ||
        item->valuestring[0] == '\0' || strlen(item->valuestring) >= size) {
        return false;
    }
    snprintf(dest, size, "%s", item->valuestring);
    return true;
}

static bool num_u32(cJSON *node, uint32_t *out)
{
    if (!cJSON_IsNumber(node) || node->valuedouble < 0 ||
        node->valuedouble > 4294967295.0 ||
        node->valuedouble != (double)(uint32_t)node->valuedouble) {
        return false;
    }
    *out = (uint32_t)node->valuedouble;
    return true;
}

static void free_regions(manifest_t *manifest)
{
    if (manifest->regions != NULL) {
        for (size_t i = 0; i < manifest->region_count; i++) {
            free(manifest->regions[i].data);
        }
        free(manifest->regions);
        manifest->regions = NULL;
    }
    manifest->region_count = 0;
}

static bool parse_regions(cJSON *root, manifest_t *manifest)
{
    cJSON *regions = cJSON_GetObjectItemCaseSensitive(root, "regions");
    if (!cJSON_IsArray(regions)) {
        return false;
    }
    int n = cJSON_GetArraySize(regions);
    if (n <= 0) {
        return false;
    }

    manifest->regions = calloc((size_t)n, sizeof(region_manifest_t));
    if (manifest->regions == NULL) {
        return false;
    }

    const epd_panel_ops_t *panel = epd_panel_current();
    for (int i = 0; i < n; i++) {
        cJSON *rg = cJSON_GetArrayItem(regions, i);
        region_manifest_t *out = &manifest->regions[i];
        cJSON *jx = cJSON_GetObjectItemCaseSensitive(rg, "x");
        cJSON *jy = cJSON_GetObjectItemCaseSensitive(rg, "y");
        cJSON *jw = cJSON_GetObjectItemCaseSensitive(rg, "width");
        cJSON *jh = cJSON_GetObjectItemCaseSensitive(rg, "height");
        cJSON *jstride = cJSON_GetObjectItemCaseSensitive(rg, "stride");
        cJSON *jpbs = cJSON_GetObjectItemCaseSensitive(rg, "plane_byte_size");
        cJSON *jbs = cJSON_GetObjectItemCaseSensitive(rg, "byte_size");

        uint32_t ux, uy, uw, uh, ustride, upbs, ubs;
        if (!num_u32(jx, &ux) || !num_u32(jy, &uy) ||
            !num_u32(jw, &uw) || !num_u32(jh, &uh) ||
            !num_u32(jstride, &ustride) || !num_u32(jpbs, &upbs) || !num_u32(jbs, &ubs) ||
            !json_string(rg, "sha256", out->sha256, sizeof(out->sha256)) ||
            strlen(out->sha256) != 64 ||
            !json_string(rg, "url", out->url, sizeof(out->url))) {
            return false;
        }
        if (uw == 0 || uh == 0 || (uw % 8) != 0 ||
            ustride != (uw + 7) / 8 ||
            upbs != ustride * uh ||
            ubs != 2 * upbs ||
            ux + uw > panel->width ||
            uy + uh > panel->height) {
            EPD_LOGE(TAG, "region %d geometry invalid", i);
            return false;
        }
        out->x = (uint16_t)ux;
        out->y = (uint16_t)uy;
        out->width = (uint16_t)uw;
        out->height = (uint16_t)uh;
        out->stride = (uint16_t)ustride;
        out->plane_byte_size = upbs;
        out->byte_size = ubs;
    }

    manifest->region_count = (size_t)n;
    return true;
}

static bool parse_manifest(const char *body, manifest_t *manifest)
{
    cJSON *root = cJSON_Parse(body);
    if (root == NULL) {
        return false;
    }

    cJSON *width = cJSON_GetObjectItemCaseSensitive(root, "width");
    cJSON *height = cJSON_GetObjectItemCaseSensitive(root, "height");
    cJSON *byte_size = cJSON_GetObjectItemCaseSensitive(root, "byte_size");
    cJSON *refresh = cJSON_GetObjectItemCaseSensitive(root, "refresh_after_seconds");
    cJSON *color_mode = cJSON_GetObjectItemCaseSensitive(root, "color_mode");
    cJSON *layout = cJSON_GetObjectItemCaseSensitive(root, "panel_layout");
    cJSON *encoding = cJSON_GetObjectItemCaseSensitive(root, "encoding");
    bool refresh_valid = cJSON_IsNumber(refresh) &&
                         refresh->valuedouble >= 1 &&
                         refresh->valuedouble <= UINT32_MAX &&
                         refresh->valuedouble ==
                             (double)(uint32_t)refresh->valuedouble;

    const epd_panel_ops_t *panel = epd_panel_current();
    /* Fields common to both full and partial updates. The top-level
     * byte_size/sha256/frame_url always describe the complete frame, so they
     * are validated here regardless of update_type. */
    bool valid = cJSON_IsNumber(width) && width->valuedouble == panel->width &&
                 cJSON_IsNumber(height) && height->valuedouble == panel->height &&
                 cJSON_IsNumber(byte_size) && byte_size->valuedouble == (double)panel->frame_bytes &&
                 refresh_valid &&
                 cJSON_IsString(color_mode) && strcmp(color_mode->valuestring, panel->color_mode) == 0 &&
                 cJSON_IsString(layout) && strcmp(layout->valuestring, panel->panel_layout) == 0 &&
                 cJSON_IsString(encoding) && strcmp(encoding->valuestring, panel->encoding) == 0 &&
                 json_string(root, "update_type", manifest->update_type, sizeof(manifest->update_type)) &&
                 json_string(root, "snapshot_id", manifest->snapshot_id,
                             sizeof(manifest->snapshot_id)) &&
                 json_string(root, "frame_url", manifest->frame_url,
                             sizeof(manifest->frame_url)) &&
                 json_string(root, "sha256", manifest->sha256, sizeof(manifest->sha256)) &&
                 strlen(manifest->sha256) == 64;

    if (valid) {
        manifest->byte_size = (size_t)byte_size->valuedouble;
        manifest->refresh_seconds = (uint32_t)refresh->valuedouble;
        /* base_snapshot_id may be absent on full updates. */
        json_string(root, "base_snapshot_id", manifest->base_snapshot_id,
                    sizeof(manifest->base_snapshot_id));

        if (strcmp(manifest->update_type, "full") == 0) {
            manifest->regions = NULL;
            manifest->region_count = 0;
        } else if (strcmp(manifest->update_type, "partial") == 0) {
            valid = (panel->display_partial != NULL) && parse_regions(root, manifest);
        } else {
            valid = false;
        }
    }

    cJSON_Delete(root);
    if (!valid) {
        free_regions(manifest);
    }
    return valid;
}

static int fetch_manifest(const device_config_t *config, manifest_t *manifest,
                          bool include_snapshot)
{
    char url[512];
    int written;
    if (include_snapshot && config->snapshot_id[0] != '\0') {
        written = snprintf(url, sizeof(url),
                           "%s/api/v1/device/screens/%s/manifest?base_snapshot_id=%s",
                           config->server_url, config->screen_id, config->snapshot_id);
    } else {
        written = snprintf(url, sizeof(url), "%s/api/v1/device/screens/%s/manifest",
                           config->server_url, config->screen_id);
    }
    if (written < 0 || (size_t)written >= sizeof(url)) {
        return -1;
    }

    char *body = calloc(1, MANIFEST_BODY_MAX);
    if (body == NULL) {
        return -1;
    }
    http_context_t context = {
        .body = body,
        .capacity = MANIFEST_BODY_MAX,
    };
    esp_http_client_handle_t client = create_client(url, &context);
    if (client == NULL) {
        free(body);
        return -1;
    }
    esp_http_client_set_header(client, "X-API-Key", config->api_key);
    esp_http_client_set_header(client, "Accept", "application/json");

    esp_err_t err = esp_http_client_perform(client);
    int status = err == ESP_OK ? esp_http_client_get_status_code(client) : -1;
    EPD_LOGD(TAG, "manifest status=%d bytes=%u", status, (unsigned)context.length);
    if (err != ESP_OK) {
        EPD_LOGW(TAG, "manifest request failed: %s", esp_err_to_name(err));
    } else if (status == 200 && (context.overflow || !parse_manifest(body, manifest))) {
        EPD_LOGE(TAG, "manifest is invalid or incompatible with this panel");
        status = -1;
    }
    esp_http_client_cleanup(client);
    free(body);
    return status;
}

static void sha256_hex(const uint8_t digest[32], char output[65])
{
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < 32; ++i) {
        output[i * 2] = hex[digest[i] >> 4];
        output[i * 2 + 1] = hex[digest[i] & 0x0f];
    }
    output[64] = '\0';
}

/* Download a binary blob (full frame or a region's [old][new] package) into
 * PSRAM, verifying announced length and SHA-256 against the manifest and the
 * X-Content-SHA256 header. Returns the buffer via *out on success. */
static bool download_verified(const device_config_t *config, const char *url,
                              size_t expected_size, const char *expected_sha256,
                              uint8_t **out)
{
    uint8_t *buf = heap_caps_malloc(expected_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buf == NULL) {
        EPD_LOGE(TAG, "unable to allocate %u-byte PSRAM buffer", (unsigned)expected_size);
        return false;
    }

    http_context_t context = {0};
    esp_http_client_handle_t client = create_client(url, &context);
    if (client == NULL) {
        free(buf);
        return false;
    }
    esp_http_client_set_header(client, "X-API-Key", config->api_key);
    esp_http_client_set_header(client, "Accept", "application/octet-stream");

    bool success = false;
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts(&sha, 0);
    if (esp_http_client_open(client, 0) != ESP_OK) {
        goto done;
    }
    int64_t announced_length = esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    if (status != 200 || (announced_length >= 0 &&
                          (size_t)announced_length != expected_size)) {
        EPD_LOGE(TAG, "download HTTP status=%d length=%lld", status, announced_length);
        goto done;
    }

    size_t total = 0;
    while (total < expected_size) {
        int count = esp_http_client_read(client, (char *)buf + total,
                                         expected_size - total);
        if (count < 0) {
            EPD_LOGE(TAG, "download read failed");
            goto done;
        }
        if (count == 0) {
            break;
        }
        mbedtls_sha256_update(&sha, buf + total, count);
        total += count;
    }
    if (total != expected_size) {
        EPD_LOGE(TAG, "download truncated: %u/%u", (unsigned)total,
                 (unsigned)expected_size);
        goto done;
    }

    uint8_t digest[32];
    char calculated[65];
    mbedtls_sha256_finish(&sha, digest);
    sha256_hex(digest, calculated);
    if (strcasecmp(calculated, expected_sha256) != 0 ||
        (context.content_sha256[0] != '\0' &&
         strcasecmp(calculated, context.content_sha256) != 0)) {
        EPD_LOGE(TAG, "download SHA-256 mismatch");
        goto done;
    }
    success = true;

done:
    mbedtls_sha256_free(&sha);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    if (!success) {
        free(buf);
        return false;
    }
    *out = buf;
    return true;
}

static uint32_t retry_interval(void)
{
    if (s_failures < 8) {
        ++s_failures;
    }
    uint32_t seconds = RETRY_BASE_SECONDS << (s_failures > 6 ? 6 : (s_failures - 1));
    if (seconds > RETRY_MAX_SECONDS) {
        seconds = RETRY_MAX_SECONDS;
    }
    return seconds;
}

static bool apply_full(const device_config_t *config, const epd_panel_ops_t *panel,
                       const manifest_t *manifest)
{
    uint8_t *frame = NULL;
    if (!download_verified(config, manifest->frame_url, manifest->byte_size,
                           manifest->sha256, &frame)) {
        return false;
    }
    EPD_LOGI(TAG, "verified snapshot %s; refreshing panel (full)", manifest->snapshot_id);
    esp_err_t err = panel->init();
    if (err == ESP_OK) {
        err = panel->display_image(frame);
    }
    free(frame);
    if (err != ESP_OK) {
        EPD_LOGE(TAG, "panel refresh failed: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

static bool apply_partial(const device_config_t *config, const epd_panel_ops_t *panel,
                          manifest_t *manifest)
{
    for (size_t i = 0; i < manifest->region_count; i++) {
        if (!download_verified(config, manifest->regions[i].url,
                               manifest->regions[i].byte_size,
                               manifest->regions[i].sha256,
                               &manifest->regions[i].data)) {
            EPD_LOGW(TAG, "partial region %u download failed; falling back to full",
                     (unsigned)i);
            return false;
        }
    }

    size_t n = manifest->region_count;
    epd_region_t *regions = calloc(n, sizeof(epd_region_t));
    if (regions == NULL) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        regions[i].x = manifest->regions[i].x;
        regions[i].y = manifest->regions[i].y;
        regions[i].width = manifest->regions[i].width;
        regions[i].height = manifest->regions[i].height;
        regions[i].stride = manifest->regions[i].stride;
        regions[i].old_data = manifest->regions[i].data;
        regions[i].new_data = manifest->regions[i].data + manifest->regions[i].plane_byte_size;
    }

    EPD_LOGI(TAG, "applying %u partial region(s) for snapshot %s",
             (unsigned)n, manifest->snapshot_id);
    esp_err_t err = panel->display_partial(regions, n);
    free(regions);
    if (err != ESP_OK) {
        EPD_LOGW(TAG, "partial refresh failed (%s); falling back to full",
                 esp_err_to_name(err));
        return false;
    }
    return true;
}

uint32_t display_client_poll(device_config_t *config, bool force_full)
{
    if (force_full) {
        s_force_full_manifest = true;
        EPD_LOGI(TAG, "full-screen refresh requested");
    }
    manifest_t manifest = {0};
    int status = fetch_manifest(config, &manifest, !s_force_full_manifest);
    if (status == 304) {
        s_failures = 0;
        EPD_LOGD(TAG, "display unchanged");
        if (config->refresh_after_seconds > 0) {
            return config->refresh_after_seconds;
        }
        EPD_LOGW(TAG, "manifest interval unavailable; requesting full manifest again");
        s_force_full_manifest = true;
        return retry_interval();
    }
    if (status != 200) {
        if (status == 401) {
            EPD_LOGE(TAG, "API Key rejected for this screen");
        } else if (status == 404) {
            EPD_LOGW(TAG, "screen or snapshot is unavailable");
        } else if (status == 429) {
            EPD_LOGW(TAG, "device API rate limit reached");
        }
        return retry_interval();
    }

    const epd_panel_ops_t *panel = epd_panel_current();
    bool applied = false;
    bool used_partial = false;
    bool want_partial = strcmp(manifest.update_type, "partial") == 0 &&
                        panel->display_partial != NULL &&
                        manifest.base_snapshot_id[0] != '\0' &&
                        strcmp(manifest.base_snapshot_id, config->snapshot_id) == 0;
    if (want_partial && s_partial_since_full >= PARTIAL_BEFORE_FULL) {
        EPD_LOGI(TAG,
                 "partial budget exhausted (%u/%u); forcing full refresh",
                 (unsigned)s_partial_since_full, (unsigned)PARTIAL_BEFORE_FULL);
        want_partial = false;
    }
    if (want_partial) {
        applied = apply_partial(config, panel, &manifest);
        used_partial = applied;
    }
    if (!applied) {
        /* Any partial skip/failure falls back to a full update using the
         * same manifest's frame_url (always present for both update types). */
        applied = apply_full(config, panel, &manifest);
    }
    free_regions(&manifest);

    if (!applied) {
        return retry_interval();
    }
    if (used_partial) {
        s_partial_since_full++;
        EPD_LOGI(TAG, "partial streak %u/%u",
                 (unsigned)s_partial_since_full, (unsigned)PARTIAL_BEFORE_FULL);
    } else {
        s_partial_since_full = 0;
    }
    if (device_config_save_manifest_state(manifest.snapshot_id,
                                          manifest.refresh_seconds) != ESP_OK) {
        EPD_LOGE(TAG, "snapshot displayed but state commit failed");
        return retry_interval();
    }
    snprintf(config->snapshot_id, sizeof(config->snapshot_id), "%s", manifest.snapshot_id);
    config->refresh_after_seconds = manifest.refresh_seconds;
    s_force_full_manifest = false;
    s_failures = 0;
    EPD_LOGI(TAG, "snapshot committed");
    return manifest.refresh_seconds;
}
