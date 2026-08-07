#include "net/wifi_manager.h"

#include <stdio.h>
#include <string.h>

#include "util/debug_log.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

static const char *TAG = "wifi";
static const EventBits_t CONNECTED_BIT = BIT0;
static EventGroupHandle_t s_events;
static esp_netif_t *s_sta_netif;
static bool s_initialized;
static bool s_provisioning;
static volatile bool s_scan_running;
static volatile bool s_scan_ready;
static char s_ap_ssid[24];

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)data;
    if (id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_events, CONNECTED_BIT);
        if (!s_provisioning) {
            esp_wifi_connect();
        }
    } else if (id == WIFI_EVENT_SCAN_DONE) {
        const wifi_event_sta_scan_done_t *event = data;
        s_scan_running = false;
        s_scan_ready = event != NULL && event->status == 0;
        if (s_scan_ready) {
            EPD_LOGI(TAG, "WiFi scan completed: %u access points",
                     event != NULL ? (unsigned)event->number : 0U);
        } else {
            EPD_LOGE(TAG, "WiFi scan failed, status=%u",
                     event != NULL ? (unsigned)event->status : 0U);
        }
    }
}

static void on_ip_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    if (id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = data;
        EPD_LOGI(TAG, "connected, IP=" IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_events, CONNECTED_BIT);
    }
}

esp_err_t wifi_manager_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif init");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event loop");
    s_sta_netif = esp_netif_create_default_wifi_sta();
    if (s_sta_netif == NULL) {
        return ESP_ERR_NO_MEM;
    }
    s_events = xEventGroupCreate();
    if (s_events == NULL) {
        return ESP_ERR_NO_MEM;
    }

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init), TAG, "WiFi init");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                   on_wifi_event, NULL), TAG, "WiFi handler");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                   on_ip_event, NULL), TAG, "IP handler");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "WiFi storage");
    s_initialized = true;
    return ESP_OK;
}

esp_err_t wifi_manager_connect(const device_config_t *config, uint32_t timeout_ms)
{
    if (!s_initialized || config == NULL || config->wifi_ssid[0] == '\0') {
        return ESP_ERR_INVALID_STATE;
    }

    wifi_config_t station = {0};
    memcpy(station.sta.ssid, config->wifi_ssid, strlen(config->wifi_ssid));
    memcpy(station.sta.password, config->wifi_password, strlen(config->wifi_password));
    station.sta.threshold.authmode = config->wifi_password[0] ?
                                     WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    station.sta.pmf_cfg.capable = true;
    station.sta.pmf_cfg.required = false;

    s_provisioning = false;
    xEventGroupClearBits(s_events, CONNECTED_BIT);
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "set STA mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &station), TAG, "set STA config");

    esp_err_t err = esp_wifi_start();
    if (err == ESP_ERR_WIFI_CONN) {
        err = esp_wifi_disconnect();
        if (err == ESP_OK || err == ESP_ERR_WIFI_NOT_CONNECT) {
            err = esp_wifi_connect();
        }
    }
    ESP_RETURN_ON_ERROR(err, TAG, "start WiFi");

    EPD_LOGI(TAG, "connecting to SSID %s", config->wifi_ssid);
    EventBits_t bits = xEventGroupWaitBits(s_events, CONNECTED_BIT, pdFALSE, pdTRUE,
                                           pdMS_TO_TICKS(timeout_ms));
    return (bits & CONNECTED_BIT) ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t wifi_manager_start_provisioning(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t mac[6];
    ESP_RETURN_ON_ERROR(esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP), TAG, "read MAC");
    snprintf(s_ap_ssid, sizeof(s_ap_ssid), "Moulin-EPDPanel-%02X%02X", mac[4], mac[5]);

    wifi_config_t access_point = {0};
    snprintf((char *)access_point.ap.ssid, sizeof(access_point.ap.ssid), "%s", s_ap_ssid);
    snprintf((char *)access_point.ap.password, sizeof(access_point.ap.password), "%s",
             EPD_SETUP_AP_PASSWORD);
    access_point.ap.ssid_len = strlen(s_ap_ssid);
    access_point.ap.channel = 1;
    access_point.ap.max_connection = 4;
    access_point.ap.authmode = WIFI_AUTH_WPA2_PSK;

    s_provisioning = true;
    s_scan_running = false;
    s_scan_ready = false;
    /*
     * A timed-out STA connection may still be retrying in the driver. Active
     * connection attempts make esp_wifi_scan_start() return ESP_ERR_WIFI_STATE.
     * Stop that attempt before exposing the setup page.
     */
    esp_err_t disconnect_err = esp_wifi_disconnect();
    if (disconnect_err != ESP_OK && disconnect_err != ESP_ERR_WIFI_NOT_CONNECT &&
        disconnect_err != ESP_ERR_WIFI_NOT_STARTED) {
        EPD_LOGW(TAG, "unable to stop STA connection: %s",
                 esp_err_to_name(disconnect_err));
    }
    esp_netif_create_default_wifi_ap();
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_APSTA), TAG, "set APSTA mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &access_point), TAG, "set AP config");
    esp_err_t err = esp_wifi_start();
    if (err != ESP_OK && err != ESP_ERR_WIFI_CONN) {
        return err;
    }
    EPD_LOGI(TAG, "setup AP ready: SSID=%s URL=http://192.168.4.1", s_ap_ssid);
    EPD_LOGD(TAG, "setup AP password=%s", EPD_SETUP_AP_PASSWORD);
    return ESP_OK;
}

esp_err_t wifi_manager_scan(wifi_ap_record_t *records, uint16_t *count)
{
    if (!s_initialized || records == NULL || count == NULL || *count == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_scan_ready) {
        esp_err_t err = esp_wifi_scan_get_ap_records(count, records);
        s_scan_ready = false;
        if (err != ESP_OK) {
            EPD_LOGE(TAG, "read WiFi scan results failed: %s", esp_err_to_name(err));
        }
        return err;
    }
    if (s_scan_running) {
        return ESP_ERR_NOT_FINISHED;
    }

    wifi_scan_config_t scan = {
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active = {
            .min = 40,
            .max = 120,
        },
    };
    EPD_LOGI(TAG, "starting asynchronous WiFi scan");
    esp_err_t err = esp_wifi_scan_start(&scan, false);
    if (err == ESP_ERR_WIFI_STATE && s_provisioning) {
        /*
         * Recover if a stale connection/scan survived the mode transition.
         * The WiFi event callback does not reconnect while provisioning.
         */
        esp_wifi_scan_stop();
        esp_wifi_disconnect();
        vTaskDelay(pdMS_TO_TICKS(100));
        err = esp_wifi_scan_start(&scan, false);
    }
    if (err != ESP_OK) {
        EPD_LOGE(TAG, "start WiFi scan failed: %s", esp_err_to_name(err));
        return err;
    }
    s_scan_running = true;
    return ESP_ERR_NOT_FINISHED;
}

bool wifi_manager_is_connected(void)
{
    return s_events != NULL && (xEventGroupGetBits(s_events) & CONNECTED_BIT) != 0;
}

bool wifi_manager_is_provisioning(void)
{
    return s_provisioning;
}

void wifi_manager_get_ip(char *buffer, size_t size)
{
    if (buffer == NULL || size == 0) {
        return;
    }
    esp_netif_ip_info_t info;
    if (s_sta_netif != NULL && esp_netif_get_ip_info(s_sta_netif, &info) == ESP_OK &&
        info.ip.addr != 0) {
        snprintf(buffer, size, IPSTR, IP2STR(&info.ip));
    } else {
        snprintf(buffer, size, "0.0.0.0");
    }
}

void wifi_manager_get_ap_ssid(char *buffer, size_t size)
{
    if (buffer != NULL && size > 0) {
        snprintf(buffer, size, "%s", s_ap_ssid);
    }
}
