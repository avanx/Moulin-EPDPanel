#pragma once

#include <stdint.h>

#include "device/config.h"

/**
 * Poll once, optionally download and refresh the display.
 * Returns the number of seconds to wait before the next poll.
 */
uint32_t display_client_poll(device_config_t *config, bool force_full);
