#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "epd/layout/single.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * L1 Controller family — SSD1619 / SSD1619A (B/W/R, dual RAM 0x24/0x26).
 *
 * Used by HINK-E042A13-A0 (GDEH042Z96 reference). Single IC on Master CS.
 * BUSY polarity is active-high (idle = 0), unlike UC8579/JD79686.
 *
 * Frame packing: epd-bwr-plane-v1 — [BW plane][R plane], each
 * (width/8)*height bytes, 1bpp MSB-first. BW plane 0=black / 1=white,
 * R plane 1=red / 0=no red; both match controller RAM and are written as-is.
 */

typedef struct {
    epd_single_cfg_t layout;    /* bpp must be 1; logical frame is 2 planes */
} ssd1619_cfg_t;

esp_err_t ssd1619_init(const ssd1619_cfg_t *cfg);

/*
 * Write concatenated [BW][R] planes then full refresh + deep sleep.
 * Both planes are written to RAM unchanged (no inversion).
 */
esp_err_t ssd1619_display_image(const ssd1619_cfg_t *cfg, const uint8_t *image);

/* Full white: BW=0xFF, R=0x00 (controller-native), then refresh + sleep. */
esp_err_t ssd1619_fill_white(const ssd1619_cfg_t *cfg);

/* Full black: BW=0x00, R=0x00, then refresh + sleep. */
esp_err_t ssd1619_fill_black(const ssd1619_cfg_t *cfg);

#ifdef __cplusplus
}
#endif
