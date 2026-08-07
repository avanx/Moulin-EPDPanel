#include "epd/layout/dual_lr.h"

uint16_t epd_dual_lr_ic_row_bytes(const epd_dual_lr_cfg_t *cfg)
{
    return (uint16_t)((uint32_t)cfg->panel_w * cfg->bpp / 8);
}

uint16_t epd_dual_lr_frame_row_bytes(const epd_dual_lr_cfg_t *cfg)
{
    return (uint16_t)(epd_dual_lr_ic_row_bytes(cfg) * 2);
}

const uint8_t *epd_dual_lr_ic_row_ptr(const epd_dual_lr_cfg_t *cfg,
                                      const uint8_t *frame,
                                      uint16_t row,
                                      uint8_t cs)
{
    uint16_t ic_bytes = epd_dual_lr_ic_row_bytes(cfg);
    if (cfg->pack == EPD_DUAL_LR_PACK_ROW_INTERLEAVED) {
        uint16_t frame_row = (uint16_t)(ic_bytes * 2);
        uint16_t off = (cs == EPD_CS_SLAVE) ? ic_bytes : 0;
        return &frame[(uint32_t)row * frame_row + off];
    }

    /* Panel-contiguous: [Master height*ic_bytes][Slave height*ic_bytes]. */
    uint32_t panel_bytes = (uint32_t)cfg->height * ic_bytes;
    uint32_t base = (cs == EPD_CS_SLAVE) ? panel_bytes : 0;
    return &frame[base + (uint32_t)row * ic_bytes];
}

int epd_dual_lr_split_region(const epd_dual_lr_cfg_t *cfg,
                             uint16_t x, uint16_t y,
                             uint16_t width, uint16_t height,
                             epd_dual_lr_subregion_t out[2])
{
    const uint32_t panel_w = cfg->panel_w;
    const uint32_t right = (uint32_t)x + width;
    int n = 0;

    if ((uint32_t)x < panel_w) {
        out[n].cs = EPD_CS_MASTER;
        out[n].px = x;
        out[n].py = y;
        out[n].ph = height;
        out[n].pw = (uint16_t)(right > panel_w ? panel_w - x : width);
        out[n].col0_bytes = 0;
        n++;
    }
    if (right > panel_w) {
        uint32_t start = (uint32_t)x > panel_w ? (uint32_t)x : panel_w;
        out[n].cs = EPD_CS_SLAVE;
        out[n].px = (uint16_t)(start - panel_w);
        out[n].py = y;
        out[n].ph = height;
        out[n].pw = (uint16_t)(right - start);
        out[n].col0_bytes = (uint16_t)((start - (uint32_t)x) / 8);
        n++;
    }
    return n;
}
