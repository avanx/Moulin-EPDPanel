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
 * L1 Controller family — IL3895 (Waveshare 2.13" V1 / epd2in13).
 *
 * Implementation mirrors reference/epd2in13 (Epd::Init / SetFrameMemory /
 * DisplayFrame / Sleep). Used by HINK-E0213A04.
 *
 * DisplayFrame always uses 0xC4. After each refresh the next WRITE_RAM goes
 * to the other ping-pong buffer, so a full image is written+refreshed twice.
 *
 * Frame: epd-bw-msb-v1, 1bpp MSB-first, 0=black / 1=white (0xFF = white).
 * Logical RAM 128×250; visible 122×250.
 */

typedef struct {
    epd_single_cfg_t layout;    /* bpp must be 1 */
} il3895_cfg_t;

esp_err_t il3895_init(const il3895_cfg_t *cfg);

/* SetFrameMemory + DisplayFrame, twice, then Sleep. */
esp_err_t il3895_display_image(const il3895_cfg_t *cfg, const uint8_t *image);

/* ClearFrameMemory(0xFF) + DisplayFrame, twice, then Sleep. */
esp_err_t il3895_fill_white(const il3895_cfg_t *cfg);

/*
 * Partial: Init(lut_partial), then for each region prime old / apply new
 * with write+DisplayFrame twice each. Width multiple of 8.
 */
esp_err_t il3895_display_partial(const il3895_cfg_t *cfg,
                                 const epd_region_t *regions,
                                 size_t count);

#ifdef __cplusplus
}
#endif
