#include "epd/layout/single.h"

uint16_t epd_single_row_bytes(const epd_single_cfg_t *cfg)
{
    return (uint16_t)((uint32_t)cfg->width * cfg->bpp / 8);
}

const uint8_t *epd_single_row_ptr(const epd_single_cfg_t *cfg,
                                  const uint8_t *frame,
                                  uint16_t row)
{
    return &frame[(uint32_t)row * epd_single_row_bytes(cfg)];
}

int epd_single_map_region(const epd_single_cfg_t *cfg,
                          uint16_t x, uint16_t y,
                          uint16_t width, uint16_t height,
                          epd_single_subregion_t out[1])
{
    if (width == 0 || height == 0) {
        return 0;
    }
    if ((uint32_t)x + width > cfg->width ||
        (uint32_t)y + height > cfg->height) {
        return 0;
    }

    out[0].cs = EPD_CS_MASTER;
    out[0].px = x;
    out[0].py = y;
    out[0].pw = width;
    out[0].ph = height;
    out[0].col0_bytes = 0;
    return 1;
}
