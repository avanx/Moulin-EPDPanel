#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define DEVICE_WIFI_SSID_MAX       32
#define DEVICE_WIFI_PASSWORD_MAX   63
#define DEVICE_SERVER_URL_MAX      192
#define DEVICE_SCREEN_ID_MAX       37
#define DEVICE_API_KEY_MAX         128
#define DEVICE_SNAPSHOT_ID_MAX     64
#define DEVICE_PANEL_TYPE_MAX      24

typedef struct {
    char wifi_ssid[DEVICE_WIFI_SSID_MAX + 1];
    char wifi_password[DEVICE_WIFI_PASSWORD_MAX + 1];
    char server_url[DEVICE_SERVER_URL_MAX + 1];
    char screen_id[DEVICE_SCREEN_ID_MAX + 1];
    char api_key[DEVICE_API_KEY_MAX + 1];
    char panel_type[DEVICE_PANEL_TYPE_MAX + 1];
    char snapshot_id[DEVICE_SNAPSHOT_ID_MAX + 1];
    uint32_t refresh_after_seconds;
} device_config_t;

esp_err_t device_config_init(void);
esp_err_t device_config_load(device_config_t *config);
esp_err_t device_config_save(const device_config_t *config);
esp_err_t device_config_save_wifi(const device_config_t *config);
esp_err_t device_config_save_device_settings(const device_config_t *config);
/* Persist panel type only (no server fields). Clears cached snapshot. */
esp_err_t device_config_save_panel_type(const char *panel_type);
esp_err_t device_config_save_manifest_state(const char *snapshot_id,
                                            uint32_t refresh_after_seconds);
esp_err_t device_config_clear_snapshot(void);
bool device_config_has_wifi(const device_config_t *config);
bool device_config_is_complete(const device_config_t *config);
bool device_config_validate_wifi(const device_config_t *config);
bool device_config_validate_device_settings(const device_config_t *config);
bool device_config_validate(const device_config_t *config);
