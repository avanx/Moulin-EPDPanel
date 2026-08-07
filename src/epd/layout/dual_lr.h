#pragma once

#include <stdint.h>

#include "epd/io.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * L2 Layout — dual-panel left/right topology.
 *
 * Maps full-screen geometry onto Master (left) / Slave (right) IC-local
 * windows and locates per-IC row slices inside a packed frame buffer.
 */

typedef enum {
    /* Each row is [Master bytes | Slave bytes]. Used by 1bpp mono. */
    EPD_DUAL_LR_PACK_ROW_INTERLEAVED = 0,
    /* [entire Master panel][entire Slave panel]. Used by 2bpp color. */
    EPD_DUAL_LR_PACK_PANEL_CONTIGUOUS = 1,
} epd_dual_lr_pack_t;

typedef struct {
    uint16_t total_w;           /* full logical width (both ICs) */
    uint16_t panel_w;           /* columns per IC (seam at panel_w) */
    uint16_t height;
    uint8_t bpp;                /* bits per pixel: 1 or 2 */
    epd_dual_lr_pack_t pack;
} epd_dual_lr_cfg_t;

/*
 * One slice of a full-screen region on a single IC, in that IC's local space.
 * col0_bytes is the first byte column within a 1bpp region plane (width/8).
 */
typedef struct {
    uint8_t cs;                 /* EPD_CS_MASTER / EPD_CS_SLAVE */
    uint16_t px, py;            /* local window origin */
    uint16_t pw, ph;            /* local window size */
    uint16_t col0_bytes;        /* byte offset into region plane rows */
} epd_dual_lr_subregion_t;

/* Bytes per IC for one row (= panel_w * bpp / 8). */
uint16_t epd_dual_lr_ic_row_bytes(const epd_dual_lr_cfg_t *cfg);

/* Full-frame bytes per logical row (both ICs). */
uint16_t epd_dual_lr_frame_row_bytes(const epd_dual_lr_cfg_t *cfg);

/* Pointer to one IC's bytes for `row` inside a packed full frame. */
const uint8_t *epd_dual_lr_ic_row_ptr(const epd_dual_lr_cfg_t *cfg,
                                      const uint8_t *frame,
                                      uint16_t row,
                                      uint8_t cs);

/*
 * Full-screen (x,y,w,h) -> up to 2 Master/Slave local windows.
 *   x+w <= panel_w  -> Master only
 *   x >= panel_w    -> Slave only
 *   else            -> both (Slave local_x = 0 when spanning seam)
 * Returns number of subregions written to out[0..1] (0..2).
 * Assumes x and width are multiples of 8 for 1bpp col0_bytes.
 */
int epd_dual_lr_split_region(const epd_dual_lr_cfg_t *cfg,
                             uint16_t x, uint16_t y,
                             uint16_t width, uint16_t height,
                             epd_dual_lr_subregion_t out[2]);

#ifdef __cplusplus
}
#endif
