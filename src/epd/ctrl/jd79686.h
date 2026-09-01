#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "epd/layout/dual_lr.h"
#include "epd/panel.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * L1 Controller family — JD79686AB / FITI-like (1bpp, dual RAM 0x10/0x13,
 * optional register-LUT partial refresh via 0x90 windows).
 *
 * Used by QYEG1085BNF86BF0. Frame packing is dual-lr row-interleaved 1bpp.
 *
 * Partial refresh capability: yes (when EPD_MASTER_ONLY_TEST is 0).
 * Region encoding: epd-bw-msb-v1 planes [old][new]; width multiple of 8.
 * Cross-IC regions are split by epd_dual_lr_split_region.
 */

typedef struct {
    epd_dual_lr_cfg_t layout;
} jd79686_cfg_t;

esp_err_t jd79686_init(const jd79686_cfg_t *cfg);
esp_err_t jd79686_display_image(const jd79686_cfg_t *cfg, const uint8_t *image);
esp_err_t jd79686_fill_white(const jd79686_cfg_t *cfg);
esp_err_t jd79686_fill_black(const jd79686_cfg_t *cfg);

#if !EPD_MASTER_ONLY_TEST
esp_err_t jd79686_display_partial(const jd79686_cfg_t *cfg,
                                  const epd_region_t *regions,
                                  size_t count);
#endif

#ifdef __cplusplus
}
#endif
