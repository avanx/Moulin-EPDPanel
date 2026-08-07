#include "epd/drivers/qyeg1085ryu579.h"

#if defined(CONFIG_EPD_PANEL_QYEG1085_COLOR) && CONFIG_EPD_PANEL_QYEG1085_COLOR

#include "epd/io.h"

#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "epd_color";

/* Color_get(): Image2LCD palette -> panel 2bpp encoding */
static uint8_t color_get(uint8_t color)
{
    switch (color & 0x03) {
    case 0x00: return EPD_COLOR_WHITE;
    case 0x01: return EPD_COLOR_YELLOW;
    case 0x02: return EPD_COLOR_RED;
    case 0x03: return EPD_COLOR_BLACK;
    default:   return EPD_COLOR_WHITE;
    }
}

static esp_err_t epd_init(void)
{
    ESP_LOGI(TAG, "panel init start (%dx%d)", EPD_WIDTH, EPD_HEIGHT);
    epd_delay_ms(100);
    gpio_set_level(EPD_PIN_RST, 0);
    epd_delay_ms(20);
    gpio_set_level(EPD_PIN_RST, 1);
    epd_delay_ms(50);
    esp_err_t err = epd_wait_busy();
    if (err != ESP_OK) {
        return err;
    }

    epd_write_command(0x00); /* PSR */
    epd_write_data(0x2F);    /* OTP mode */
    epd_write_data(0x29);

    epd_write_command(0x01); /* PWR */
    epd_write_data(0x07);
    epd_write_data(0x00);

    epd_write_command(0x03); /* PFS */
    epd_write_data(0x00);
    epd_write_data(0x54);
    epd_write_data(0x44);

    epd_write_command(0x06); /* BTST */
    epd_write_data(0x17);
    epd_write_data(0x24);
    epd_write_data(0x2C);
    epd_write_data(0x58);

    epd_write_command(0x30); /* PLL */
    epd_write_data(0x08);

    epd_write_command(0x50); /* CDI */
    epd_write_data(0x37);    /* border white */

    epd_write_command(0x60); /* TCON */
    epd_write_data(0x02);
    epd_write_data(0x02);

    epd_write_command(0xE3);
    epd_write_data(0x22);

    epd_write_command(0xE9);
    epd_write_data(0x01);

    epd_write_command(0xE0);
    epd_write_data(0x01);

    /* MASTER */
    epd_write_command_cs(0x61, EPD_CS_MASTER);
    epd_write_data_cs(EPD_X_ADDR_START_H, EPD_CS_MASTER);
    epd_write_data_cs(EPD_X_ADDR_START_L, EPD_CS_MASTER);
    epd_write_data_cs(EPD_Y_ADDR_START_H, EPD_CS_MASTER);
    epd_write_data_cs(EPD_Y_ADDR_START_L, EPD_CS_MASTER);

    epd_write_command(0x65);
    epd_write_data_cs(0x00, EPD_CS_MASTER);
    epd_write_data_cs(0x00, EPD_CS_MASTER);
    epd_write_data_cs(0x00, EPD_CS_MASTER);
    epd_write_data_cs(0x00, EPD_CS_MASTER);

    /* SLAVE */
    epd_write_command_cs(0x61, EPD_CS_SLAVE);
    epd_write_data_cs(EPD_X_ADDR_START_H, EPD_CS_SLAVE);
    epd_write_data_cs(EPD_X_ADDR_START_L, EPD_CS_SLAVE);
    epd_write_data_cs(EPD_Y_ADDR_START_H, EPD_CS_SLAVE);
    epd_write_data_cs(EPD_Y_ADDR_START_L, EPD_CS_SLAVE);

    epd_write_command(0x65);
    epd_write_data_cs(0x00, EPD_CS_SLAVE);
    epd_write_data_cs(0x00, EPD_CS_SLAVE);
    epd_write_data_cs(0x00, EPD_CS_SLAVE);
    epd_write_data_cs(0x00, EPD_CS_SLAVE);
    ESP_LOGI(TAG, "panel init done");
    return ESP_OK;
}

static esp_err_t epd_fill_both(uint8_t fill)
{
    ESP_LOGI(TAG, "fill screen data=0x%02X (%lu bytes x2 via dual CS)",
             fill, (unsigned long)EPD_PANEL_BYTES);
    epd_write_command(0x10);
    for (uint32_t i = 0; i < EPD_MAX_COLUMN_BYTES; i++) {
        for (uint32_t j = 0; j < EPD_MAX_LINE_BYTES; j++) {
            epd_write_data(fill);
        }
    }
    return epd_update_and_sleep();
}

static esp_err_t epd_fill_white(void)
{
    return epd_fill_both(0x55);
}

static esp_err_t epd_display_image(const uint8_t *image)
{
    if (image == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_LOGI(TAG, "display image (%lu bytes)", (unsigned long)EPD_FULL_BYTES);

    /* Master (left) */
    ESP_LOGI(TAG, "write master (left) panel");
    epd_write_command_cs(0x10, EPD_CS_MASTER);
    for (uint32_t i = 0; i < EPD_MAX_COLUMN_BYTES; i++) {
        for (uint32_t j = 0; j < EPD_MAX_LINE_BYTES; j++) {
            uint8_t temp = image[i * EPD_MAX_LINE_BYTES + j];
            uint8_t out = (uint8_t)((color_get(temp >> 6) << 6) |
                                    (color_get(temp >> 4) << 4) |
                                    (color_get(temp >> 2) << 2) |
                                    color_get(temp));
            epd_write_data_cs(out, EPD_CS_MASTER);
        }
    }

    /* Slave (right) */
    ESP_LOGI(TAG, "write slave (right) panel");
    epd_write_command_cs(0x10, EPD_CS_SLAVE);
    for (uint32_t i = 0; i < EPD_MAX_COLUMN_BYTES; i++) {
        for (uint32_t j = 0; j < EPD_MAX_LINE_BYTES; j++) {
            uint8_t temp = image[i * EPD_MAX_LINE_BYTES + EPD_PANEL_BYTES + j];
            uint8_t out = (uint8_t)((color_get(temp >> 6) << 6) |
                                    (color_get(temp >> 4) << 4) |
                                    (color_get(temp >> 2) << 2) |
                                    color_get(temp));
            epd_write_data_cs(out, EPD_CS_SLAVE);
        }
    }

    return epd_update_and_sleep();
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
    .init = epd_init,
    .display_image = epd_display_image,
    .fill_white = epd_fill_white,
    .display_partial = NULL,   /* color panel does not support partial refresh */
};

#endif /* CONFIG_EPD_PANEL_QYEG1085_COLOR */
