#include "epd/drivers/hink_e042a13_a0.h"

#if defined(CONFIG_EPD_PANEL_HINK_E042A13_A0) && CONFIG_EPD_PANEL_HINK_E042A13_A0

#include "epd/ctrl/ssd1619.h"
#include "epd/io.h"

static const ssd1619_cfg_t s_cfg = {
    .layout = {
        .width = EPD_HINK042_WIDTH,
        .height = EPD_HINK042_HEIGHT,
        .bpp = 1,
    },
};

static esp_err_t panel_init(void)
{
    return ssd1619_init(&s_cfg);
}

static esp_err_t panel_display_image(const uint8_t *image)
{
    return ssd1619_display_image(&s_cfg, image);
}

static esp_err_t panel_fill_white(void)
{
    return ssd1619_fill_white(&s_cfg);
}

const epd_panel_ops_t hink_e042a13_a0_panel = {
    .type = "hink_e042a13_a0",
    .label = "HINK-E042A13-A0 · 黑白红 B/W/R · 400×300",
    .width = EPD_HINK042_WIDTH,
    .height = EPD_HINK042_HEIGHT,
    .frame_bytes = EPD_HINK042_FULL_BYTES,
    .color_mode = "bwr",
    .encoding = "epd-bwr-plane-v1",
    .panel_layout = "single",
    .update_type = "full",
    .io_init = epd_io_init,
    .init = panel_init,
    .display_image = panel_display_image,
    .fill_white = panel_fill_white,
    .display_partial = NULL,   /* SSD1619 BWR OTP: full refresh only */
};

#endif /* CONFIG_EPD_PANEL_HINK_E042A13_A0 */
