#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "epd/layout/dual_lr.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * L1 Controller family — UC8579cGAC-like (4-color OTP, single RAM plane 0x10).
 *
 * Used by QYEG1085RYU579. Frame packing is dual-lr panel-contiguous 2bpp.
 */

typedef struct {
    epd_dual_lr_cfg_t layout;
    uint8_t x_addr_h;
    uint8_t x_addr_l;
    uint8_t y_addr_h;
    uint8_t y_addr_l;
} uc8579_cfg_t;

/* Optional per-byte remap (e.g. Image2LCD palette -> panel 2bpp). NULL = identity. */
typedef uint8_t (*uc8579_byte_remap_fn)(uint8_t byte);

esp_err_t uc8579_init(const uc8579_cfg_t *cfg);

/* Broadcast fill of both ICs' 0x10 planes, then full refresh + sleep. */
esp_err_t uc8579_fill(const uc8579_cfg_t *cfg, uint8_t fill_byte);

/*
 * Write a packed dual-lr frame (panel-contiguous 2bpp) to Master then Slave
 * 0x10 planes, optionally remapping each byte, then full refresh + sleep.
 */
esp_err_t uc8579_display_image(const uc8579_cfg_t *cfg,
                               const uint8_t *image,
                               uc8579_byte_remap_fn remap);

#ifdef __cplusplus
}
#endif
