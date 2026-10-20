#include "epd/panel.h"

#include "device/config.h"
#include "device/control.h"
#include "net/provisioning_server.h"
#include "net/wifi_manager.h"
#include "util/debug_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "main";
static device_config_t s_config;

static void polling_task(void *argument)
{
    (void)argument;
    uint32_t disconnected_seconds = 0;
    while (true) {
        if (!wifi_manager_is_connected()) {
            vTaskDelay(pdMS_TO_TICKS(5000));
            disconnected_seconds += 5;
            if (disconnected_seconds >= 60 && !wifi_manager_is_provisioning()) {
                EPD_LOGW(TAG, "WiFi unavailable; enabling setup access point");
                if (wifi_manager_start_provisioning() == ESP_OK) {
                    provisioning_server_start();
                }
            }
            continue;
        }

        disconnected_seconds = 0;
        int64_t poll_started_us = esp_timer_get_time();
        uint32_t wait_seconds = device_control_poll(&s_config);
        int64_t elapsed_us = esp_timer_get_time() - poll_started_us;
        uint32_t elapsed_seconds = elapsed_us > 0 ? (uint32_t)(elapsed_us / 1000000) : 0;
        if (elapsed_seconds >= wait_seconds) {
            wait_seconds = 0;
        } else {
            wait_seconds -= elapsed_seconds;
        }
        EPD_LOGD(TAG, "next poll in %u seconds", (unsigned)wait_seconds);
        while (wait_seconds-- > 0) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            if (!wifi_manager_is_connected()) {
                break;
            }
            if (device_control_full_refresh_requested()) {
                EPD_LOGI(TAG, "interrupting poll wait for full-screen refresh");
                break;
            }
        }
    }
}

static void start_setup_mode(void)
{
    ESP_ERROR_CHECK(wifi_manager_start_provisioning());
    ESP_ERROR_CHECK(provisioning_server_start());
    EPD_LOGI(TAG, "configure the device at http://192.168.4.1");
}

void app_main(void)
{
    ESP_ERROR_CHECK(device_config_init());
    ESP_ERROR_CHECK(device_config_load(&s_config));
    const epd_panel_ops_t *panel = epd_panel_select(s_config.panel_type);
    EPD_LOGI(TAG, "Moulin-EPDPanel start: ESP32-S3, %s %dx%d",
             panel->label, panel->width, panel->height);
    ESP_ERROR_CHECK(wifi_manager_init());
    ESP_ERROR_CHECK(device_control_init());
    ESP_ERROR_CHECK(panel->io_init());

    if (!device_config_has_wifi(&s_config) || !device_config_validate_wifi(&s_config)) {
        EPD_LOGW(TAG, "WiFi configuration is incomplete");
        start_setup_mode();
        return;
    }
    esp_err_t wifi_err = wifi_manager_connect(&s_config, 30000);
    if (wifi_err != ESP_OK) {
        EPD_LOGW(TAG, "WiFi connection failed: %s", esp_err_to_name(wifi_err));
        start_setup_mode();
        return;
    }

    ESP_ERROR_CHECK(provisioning_server_start());
    if (!device_config_is_complete(&s_config) || !device_config_validate(&s_config)) {
        EPD_LOGW(TAG, "device parameters are incomplete; open the Station IP to configure");
        return;
    }

    BaseType_t created = xTaskCreate(polling_task, "display_poll", 10240, NULL, 5, NULL);
    ESP_ERROR_CHECK(created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
}
