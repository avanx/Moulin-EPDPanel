#include "epd/ctrl/ssd1619.h"

#include "epd/io.h"

#include "esp_log.h"

static const char *TAG = "ssd1619";

/* SSD1619: BUSY high = busy, low = idle (opposite of UC8579/JD79686). */
static esp_err_t ssd1619_wait_busy(void)
{
    return epd_wait_busy_idle(0);
}

static void ssd1619_cmd(uint8_t cmd)
{
    epd_write_command_cs(cmd, EPD_CS_MASTER);
}

static void ssd1619_data(uint8_t data)
{
    epd_write_data_cs(data, EPD_CS_MASTER);
}

static uint32_t ssd1619_plane_bytes(const ssd1619_cfg_t *cfg)
{
    return (uint32_t)epd_single_row_bytes(&cfg->layout) * cfg->layout.height;
}

/* GDEH042Z96 / SSD1619 display update (0x22 0xC7 / 0x20) + deep sleep. */
static esp_err_t ssd1619_update_and_sleep(void)
{
    ESP_LOGI(TAG, "refresh display");
    ssd1619_cmd(0x22);
    ssd1619_data(0xC7);
    ssd1619_cmd(0x20);
    esp_err_t err = ssd1619_wait_busy();
    if (err != ESP_OK) {
        return err;
    }

    ssd1619_cmd(0x10);
    ssd1619_data(0x01);
    epd_delay_ms(100);
    ESP_LOGI(TAG, "update complete");
    return ESP_OK;
}

esp_err_t ssd1619_init(const ssd1619_cfg_t *cfg)
{
    if (cfg == NULL || cfg->layout.bpp != 1 ||
        cfg->layout.width == 0 || cfg->layout.height == 0 ||
        (cfg->layout.width % 8) != 0) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "init start (%ux%u BWR)",
             cfg->layout.width, cfg->layout.height);

    /* Vendor comment: at least 10 ms; Arduino demo used 1 ms. */
    epd_io_reset(10, 10);

    esp_err_t err = ssd1619_wait_busy();
    if (err != ESP_OK) {
        return err;
    }

    ssd1619_cmd(0x12); /* SWRESET */
    err = ssd1619_wait_busy();
    if (err != ESP_OK) {
        return err;
    }

    /* Sequence from reference/GDEH042Z96_Arduino.ino (HINK-E042A13-A0). */
    ssd1619_cmd(0x74);
    ssd1619_data(0x54);
    ssd1619_cmd(0x7E);
    ssd1619_data(0x3B);

    ssd1619_cmd(0x2B); /* Reduce glitch under ACVCOM */
    ssd1619_data(0x04);
    ssd1619_data(0x63);

    ssd1619_cmd(0x0C); /* Soft start setting */
    ssd1619_data(0x8B);
    ssd1619_data(0x9C);
    ssd1619_data(0x96);
    ssd1619_data(0x0F);

    /* Set MUX = height - 1 (300 -> 0x012B) */
    uint16_t mux = (uint16_t)(cfg->layout.height - 1);
    ssd1619_cmd(0x01);
    ssd1619_data((uint8_t)(mux & 0xFF));
    ssd1619_data((uint8_t)(mux >> 8));
    ssd1619_data(0x00);

    /* Data entry mode: X increment, Y increment. Frame row 0 (top) goes to
     * RAM Y=0. The vendor demo used 0x01 (Y decrement from mux), which
     * mirrors epd-bwr-plane-v1 frames top-to-bottom. */
    ssd1619_cmd(0x11);
    ssd1619_data(0x03);

    /* RAM X: 0 .. (width/8 - 1) */
    uint8_t x_end = (uint8_t)(cfg->layout.width / 8 - 1);
    ssd1619_cmd(0x44);
    ssd1619_data(0x00);
    ssd1619_data(x_end);

    /* RAM Y: 0 .. mux */
    ssd1619_cmd(0x45);
    ssd1619_data(0x00);
    ssd1619_data(0x00);
    ssd1619_data((uint8_t)(mux & 0xFF));
    ssd1619_data((uint8_t)(mux >> 8));

    ssd1619_cmd(0x3C); /* Border */
    ssd1619_data(0x01); /* HiZ */

    ssd1619_cmd(0x18);
    ssd1619_data(0x80);
    ssd1619_cmd(0x22);
    ssd1619_data(0xB1); /* Load temperature and waveform */
    ssd1619_cmd(0x20);
    err = ssd1619_wait_busy();
    if (err != ESP_OK) {
        return err;
    }

    ssd1619_cmd(0x4E);
    ssd1619_data(0x00);
    ssd1619_cmd(0x4F);
    ssd1619_data(0x00);
    ssd1619_data(0x00);

    ESP_LOGI(TAG, "init done");
    return ESP_OK;
}

esp_err_t ssd1619_display_image(const ssd1619_cfg_t *cfg, const uint8_t *image)
{
    if (cfg == NULL || image == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint32_t plane = ssd1619_plane_bytes(cfg);
    const uint8_t *bw = image;
    const uint8_t *red = image + plane;

    ssd1619_cmd(0x24); /* BW RAM: 0=black, 1=white */
    for (uint32_t i = 0; i < plane; i++) {
        ssd1619_data(bw[i]);
    }

    ssd1619_cmd(0x26); /* Red RAM: 1=red, 0=no red (same as epd-bwr-plane-v1) */
    for (uint32_t i = 0; i < plane; i++) {
        ssd1619_data(red[i]);
    }

    return ssd1619_update_and_sleep();
}

/* Uniform fill: bw_byte to RAM 0x24, red plane left blank (0x00 = no red). */
static esp_err_t ssd1619_fill(const ssd1619_cfg_t *cfg, uint8_t bw_byte)
{
    if (cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint32_t plane = ssd1619_plane_bytes(cfg);

    ssd1619_cmd(0x24);
    for (uint32_t i = 0; i < plane; i++) {
        ssd1619_data(bw_byte);
    }

    ssd1619_cmd(0x26);
    for (uint32_t i = 0; i < plane; i++) {
        ssd1619_data(0x00);
    }

    return ssd1619_update_and_sleep();
}

esp_err_t ssd1619_fill_white(const ssd1619_cfg_t *cfg)
{
    return ssd1619_fill(cfg, 0xFF);
}

esp_err_t ssd1619_fill_black(const ssd1619_cfg_t *cfg)
{
    return ssd1619_fill(cfg, 0x00);
}
