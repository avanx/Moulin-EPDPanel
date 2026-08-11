#pragma once

#include "epd/panel.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * HINK-E0213A07-A1 / SSD1675A (IL3897) — 128×250 logical, black / white
 *
 * Product panel (L3): binds ssd1675a (L1, MSP430Etag / GxEPD2_213_B72) + single 1bpp.
 * Compatible with GDEH0213B72 timing; not the IL3895 A04 / Waveshare V1 panel.
 * Visible area is 122×250; frame uses 128-wide controller RAM.
 *
 * Frame: epd-bw-msb-v1, 1bpp MSB-first, 0=black / 1=white.
 * Partial refresh: supported (display_partial).
 */

#define EPD_HINK213A07_WIDTH        128
#define EPD_HINK213A07_HEIGHT       250
#define EPD_HINK213A07_VISIBLE_W    122
#define EPD_HINK213A07_LINE_BYTES   16      /* 128 / 8 */
#define EPD_HINK213A07_FULL_BYTES   (EPD_HINK213A07_LINE_BYTES * EPD_HINK213A07_HEIGHT)

/* Defined in hink_e0213a07.c; compiled only when CONFIG_EPD_PANEL_HINK_E0213A07. */
extern const epd_panel_ops_t hink_e0213a07_panel;

#ifdef __cplusplus
}
#endif
