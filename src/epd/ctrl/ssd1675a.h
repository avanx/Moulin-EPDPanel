#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "epd/layout/single.h"
#include "epd/panel.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * L1 Controller family — SSD1675A / IL3897 (HINK-E0213A07-A1).
 *
 * Timing mirrors MSP430Etag epd_hink0213a07 / GxEPD2_213_B72 / Adafruit SSD1675.
 * 70-byte LUT; PowerOn (0xC0) after init; DisplayFrame 0xC4 (no 0xFF terminate);
 * Sleep = PowerOff (0xC3) + deep sleep 0x10/0x01.
 *
 * Frame: epd-bw-msb-v1, 1bpp MSB-first, 1=black / 0=white; the driver inverts
 * each byte when writing RAM (RAM 0=black, fill 0xFF = white).
 * Logical RAM 128×250; visible 122×250.
 *
 * Dual buffer: write + DisplayFrame twice (MSP430 main.cpp / GxEPD2).
 * Partial: old plane -> RAM 0x26, new plane -> RAM 0x24, then 0xC4.
 */

typedef struct {
    epd_single_cfg_t layout;    /* bpp must be 1 */
} ssd1675a_cfg_t;

esp_err_t ssd1675a_init(const ssd1675a_cfg_t *cfg);

/* SetFrameMemory + DisplayFrame, twice, then Sleep. */
esp_err_t ssd1675a_display_image(const ssd1675a_cfg_t *cfg, const uint8_t *image);

/* ClearFrameMemory(0xFF) + DisplayFrame, twice, then Sleep. */
esp_err_t ssd1675a_fill_white(const ssd1675a_cfg_t *cfg);

/* ClearFrameMemory(0x00) + DisplayFrame, twice, then Sleep. */
esp_err_t ssd1675a_fill_black(const ssd1675a_cfg_t *cfg);

/*
 * Partial: Init(lut_partial) + PowerOn, then for each region write old->0x26 /
 * new->0x24 and DisplayFrame once. Width multiple of 8.
 */
esp_err_t ssd1675a_display_partial(const ssd1675a_cfg_t *cfg,
                                   const epd_region_t *regions,
                                   size_t count);

#ifdef __cplusplus
}
#endif
