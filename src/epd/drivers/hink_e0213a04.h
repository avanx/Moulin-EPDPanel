#pragma once

#include "epd/panel.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * HINK-E0213A04 / IL3895 — 128×250 logical, black / white
 *
 * Product panel (L3): binds il3895 (L1, Waveshare epd2in13 V1) + single 1bpp.
 * Compatible with Waveshare 2.13" e-Paper V1 (IL3895), not GDEH0213B73/0xC7.
 * Visible area is 122×250; frame uses 128-wide controller RAM.
 *
 * Frame: epd-bw-msb-v1, 1bpp MSB-first, 0=black / 1=white.
 * Partial refresh: supported (display_partial).
 */

#define EPD_HINK213_WIDTH        128
#define EPD_HINK213_HEIGHT       250
#define EPD_HINK213_VISIBLE_W    122
#define EPD_HINK213_LINE_BYTES   16      /* 128 / 8 */
#define EPD_HINK213_FULL_BYTES   (EPD_HINK213_LINE_BYTES * EPD_HINK213_HEIGHT)

/* Defined in hink_e0213a04.c; compiled only when CONFIG_EPD_PANEL_HINK_E0213A04. */
extern const epd_panel_ops_t hink_e0213a04_panel;

#ifdef __cplusplus
}
#endif
