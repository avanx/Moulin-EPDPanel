#pragma once

#include "epd/panel.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * QYEG1085BNF86BF0 / EPD_W21 (FITI) ×2 — 1360×480, monochrome (B/W)
 *
 * One glass, two controllers on a shared SPI (separate CS):
 *
 *   CS (Master)  drives physical columns   0 .. 679   local origin (0,0)
 *   CS2 (Slave)  drives physical columns 680 .. 1359  local origin (0,0) = phys 680
 *
 * Y is identical on both ICs (0 .. 479). Full-frame bytes are packed per row as
 * [Master 85 bytes | Slave 85 bytes] = 170 bytes/row. Partial 0x90 windows use
 * each IC's local X (Slave: local_x = screen_x - 680); do not mirror Slave X.
 */

#define EPD_BNF_WIDTH           1360
#define EPD_BNF_HEIGHT          480
#define EPD_BNF_PANEL_WIDTH     680     /* columns per IC; seam between Master|Slave */
#define EPD_BNF_LINE_BYTES      85      /* 680 / 8, 1 bpp, per IC */
#define EPD_BNF_COLUMN_BYTES    480
#define EPD_BNF_PANEL_BYTES     (EPD_BNF_LINE_BYTES * EPD_BNF_COLUMN_BYTES)
#define EPD_BNF_FULL_BYTES      (EPD_BNF_PANEL_BYTES * 2)

/* Defined in qyeg1085bnf86bf0.c; compiled only when CONFIG_EPD_PANEL_QYEG1085_MONO. */
extern const epd_panel_ops_t mono_panel;

#ifdef __cplusplus
}
#endif
