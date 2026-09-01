#include "epd/drivers/qyeg1085bnf86bf0.h"

#if defined(CONFIG_EPD_PANEL_QYEG1085_MONO) && CONFIG_EPD_PANEL_QYEG1085_MONO

#include "epd/ctrl/jd79686.h"
#include "epd/io.h"

static const jd79686_cfg_t s_cfg = {
    .layout = {
        .total_w = EPD_BNF_WIDTH,
        .panel_w = EPD_BNF_PANEL_WIDTH,
        .height = EPD_BNF_HEIGHT,
        .bpp = 1,
        .pack = EPD_DUAL_LR_PACK_ROW_INTERLEAVED,
    },
};

static esp_err_t panel_init(void)
{
    return jd79686_init(&s_cfg);
}

static esp_err_t panel_display_image(const uint8_t *image)
{
    return jd79686_display_image(&s_cfg, image);
}

static esp_err_t panel_fill_white(void)
{
    return jd79686_fill_white(&s_cfg);
}

static esp_err_t panel_fill_black(void)
{
    return jd79686_fill_black(&s_cfg);
}

#if !EPD_MASTER_ONLY_TEST
static esp_err_t panel_display_partial(const epd_region_t *regions, size_t count)
{
    return jd79686_display_partial(&s_cfg, regions, count);
}
#endif

const epd_panel_ops_t mono_panel = {
    .type = "qyeg1085_mono",
    .label = "QYEG1085BNF86BF0 · 黑白 B/W · 1360×480",
    .width = EPD_BNF_WIDTH,
    .height = EPD_BNF_HEIGHT,
    .frame_bytes = EPD_BNF_FULL_BYTES,
    .color_mode = "bw",
    .encoding = "epd-bw-msb-v1",
    .panel_layout = "dual-panel-left-right",
    .update_type = "full",
    .io_init = epd_io_init,
    .init = panel_init,
    .display_image = panel_display_image,
    .fill_white = panel_fill_white,
    .fill_black = panel_fill_black,
#if EPD_MASTER_ONLY_TEST
    .display_partial = NULL,
#else
    .display_partial = panel_display_partial,
#endif
};

#endif /* CONFIG_EPD_PANEL_QYEG1085_MONO */
