#pragma once

#include "epd/panel.h"

#ifdef __cplusplus
extern "C" {
#endif

/* QYEG1085RYU579 / UC8579cGAC x2 — 1360x480, 4-color (B/W/Y/R)
 *
 * Product panel (L3): binds uc8579 (L1) + dual_lr contiguous 2bpp (L2).
 * Partial refresh: not supported (display_partial = NULL).
 */

#define EPD_COLOR_BLACK   0x00
#define EPD_COLOR_WHITE   0x01
#define EPD_COLOR_YELLOW  0x02
#define EPD_COLOR_RED     0x03

#define EPD_WIDTH           1360
#define EPD_HEIGHT          480
#define EPD_PANEL_WIDTH     680
#define EPD_MAX_LINE_BYTES  170   /* 680 / 4 pixels per byte (2 bpp) */
#define EPD_MAX_COLUMN_BYTES 480
#define EPD_PANEL_BYTES     (EPD_MAX_LINE_BYTES * EPD_MAX_COLUMN_BYTES)
#define EPD_FULL_BYTES      (EPD_PANEL_BYTES * 2)

#define EPD_X_ADDR_START_H  0x02
#define EPD_X_ADDR_START_L  0xA8
#define EPD_Y_ADDR_START_H  0x01
#define EPD_Y_ADDR_START_L  0xE0

/* Defined in qyeg1085ryu579.c; compiled only when CONFIG_EPD_PANEL_QYEG1085_COLOR. */
extern const epd_panel_ops_t color_panel;

#ifdef __cplusplus
}
#endif
