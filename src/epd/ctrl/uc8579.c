#include "epd/ctrl/uc8579.h"

#include "epd/io.h"

#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "uc8579";

/* UC8579 refresh sequence (0x04 / 0x12 / 0x02 / 0x07 0xA5). Family-local. */
static esp_err_t uc8579_power_on(void)
{
    ESP_LOGI(TAG, "power on (BUSY before=%d)", gpio_get_level(EPD_PIN_BUSY));
    epd_write_command(0x04);
    epd_delay_ms(200);
    return epd_wait_busy();
}

static esp_err_t uc8579_power_off_deep_sleep(void)
{
    epd_write_command(0x02);
    epd_delay_ms(60);
    esp_err_t err = epd_wait_busy();
    if (err != ESP_OK) {
        return err;
    }
    epd_write_command(0x07);
    epd_write_data(0xA5);
    epd_delay_ms(60);
    return ESP_OK;
}

static esp_err_t uc8579_update_and_sleep(void)
{
    esp_err_t err = uc8579_power_on();
    if (err != ESP_OK) {
        return err;
    }
    ESP_LOGI(TAG, "refresh display");
    epd_write_command(0x12);
    err = epd_wait_busy();
    if (err != ESP_OK) {
        return err;
    }
    err = uc8579_power_off_deep_sleep();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "update complete");
    }
    return err;
}

esp_err_t uc8579_init(const uc8579_cfg_t *cfg)
{
    if (cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "init start (%ux%u dual-lr)",
             cfg->layout.total_w, cfg->layout.height);

    epd_delay_ms(100);
    epd_io_reset(20, 50);
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
    epd_write_data_cs(cfg->x_addr_h, EPD_CS_MASTER);
    epd_write_data_cs(cfg->x_addr_l, EPD_CS_MASTER);
    epd_write_data_cs(cfg->y_addr_h, EPD_CS_MASTER);
    epd_write_data_cs(cfg->y_addr_l, EPD_CS_MASTER);

    epd_write_command(0x65);
    epd_write_data_cs(0x00, EPD_CS_MASTER);
    epd_write_data_cs(0x00, EPD_CS_MASTER);
    epd_write_data_cs(0x00, EPD_CS_MASTER);
    epd_write_data_cs(0x00, EPD_CS_MASTER);

    /* SLAVE */
    epd_write_command_cs(0x61, EPD_CS_SLAVE);
    epd_write_data_cs(cfg->x_addr_h, EPD_CS_SLAVE);
    epd_write_data_cs(cfg->x_addr_l, EPD_CS_SLAVE);
    epd_write_data_cs(cfg->y_addr_h, EPD_CS_SLAVE);
    epd_write_data_cs(cfg->y_addr_l, EPD_CS_SLAVE);

    epd_write_command(0x65);
    epd_write_data_cs(0x00, EPD_CS_SLAVE);
    epd_write_data_cs(0x00, EPD_CS_SLAVE);
    epd_write_data_cs(0x00, EPD_CS_SLAVE);
    epd_write_data_cs(0x00, EPD_CS_SLAVE);

    ESP_LOGI(TAG, "init done");
    return ESP_OK;
}

static void write_ic_rows(const uc8579_cfg_t *cfg, uint8_t cs,
                          const uint8_t *image, uc8579_byte_remap_fn remap)
{
    uint16_t ic_bytes = epd_dual_lr_ic_row_bytes(&cfg->layout);
    epd_write_command_cs(0x10, cs);
    for (uint16_t row = 0; row < cfg->layout.height; row++) {
        const uint8_t *src = epd_dual_lr_ic_row_ptr(&cfg->layout, image, row, cs);
        for (uint16_t j = 0; j < ic_bytes; j++) {
            uint8_t b = src[j];
            epd_write_data_cs(remap ? remap(b) : b, cs);
        }
    }
}

esp_err_t uc8579_fill(const uc8579_cfg_t *cfg, uint8_t fill_byte)
{
    if (cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t ic_bytes = epd_dual_lr_ic_row_bytes(&cfg->layout);
    uint32_t panel_bytes = (uint32_t)cfg->layout.height * ic_bytes;
    ESP_LOGI(TAG, "fill data=0x%02X (%lu bytes x2 via dual CS)",
             fill_byte, (unsigned long)panel_bytes);

    epd_write_command(0x10);
    for (uint16_t row = 0; row < cfg->layout.height; row++) {
        for (uint16_t j = 0; j < ic_bytes; j++) {
            epd_write_data(fill_byte);
        }
    }
    return uc8579_update_and_sleep();
}

esp_err_t uc8579_display_image(const uc8579_cfg_t *cfg,
                               const uint8_t *image,
                               uc8579_byte_remap_fn remap)
{
    if (cfg == NULL || image == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t ic_bytes = epd_dual_lr_ic_row_bytes(&cfg->layout);
    uint32_t full_bytes = (uint32_t)cfg->layout.height * ic_bytes * 2;
    ESP_LOGI(TAG, "display image (%lu bytes)", (unsigned long)full_bytes);

    ESP_LOGI(TAG, "write master (left) panel");
    write_ic_rows(cfg, EPD_CS_MASTER, image, remap);

    ESP_LOGI(TAG, "write slave (right) panel");
    write_ic_rows(cfg, EPD_CS_SLAVE, image, remap);

    return uc8579_update_and_sleep();
}
