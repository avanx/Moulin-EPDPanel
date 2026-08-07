#pragma once

#include "epd/panel.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * QYEG1085BNF86BF0 / JD79686AB (FITI) ×2 — 1360×480, monochrome (B/W)
 *
 * Product panel (L3): binds jd79686 (L1) + dual_lr row-interleaved 1bpp (L2).
 *
 *   CS (Master)  drives physical columns   0 .. 679   local origin (0,0)
 *   CS2 (Slave)  drives physical columns 680 .. 1359  local origin (0,0) = phys 680
 *
 * Partial refresh: supported (display_partial set when not EPD_MASTER_ONLY_TEST).
 * Region encoding: epd-bw-msb-v1 [old][new]; width multiple of 8; cross-seam OK.
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
