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
} control_state_t;

static const char *TAG = "device_control";
static SemaphoreHandle_t s_display_mutex;
static volatile control_state_t s_state = CONTROL_ACTIVE;
static volatile bool s_full_refresh_requested;

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
    default:
        return "unknown";
    }
}
