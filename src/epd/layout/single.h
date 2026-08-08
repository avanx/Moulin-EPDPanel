#pragma once

#include <stdint.h>

#include "epd/io.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * L2 Layout — single-panel topology (one controller, full logical frame).
 *
 * Used by HINK-E042A13-A0 (SSD1619 BWR) and HINK-E0213A04 (IL3895 BW).
 */

typedef struct {
    uint16_t width;
    uint16_t height;
    uint8_t bpp;                /* bits per pixel */
} epd_single_cfg_t;

/* Identity map of a full-screen region onto the single IC (Master CS). */
typedef struct {
    uint8_t cs;                 /* always EPD_CS_MASTER */
    uint16_t px, py;
    uint16_t pw, ph;
    uint16_t col0_bytes;        /* always 0 for a single IC */
} epd_single_subregion_t;

uint16_t epd_single_row_bytes(const epd_single_cfg_t *cfg);

const uint8_t *epd_single_row_ptr(const epd_single_cfg_t *cfg,
                                  const uint8_t *frame,
                                  uint16_t row);

/* Returns 1 and fills out[0], or 0 if the region is empty/out of bounds. */
int epd_single_map_region(const epd_single_cfg_t *cfg,
                          uint16_t x, uint16_t y,
                          uint16_t width, uint16_t height,
                          epd_single_subregion_t out[1]);

#ifdef __cplusplus
}
#endif
