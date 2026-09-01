#include "epd/drivers/qyeg1085ryu579.h"

#if defined(CONFIG_EPD_PANEL_QYEG1085_COLOR) && CONFIG_EPD_PANEL_QYEG1085_COLOR

#include "epd/ctrl/uc8579.h"
#include "epd/io.h"

static const uc8579_cfg_t s_cfg = {
    .layout = {
        .total_w = EPD_WIDTH,
        .panel_w = EPD_PANEL_WIDTH,
        .height = EPD_HEIGHT,
        .bpp = 2,
        .pack = EPD_DUAL_LR_PACK_PANEL_CONTIGUOUS,
    },
    .x_addr_h = EPD_X_ADDR_START_H,
    .x_addr_l = EPD_X_ADDR_START_L,
    .y_addr_h = EPD_Y_ADDR_START_H,
    .y_addr_l = EPD_Y_ADDR_START_L,
};

/* Image2LCD palette nibble -> panel 2bpp encoding */
static uint8_t color_get_nibble(uint8_t nibble)
{
    switch (nibble & 0x03) {
    case 0x00: return EPD_COLOR_WHITE;
    case 0x01: return EPD_COLOR_YELLOW;
    case 0x02: return EPD_COLOR_RED;
    case 0x03: return EPD_COLOR_BLACK;
    default:   return EPD_COLOR_WHITE;
    }
}

static uint8_t color_byte_remap(uint8_t temp)
{
    return (uint8_t)((color_get_nibble(temp >> 6) << 6) |
                     (color_get_nibble(temp >> 4) << 4) |
                     (color_get_nibble(temp >> 2) << 2) |
                     color_get_nibble(temp));
}

static esp_err_t panel_init(void)
{
    return uc8579_init(&s_cfg);
}

static esp_err_t panel_fill_white(void)
{
    return uc8579_fill(&s_cfg, 0x55);
}

/* 2bpp: four EPD_COLOR_BLACK (0x00) pixels per byte. */
static esp_err_t panel_fill_black(void)
{
    return uc8579_fill(&s_cfg, 0x00);
}

static esp_err_t panel_display_image(const uint8_t *image)
{
    return uc8579_display_image(&s_cfg, image, color_byte_remap);
}

const epd_panel_ops_t color_panel = {
    .type = "qyeg1085_color",
    .label = "QYEG1085RYU579 · 彩色 B/W/R/Y · 1360×480",
    .width = EPD_WIDTH,
    .height = EPD_HEIGHT,
    .frame_bytes = EPD_FULL_BYTES,
    .color_mode = "bwry",
    .encoding = "edp-bwry-v1",
    .panel_layout = "dual-panel-left-right",
    .update_type = "full",
    .io_init = epd_io_init,
    .init = panel_init,
    .display_image = panel_display_image,
    .fill_white = panel_fill_white,
    .fill_black = panel_fill_black,
    .display_partial = NULL,   /* UC8579 color: no partial refresh */
};

#endif /* CONFIG_EPD_PANEL_QYEG1085_COLOR */
