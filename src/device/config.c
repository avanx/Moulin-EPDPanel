#include "device/config.h"

#include <ctype.h>
#include <string.h>

#include "util/debug_log.h"
#include "esp_check.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "config";
static const char *NAMESPACE = "epd";

static esp_err_t get_string(nvs_handle_t handle, const char *key, char *value, size_t size)
{
    size_t required = size;
    esp_err_t err = nvs_get_str(handle, key, value, &required);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        value[0] = '\0';
        return ESP_OK;
    }
    if (err == ESP_ERR_NVS_INVALID_LENGTH) {
        value[0] = '\0';
    }
    return err;
}

static esp_err_t set_string(nvs_handle_t handle, const char *key, const char *value)
{
    return nvs_set_str(handle, key, value != NULL ? value : "");
}

static bool is_uuid(const char *value)
{
    if (value == NULL || strlen(value) != 36) {
        return false;
    }
    for (size_t i = 0; i < 36; ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (value[i] != '-') {
                return false;
            }
        } else if (!isxdigit((unsigned char)value[i])) {
            return false;
        }
    }
    return true;
}

static bool is_server_url(const char *value)
{
    if (value == NULL) {
        return false;
    }
    size_t length = strlen(value);
    if (length < 8 || length > DEVICE_SERVER_URL_MAX || value[length - 1] == '/') {
        return false;
    }
    return strncmp(value, "https://", 8) == 0 || strncmp(value, "http://", 7) == 0;
}

esp_err_t device_config_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "erase NVS");
        err = nvs_flash_init();
    }
    return err;
}

esp_err_t device_config_load(device_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(config, 0, sizeof(*config));

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    if ((err = get_string(handle, "ssid", config->wifi_ssid, sizeof(config->wifi_ssid))) == ESP_OK &&
        (err = get_string(handle, "wifi_pass", config->wifi_password, sizeof(config->wifi_password))) == ESP_OK &&
        (err = get_string(handle, "server", config->server_url, sizeof(config->server_url))) == ESP_OK &&
        (err = get_string(handle, "screen_id", config->screen_id, sizeof(config->screen_id))) == ESP_OK &&
        (err = get_string(handle, "api_key", config->api_key, sizeof(config->api_key))) == ESP_OK &&
        (err = get_string(handle, "panel", config->panel_type, sizeof(config->panel_type))) == ESP_OK &&
        (err = get_string(handle, "snapshot", config->snapshot_id, sizeof(config->snapshot_id))) == ESP_OK) {
        esp_err_t refresh_err =
            nvs_get_u32(handle, "refresh", &config->refresh_after_seconds);
        if (refresh_err != ESP_OK && refresh_err != ESP_ERR_NVS_NOT_FOUND) {
            err = refresh_err;
        }
    }

    nvs_close(handle);
    EPD_LOGD(TAG, "configuration loaded: ssid=%s server=%s panel=%s snapshot=%s",
             config->wifi_ssid, config->server_url,
             config->panel_type[0] ? config->panel_type : "(default)",
             config->snapshot_id[0] ? config->snapshot_id : "(none)");
    return err;
}

esp_err_t device_config_save(const device_config_t *config)
{
    if (!device_config_validate(config)) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(NAMESPACE, NVS_READWRITE, &handle), TAG, "open NVS");

    esp_err_t err = set_string(handle, "ssid", config->wifi_ssid);
    if (err == ESP_OK) err = set_string(handle, "wifi_pass", config->wifi_password);
    if (err == ESP_OK) err = set_string(handle, "server", config->server_url);
    if (err == ESP_OK) err = set_string(handle, "screen_id", config->screen_id);
    if (err == ESP_OK) err = set_string(handle, "api_key", config->api_key);
    if (err == ESP_OK) err = set_string(handle, "panel", config->panel_type);
    if (err == ESP_OK) err = set_string(handle, "snapshot", config->snapshot_id);
    if (err == ESP_OK) {
        err = nvs_set_u32(handle, "refresh", config->refresh_after_seconds);
    }
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

esp_err_t device_config_save_wifi(const device_config_t *config)
{
    if (!device_config_validate_wifi(config)) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(NAMESPACE, NVS_READWRITE, &handle), TAG, "open NVS");
    esp_err_t err = set_string(handle, "ssid", config->wifi_ssid);
    if (err == ESP_OK) err = set_string(handle, "wifi_pass", config->wifi_password);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

esp_err_t device_config_save_device_settings(const device_config_t *config)
{
    if (!device_config_validate_device_settings(config)) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(NAMESPACE, NVS_READWRITE, &handle), TAG, "open NVS");
    esp_err_t err = set_string(handle, "server", config->server_url);
    if (err == ESP_OK) err = set_string(handle, "screen_id", config->screen_id);
    if (err == ESP_OK) err = set_string(handle, "api_key", config->api_key);
    if (err == ESP_OK) err = set_string(handle, "panel", config->panel_type);
    if (err == ESP_OK) err = set_string(handle, "snapshot", "");
    if (err == ESP_OK) err = nvs_set_u32(handle, "refresh", 0);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

esp_err_t device_config_save_panel_type(const char *panel_type)
{
    if (panel_type == NULL || panel_type[0] == '\0' ||
        strlen(panel_type) > DEVICE_PANEL_TYPE_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(NAMESPACE, NVS_READWRITE, &handle), TAG, "open NVS");
    esp_err_t err = set_string(handle, "panel", panel_type);
    /* Panel change invalidates any cached frame baseline. */
    if (err == ESP_OK) err = set_string(handle, "snapshot", "");
    if (err == ESP_OK) err = nvs_set_u32(handle, "refresh", 0);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    if (err == ESP_OK) {
        EPD_LOGI(TAG, "panel type saved: %s", panel_type);
    }
    return err;
}

esp_err_t device_config_save_manifest_state(const char *snapshot_id,
                                            uint32_t refresh_after_seconds)
{
    if (snapshot_id == NULL || snapshot_id[0] == '\0' ||
        strlen(snapshot_id) > DEVICE_SNAPSHOT_ID_MAX || refresh_after_seconds == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(NAMESPACE, NVS_READWRITE, &handle), TAG, "open NVS");
    esp_err_t err = nvs_set_str(handle, "snapshot", snapshot_id);
    if (err == ESP_OK) err = nvs_set_u32(handle, "refresh", refresh_after_seconds);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

esp_err_t device_config_clear_snapshot(void)
{
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(NAMESPACE, NVS_READWRITE, &handle), TAG, "open NVS");
    esp_err_t err = nvs_set_str(handle, "snapshot", "");
    if (err == ESP_OK) err = nvs_set_u32(handle, "refresh", 0);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    if (err == ESP_OK) {
        EPD_LOGI(TAG, "cached snapshot cleared; next boot will request a full frame");
    }
    return err;
}

bool device_config_is_complete(const device_config_t *config)
{
    return device_config_has_wifi(config) &&
           config->server_url[0] != '\0' && config->screen_id[0] != '\0' &&
           config->api_key[0] != '\0';
}

bool device_config_has_wifi(const device_config_t *config)
{
    return config != NULL && config->wifi_ssid[0] != '\0';
}

bool device_config_validate_wifi(const device_config_t *config)
{
    return device_config_has_wifi(config) &&
           strlen(config->wifi_ssid) <= DEVICE_WIFI_SSID_MAX &&
           strlen(config->wifi_password) <= DEVICE_WIFI_PASSWORD_MAX;
}

bool device_config_validate_device_settings(const device_config_t *config)
{
    if (config == NULL || config->server_url[0] == '\0' ||
        config->screen_id[0] == '\0' || config->api_key[0] == '\0' ||
        strlen(config->api_key) > DEVICE_API_KEY_MAX ||
        strlen(config->panel_type) > DEVICE_PANEL_TYPE_MAX ||
        !is_server_url(config->server_url) || !is_uuid(config->screen_id)) {
        return false;
    }
    return true;
}

bool device_config_validate(const device_config_t *config)
{
    return device_config_validate_wifi(config) &&
           device_config_validate_device_settings(config) &&
           strlen(config->snapshot_id) <= DEVICE_SNAPSHOT_ID_MAX;
}
