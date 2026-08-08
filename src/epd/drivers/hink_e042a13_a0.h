#pragma once

#include "epd/panel.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * HINK-E042A13-A0 / SSD1619 — 400×300, black / white / red
 *
 * Product panel (L3): binds ssd1619 (L1) + single 1bpp dual-plane (L2).
 * Compatible with Good Display GDEH042Z96 reference demo.
 *
 * Frame: epd-bwr-plane-v1 = [BW 15000][R 15000], 1bpp MSB-first.
 * Partial refresh: not supported (display_partial = NULL).
 */

#define EPD_HINK042_WIDTH        400
#define EPD_HINK042_HEIGHT       300
#define EPD_HINK042_LINE_BYTES   50      /* 400 / 8 */
#define EPD_HINK042_PLANE_BYTES  (EPD_HINK042_LINE_BYTES * EPD_HINK042_HEIGHT)
#define EPD_HINK042_FULL_BYTES   (EPD_HINK042_PLANE_BYTES * 2)

/* Defined in hink_e042a13_a0.c; compiled only when CONFIG_EPD_PANEL_HINK_E042A13_A0. */
extern const epd_panel_ops_t hink_e042a13_a0_panel;

#ifdef __cplusplus
}
#endif
