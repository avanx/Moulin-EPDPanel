#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "device/config.h"
#include "esp_err.h"

/*
 * Ink-repair mode: alternate full black / full white with a hold in between
 * so aged pixels are driven through both extremes, clearing ink deposition
 * (ghosting). Requests with cycles == 0 loop until stopped.
 */
#define DEVICE_CONTROL_REPAIR_CYCLES_DEFAULT 5U
#define DEVICE_CONTROL_REPAIR_CYCLES_MAX     100U

esp_err_t device_control_init(void);
uint32_t device_control_poll(device_config_t *config);
esp_err_t device_control_request_full_refresh(void);
esp_err_t device_control_request_white(void);
esp_err_t device_control_request_repair(uint32_t cycles);
/* Ends the run after the current cycle finishes on white; never mid-black. */
esp_err_t device_control_stop_repair(void);
uint32_t device_control_repair_cycles_done(void);
uint32_t device_control_repair_cycles_total(void);
bool device_control_full_refresh_requested(void);
bool device_control_updates_paused(void);
const char *device_control_state_name(void);
