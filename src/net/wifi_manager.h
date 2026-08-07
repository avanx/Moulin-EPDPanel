#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "device/config.h"
#include "esp_err.h"
#include "esp_wifi_types.h"

#ifndef EPD_SETUP_AP_PASSWORD
#define EPD_SETUP_AP_PASSWORD "123123"
#endif

esp_err_t wifi_manager_init(void);
esp_err_t wifi_manager_connect(const device_config_t *config, uint32_t timeout_ms);
esp_err_t wifi_manager_start_provisioning(void);
esp_err_t wifi_manager_scan(wifi_ap_record_t *records, uint16_t *count);
bool wifi_manager_is_connected(void);
bool wifi_manager_is_provisioning(void);
void wifi_manager_get_ip(char *buffer, size_t size);
void wifi_manager_get_ap_ssid(char *buffer, size_t size);
