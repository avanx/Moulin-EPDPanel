#include "epd/drivers/hink_e0213a07.h"

#if defined(CONFIG_EPD_PANEL_HINK_E0213A07) && CONFIG_EPD_PANEL_HINK_E0213A07

#include "epd/ctrl/ssd1675a.h"
#include "epd/io.h"

static const ssd1675a_cfg_t s_cfg = {
    .layout = {
        .width = EPD_HINK213A07_WIDTH,
        .height = EPD_HINK213A07_HEIGHT,
        .bpp = 1,
    },
};

static esp_err_t panel_init(void)
{
    return ssd1675a_init(&s_cfg);
}

static esp_err_t panel_display_image(const uint8_t *image)
{
    return ssd1675a_display_image(&s_cfg, image);
}

static esp_err_t panel_fill_white(void)
{
    return ssd1675a_fill_white(&s_cfg);
}

static esp_err_t panel_fill_black(void)
{
    return ssd1675a_fill_black(&s_cfg);
}

static esp_err_t panel_display_partial(const epd_region_t *regions, size_t count)
{
    return ssd1675a_display_partial(&s_cfg, regions, count);
}

const epd_panel_ops_t hink_e0213a07_panel = {
    .type = "hink_e0213a07",
    .label = "HINK-E0213A07-A1 · 黑白 B/W · 128×250",
    .width = EPD_HINK213A07_WIDTH,
    .height = EPD_HINK213A07_HEIGHT,
    .frame_bytes = EPD_HINK213A07_FULL_BYTES,
    .color_mode = "bw",
    .encoding = "epd-bw-msb-v1",
    .panel_layout = "single",
    .update_type = "full",
    .io_init = epd_io_init,
    .init = panel_init,
    .display_image = panel_display_image,
    .fill_white = panel_fill_white,
    .fill_black = panel_fill_black,
    .display_partial = panel_display_partial,
};

#endif /* CONFIG_EPD_PANEL_HINK_E0213A07 */
