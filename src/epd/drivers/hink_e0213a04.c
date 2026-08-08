#include "epd/drivers/hink_e0213a04.h"

#if defined(CONFIG_EPD_PANEL_HINK_E0213A04) && CONFIG_EPD_PANEL_HINK_E0213A04

#include "epd/ctrl/il3895.h"
#include "epd/io.h"

static const il3895_cfg_t s_cfg = {
    .layout = {
        .width = EPD_HINK213_WIDTH,
        .height = EPD_HINK213_HEIGHT,
        .bpp = 1,
    },
};

static esp_err_t panel_init(void)
{
    return il3895_init(&s_cfg);
}

static esp_err_t panel_display_image(const uint8_t *image)
{
    return il3895_display_image(&s_cfg, image);
}

static esp_err_t panel_fill_white(void)
{
    return il3895_fill_white(&s_cfg);
}

static esp_err_t panel_display_partial(const epd_region_t *regions, size_t count)
{
    return il3895_display_partial(&s_cfg, regions, count);
}

const epd_panel_ops_t hink_e0213a04_panel = {
    .type = "hink_e0213a04",
    .label = "HINK-E0213A04 · 黑白 B/W · 128×250",
    .width = EPD_HINK213_WIDTH,
    .height = EPD_HINK213_HEIGHT,
    .frame_bytes = EPD_HINK213_FULL_BYTES,
    .color_mode = "bw",
    .encoding = "epd-bw-msb-v1",
    .panel_layout = "single",
    .update_type = "full",
    .io_init = epd_io_init,
    .init = panel_init,
    .display_image = panel_display_image,
    .fill_white = panel_fill_white,
    .display_partial = panel_display_partial,
};

#endif /* CONFIG_EPD_PANEL_HINK_E0213A04 */
