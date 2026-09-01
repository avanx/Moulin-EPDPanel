#include "device/control.h"

#include "epd/panel.h"
#include "net/display_client.h"
#include "util/debug_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

typedef enum {
    CONTROL_ACTIVE,
    CONTROL_WHITENING,
    CONTROL_WHITE,
    CONTROL_WHITE_FAILED,
    CONTROL_REPAIRING,
    CONTROL_REPAIR_FAILED,
} control_state_t;

/* Hold each extreme long enough for the ink to settle before flipping. */
#define REPAIR_HOLD_MS  5000U
#define REPAIR_HOLD_SLICE_MS 100U

static const char *TAG = "device_control";
static SemaphoreHandle_t s_display_mutex;
static volatile control_state_t s_state = CONTROL_ACTIVE;
static volatile bool s_full_refresh_requested;
static volatile uint32_t s_repair_cycles_total;
static volatile uint32_t s_repair_cycles_done;
static volatile bool s_repair_stop_requested;

esp_err_t device_control_init(void)
{
    if (s_display_mutex != NULL) {
        return ESP_OK;
    }
    s_display_mutex = xSemaphoreCreateMutex();
    return s_display_mutex != NULL ? ESP_OK : ESP_ERR_NO_MEM;
}

uint32_t device_control_poll(device_config_t *config)
{
    if (s_state != CONTROL_ACTIVE) {
        return 1;
    }
    if (xSemaphoreTake(s_display_mutex, portMAX_DELAY) != pdTRUE) {
        return 1;
    }
    bool force_full = s_full_refresh_requested;
    s_full_refresh_requested = false;
    uint32_t seconds = 1;
    if (s_state == CONTROL_ACTIVE) {
        seconds = display_client_poll(config, force_full);
    }
    xSemaphoreGive(s_display_mutex);
    return seconds;
}

esp_err_t device_control_request_full_refresh(void)
{
    if (s_display_mutex == NULL || s_state != CONTROL_ACTIVE) {
        return ESP_ERR_INVALID_STATE;
    }
    s_full_refresh_requested = true;
    EPD_LOGI(TAG, "full-screen refresh queued");
    return ESP_OK;
}

bool device_control_full_refresh_requested(void)
{
    return s_full_refresh_requested;
}

static void white_task(void *argument)
{
    (void)argument;
    EPD_LOGI(TAG, "white-screen command started; automatic updates paused until reboot");
    xSemaphoreTake(s_display_mutex, portMAX_DELAY);
    /*
     * Clear the persisted baseline while holding the display lock. An
     * in-flight poll may commit a snapshot before we obtain this lock; doing
     * it here guarantees our clear is the final persisted state.
     */
    esp_err_t err = device_config_clear_snapshot();
    if (err == ESP_OK) {
        err = epd_panel_current()->init();
    }
    if (err == ESP_OK) {
        err = epd_panel_current()->fill_white();
    }
    xSemaphoreGive(s_display_mutex);

    if (err == ESP_OK) {
        s_state = CONTROL_WHITE;
        EPD_LOGI(TAG, "screen cleared to white; automatic updates remain paused");
    } else {
        s_state = CONTROL_WHITE_FAILED;
        EPD_LOGE(TAG, "white-screen command failed: %s; updates remain paused",
                 esp_err_to_name(err));
    }
    vTaskDelete(NULL);
}

esp_err_t device_control_request_white(void)
{
    if (s_display_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_state != CONTROL_ACTIVE) {
        return ESP_ERR_INVALID_STATE;
    }

    s_state = CONTROL_WHITENING;
    if (xTaskCreate(white_task, "screen_white", 4096, NULL, 6, NULL) != pdPASS) {
        s_state = CONTROL_ACTIVE;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

/* Sliced sleep so a stop request lands within one slice. */
static void repair_hold(void)
{
    for (uint32_t elapsed = 0; elapsed < REPAIR_HOLD_MS; elapsed += REPAIR_HOLD_SLICE_MS) {
        if (s_repair_stop_requested) {
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(REPAIR_HOLD_SLICE_MS));
    }
}

/* Every fill ends in deep sleep, so each pass needs its own init. */
static esp_err_t repair_pass(esp_err_t (*fill)(void))
{
    esp_err_t err = epd_panel_current()->init();
    if (err == ESP_OK) {
        err = fill();
    }
    return err;
}

static void repair_task(void *argument)
{
    (void)argument;
    const epd_panel_ops_t *panel = epd_panel_current();
    uint32_t total = s_repair_cycles_total;
    EPD_LOGI(TAG, "ink repair started: %u cycle(s), %u ms hold per colour",
             (unsigned)total, (unsigned)REPAIR_HOLD_MS);

    xSemaphoreTake(s_display_mutex, portMAX_DELAY);
    /*
     * Drop the persisted baseline before touching the panel: a reboot in the
     * middle of the run must not leave the device believing the pre-repair
     * snapshot is still on screen.
     */
    esp_err_t err = device_config_clear_snapshot();
    for (uint32_t cycle = 0; err == ESP_OK && (total == 0 || cycle < total); cycle++) {
        err = repair_pass(panel->fill_black);
        if (err != ESP_OK) {
            break;
        }
        repair_hold();
        /* Always close the cycle on white so a stop never leaves it black. */
        err = repair_pass(panel->fill_white);
        if (err != ESP_OK) {
            break;
        }
        s_repair_cycles_done = cycle + 1;
        EPD_LOGI(TAG, "ink repair cycle %u done", (unsigned)s_repair_cycles_done);
        if (s_repair_stop_requested) {
            break;
        }
        if (total == 0 || cycle + 1 < total) {
            repair_hold();
        }
    }
    xSemaphoreGive(s_display_mutex);

    if (err == ESP_OK) {
        s_state = CONTROL_ACTIVE;
        s_full_refresh_requested = true;
        EPD_LOGI(TAG, "ink repair finished after %u cycle(s); queued full refresh",
                 (unsigned)s_repair_cycles_done);
    } else {
        s_state = CONTROL_REPAIR_FAILED;
        EPD_LOGE(TAG, "ink repair failed: %s; updates remain paused",
                 esp_err_to_name(err));
    }
    vTaskDelete(NULL);
}

esp_err_t device_control_request_repair(uint32_t cycles)
{
    if (s_display_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_state != CONTROL_ACTIVE) {
        return ESP_ERR_INVALID_STATE;
    }
    if (cycles > DEVICE_CONTROL_REPAIR_CYCLES_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    const epd_panel_ops_t *panel = epd_panel_current();
    if (panel->fill_black == NULL || panel->fill_white == NULL) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    s_repair_cycles_total = cycles;
    s_repair_cycles_done = 0;
    s_repair_stop_requested = false;
    s_state = CONTROL_REPAIRING;
    if (xTaskCreate(repair_task, "screen_repair", 4096, NULL, 6, NULL) != pdPASS) {
        s_state = CONTROL_ACTIVE;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t device_control_stop_repair(void)
{
    if (s_state != CONTROL_REPAIRING) {
        return ESP_ERR_INVALID_STATE;
    }
    s_repair_stop_requested = true;
    EPD_LOGI(TAG, "ink repair stop requested; finishing the current cycle");
    return ESP_OK;
}

uint32_t device_control_repair_cycles_done(void)
{
    return s_repair_cycles_done;
}

uint32_t device_control_repair_cycles_total(void)
{
    return s_repair_cycles_total;
}

bool device_control_updates_paused(void)
{
    return s_state != CONTROL_ACTIVE;
}

const char *device_control_state_name(void)
{
    switch (s_state) {
    case CONTROL_ACTIVE:
        return "active";
    case CONTROL_WHITENING:
        return "whitening";
    case CONTROL_WHITE:
        return "white";
    case CONTROL_WHITE_FAILED:
        return "white_failed";
    case CONTROL_REPAIRING:
        return "repairing";
    case CONTROL_REPAIR_FAILED:
        return "repair_failed";
    default:
        return "unknown";
    }
}
