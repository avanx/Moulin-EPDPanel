#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * L0 Bus — shared EPD IO (bit-banged SPI + pins).
 *
 * Provides only transport: GPIO setup, reset pulse, busy-wait, and
 * command/data writes (broadcast or per-CS). Controller refresh opcodes
 * (power-on / refresh / deep-sleep) live inside each epd/ctrl family (L1).
 *
 * Default pin map for ESP32-S3-DevKitM-1 (avoid USB 19/20, strapping 0/3/45/46).
 *
 *   BUSY -> GPIO4
 *   RST  -> GPIO5
 *   DC   -> GPIO6
 *   CS   -> GPIO7   (Master / left)
 *   CS2  -> GPIO8   (Slave  / right)
 *   CLK  -> GPIO9
 *   MOSI -> GPIO10
 */

#ifndef EPD_PIN_BUSY
#define EPD_PIN_BUSY  4
#endif
#ifndef EPD_PIN_RST
#define EPD_PIN_RST   5
#endif
#ifndef EPD_PIN_DC
#define EPD_PIN_DC    6
#endif
#ifndef EPD_PIN_CS
#define EPD_PIN_CS    7
#endif
#ifndef EPD_PIN_CS2
#define EPD_PIN_CS2   8
#endif
#ifndef EPD_PIN_CLK
#define EPD_PIN_CLK   9
#endif
#ifndef EPD_PIN_MOSI
#define EPD_PIN_MOSI  10
#endif

/*
 * Diagnostic mode for hardware exposing only the Master CS.
 * Broadcast writes are routed to EPD_PIN_CS only; Slave operations must be
 * skipped by the panel driver.
 */
#ifndef EPD_MASTER_ONLY_TEST
#define EPD_MASTER_ONLY_TEST 0
#endif

#define EPD_CS_MASTER 0
#define EPD_CS_SLAVE  1

esp_err_t epd_io_init(void);
void epd_delay_ms(uint32_t ms);
esp_err_t epd_wait_busy(void);

/* Pulse RST low then high. */
void epd_io_reset(uint32_t low_ms, uint32_t high_ms);

void epd_write_command(uint8_t cmd);
void epd_write_data(uint8_t data);
void epd_write_command_cs(uint8_t cmd, uint8_t cs_num);
void epd_write_data_cs(uint8_t data, uint8_t cs_num);

#ifdef __cplusplus
}
#endif
