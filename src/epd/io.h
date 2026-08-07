#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Shared EPD IO layer.
 *
 * Bit-banged SPI, busy-wait, command/data writes and the common
 * power-on / refresh / power-off + deep-sleep sequence. Panels with
 * identical timing (QYEG1085RYU579 color and QYEG1085BNF86BF0 mono, both
 * dual-panel) share this layer; a future panel with different timing
 * may implement its own IO instead of using these helpers.
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

void epd_write_command(uint8_t cmd);
void epd_write_data(uint8_t data);
void epd_write_command_cs(uint8_t cmd, uint8_t cs_num);
void epd_write_data_cs(uint8_t data, uint8_t cs_num);

/* Common power-on -> refresh -> power-off + deep-sleep sequence. */
esp_err_t epd_update_and_sleep(void);

/* Building blocks shared by full and partial refresh sequences. */
esp_err_t epd_power_on(void);              /* 0x04 + wait BUSY */
esp_err_t epd_power_off_deep_sleep(void);  /* 0x02 + wait BUSY + 0x07 0xA5 */

#ifdef __cplusplus
}
#endif
