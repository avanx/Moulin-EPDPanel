#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "device/config.h"
#include "esp_err.h"

esp_err_t device_control_init(void);
uint32_t device_control_poll(device_config_t *config);
esp_err_t device_control_request_full_refresh(void);
esp_err_t device_control_request_white(void);
bool device_control_full_refresh_requested(void);
bool device_control_updates_paused(void);
const char *device_control_state_name(void);
