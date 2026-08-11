#include "epd/ctrl/ssd1675a.h"

#include "epd/io.h"

#include "esp_log.h"

static const char *TAG = "ssd1675a";

#define SSD1675A_LUT_SIZE 70

/*
 * SSD1675A 70-byte LUT — GxEPD2_213_B72 / Adafruit SSD1675 /
 * MSP430Etag epd_hink0213a07.
 */
static const uint8_t LUT_FULL_UPDATE[SSD1675A_LUT_SIZE] = {
    0x80, 0x60, 0x40, 0x00, 0x00, 0x00, 0x00,
    0x10, 0x60, 0x20, 0x00, 0x00, 0x00, 0x00,
    0x80, 0x60, 0x40, 0x00, 0x00, 0x00, 0x00,
    0x10, 0x60, 0x20, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x03, 0x03, 0x00, 0x00, 0x02,
    0x09, 0x09, 0x00, 0x00, 0x02,
    0x03, 0x03, 0x00, 0x00, 0x02,
    0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00,
};

static const uint8_t LUT_PARTIAL_UPDATE[SSD1675A_LUT_SIZE] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x0A, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00,
};

/* BUSY high = busy, low = idle (same as IL3895 / SSD1619). */
static esp_err_t ssd1675a_wait_busy(void)
{
    return epd_wait_busy_idle(0);
}

static void ssd1675a_cmd(uint8_t cmd)
{
    epd_write_command_cs(cmd, EPD_CS_MASTER);
}

static void ssd1675a_data(uint8_t data)
{
    epd_write_data_cs(data, EPD_CS_MASTER);
}

static void ssd1675a_data_buf(const uint8_t *data, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        ssd1675a_data(data[i]);
    }
}

/* Epd::SetMemoryArea */
static void ssd1675a_set_memory_area(uint16_t x_start, uint16_t y_start,
                                     uint16_t x_end, uint16_t y_end)
{
    ssd1675a_cmd(0x44); /* SET_RAM_X_ADDRESS_START_END_POSITION */
    ssd1675a_data((uint8_t)((x_start >> 3) & 0xFF));
    ssd1675a_data((uint8_t)((x_end >> 3) & 0xFF));
    ssd1675a_cmd(0x45); /* SET_RAM_Y_ADDRESS_START_END_POSITION */
    ssd1675a_data((uint8_t)(y_start & 0xFF));
    ssd1675a_data((uint8_t)(y_start >> 8));
    ssd1675a_data((uint8_t)(y_end & 0xFF));
    ssd1675a_data((uint8_t)(y_end >> 8));
}

/* Epd::SetMemoryPointer */
static void ssd1675a_set_memory_pointer(uint16_t x, uint16_t y)
{
    ssd1675a_cmd(0x4E); /* SET_RAM_X_ADDRESS_COUNTER */
    ssd1675a_data((uint8_t)((x >> 3) & 0xFF));
    ssd1675a_cmd(0x4F); /* SET_RAM_Y_ADDRESS_COUNTER */
    ssd1675a_data((uint8_t)(y & 0xFF));
    ssd1675a_data((uint8_t)(y >> 8));
}

/* Epd::SetLut — 70 bytes */
static void ssd1675a_set_lut(const uint8_t *lut)
{
    ssd1675a_cmd(0x32); /* WRITE_LUT_REGISTER */
    ssd1675a_data_buf(lut, SSD1675A_LUT_SIZE);
}

/* Epd::PowerOn — 0x22 0xC0 / 0x20 */
static esp_err_t ssd1675a_power_on(void)
{
    ssd1675a_cmd(0x22); /* DISPLAY_UPDATE_CONTROL_2 */
    ssd1675a_data(0xC0);
    ssd1675a_cmd(0x20); /* MASTER_ACTIVATION */
    return ssd1675a_wait_busy();
}

/* Epd::PowerOff — 0x22 0xC3 / 0x20 */
static esp_err_t ssd1675a_power_off(void)
{
    ssd1675a_cmd(0x22);
    ssd1675a_data(0xC3);
    ssd1675a_cmd(0x20);
    return ssd1675a_wait_busy();
}

/* Epd::DisplayFrame — 0x22 0xC4 / 0x20 (no 0xFF terminate). */
static esp_err_t ssd1675a_display_frame(void)
{
    ESP_LOGI(TAG, "DisplayFrame (0xC4)");
    ssd1675a_cmd(0x22);
    ssd1675a_data(0xC4);
    ssd1675a_cmd(0x20);
    return ssd1675a_wait_busy();
}

/* Epd::Sleep — PowerOff then deep sleep 0x10 / 0x01. */
static esp_err_t ssd1675a_sleep(void)
{
    esp_err_t err = ssd1675a_power_off();
    if (err != ESP_OK) {
        return err;
    }
    ssd1675a_cmd(0x10); /* DEEP_SLEEP_MODE */
    ssd1675a_data(0x01);
    return ESP_OK;
}

/*
 * Epd::Init — reset + panel regs + LUT + PowerOn.
 * Aligns with MSP430Etag epd_hink0213a07.cpp.
 */
static esp_err_t ssd1675a_hw_init(const ssd1675a_cfg_t *cfg, const uint8_t *lut)
{
    uint16_t mux = (uint16_t)(cfg->layout.height - 1);

    epd_io_reset(10, 10); /* Epd::Reset: 10 ms / 10 ms */
    esp_err_t err = ssd1675a_wait_busy();
    if (err != ESP_OK) {
        return err;
    }

    ssd1675a_cmd(0x74); /* SET_ANALOG_BLOCK_CONTROL */
    ssd1675a_data(0x54);
    ssd1675a_cmd(0x7E); /* SET_DIGITAL_BLOCK_CONTROL */
    ssd1675a_data(0x3B);

    ssd1675a_cmd(0x01); /* DRIVER_OUTPUT_CONTROL */
    ssd1675a_data((uint8_t)(mux & 0xFF));
    ssd1675a_data((uint8_t)(mux >> 8));
    ssd1675a_data(0x00);

    ssd1675a_cmd(0x3C); /* BORDER_WAVEFORM_CONTROL */
    ssd1675a_data(0x03);

    ssd1675a_cmd(0x2C); /* WRITE_VCOM_REGISTER */
    ssd1675a_data(0x70);

    ssd1675a_cmd(0x03); /* GATE_DRIVING_VOLTAGE_CONTROL */
    ssd1675a_data(0x15); /* 19V */

    ssd1675a_cmd(0x04); /* SOURCE_DRIVING_VOLTAGE_CONTROL */
    ssd1675a_data(0x41); /* VSH1 15V */
    ssd1675a_data(0xA8); /* VSH2 5V */
    ssd1675a_data(0x32); /* VSL -15V */

    ssd1675a_cmd(0x3A); /* SET_DUMMY_LINE_PERIOD */
    ssd1675a_data(0x30);
    ssd1675a_cmd(0x3B); /* SET_GATE_TIME */
    ssd1675a_data(0x0A);

    ssd1675a_cmd(0x11); /* DATA_ENTRY_MODE_SETTING */
    ssd1675a_data(0x03); /* X increment; Y increment */

    ssd1675a_set_memory_area(0, 0,
                             (uint16_t)(cfg->layout.width - 1),
                             (uint16_t)(cfg->layout.height - 1));
    ssd1675a_set_lut(lut);
    return ssd1675a_power_on();
}

/* Write full frame to RAM cmd (0x24 black / 0x26 red-previous). */
static esp_err_t ssd1675a_write_full(const ssd1675a_cfg_t *cfg,
                                     const uint8_t *image, uint8_t fill,
                                     uint8_t ram_cmd)
{
    uint16_t w = cfg->layout.width;
    uint16_t h = cfg->layout.height;
    uint16_t row_bytes = epd_single_row_bytes(&cfg->layout);

    ssd1675a_set_memory_area(0, 0, (uint16_t)(w - 1), (uint16_t)(h - 1));

    for (uint16_t y = 0; y < h; y++) {
        ssd1675a_set_memory_pointer(0, y);
        ssd1675a_cmd(ram_cmd);
        if (image != NULL) {
            const uint8_t *line = image + (uint32_t)y * row_bytes;
            for (uint16_t x = 0; x < row_bytes; x++) {
                ssd1675a_data(line[x]);
            }
        } else {
            for (uint16_t x = 0; x < row_bytes; x++) {
                ssd1675a_data(fill);
            }
        }
    }
    return ESP_OK;
}

static esp_err_t ssd1675a_write_window(uint16_t x, uint16_t y,
                                       uint16_t w, uint16_t h,
                                       uint16_t stride,
                                       const uint8_t *plane,
                                       uint8_t ram_cmd)
{
    uint16_t x_end = (uint16_t)(x + w - 1);
    uint16_t y_end = (uint16_t)(y + h - 1);
    uint16_t bytes_per_row = (uint16_t)(w / 8);

    ssd1675a_set_memory_area(x, y, x_end, y_end);

    for (uint16_t row = 0; row < h; row++) {
        ssd1675a_set_memory_pointer(x, (uint16_t)(y + row));
        ssd1675a_cmd(ram_cmd);
        const uint8_t *line = plane + (uint32_t)row * stride;
        for (uint16_t bx = 0; bx < bytes_per_row; bx++) {
            ssd1675a_data(line[bx]);
        }
    }
    return ESP_OK;
}

/* Dual-buffer commit: write 0x24 + DisplayFrame, twice, then Sleep. */
static esp_err_t ssd1675a_commit_twice(const ssd1675a_cfg_t *cfg,
                                       const uint8_t *image, uint8_t fill)
{
    for (int pass = 0; pass < 2; pass++) {
        esp_err_t err = ssd1675a_write_full(cfg, image, fill, 0x24);
        if (err != ESP_OK) {
            return err;
        }
        err = ssd1675a_display_frame();
        if (err != ESP_OK) {
            return err;
        }
    }
    return ssd1675a_sleep();
}

esp_err_t ssd1675a_init(const ssd1675a_cfg_t *cfg)
{
    if (cfg == NULL || cfg->layout.bpp != 1 ||
        cfg->layout.width == 0 || cfg->layout.height == 0 ||
        (cfg->layout.width % 8) != 0) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "init start (%ux%u, lut_full)",
             cfg->layout.width, cfg->layout.height);

    esp_err_t err = ssd1675a_hw_init(cfg, LUT_FULL_UPDATE);
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(TAG, "init done");
    return ESP_OK;
}

esp_err_t ssd1675a_display_image(const ssd1675a_cfg_t *cfg, const uint8_t *image)
{
    if (cfg == NULL || image == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ssd1675a_commit_twice(cfg, image, 0xFF);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "update complete");
    }
    return err;
}

esp_err_t ssd1675a_fill_white(const ssd1675a_cfg_t *cfg)
{
    if (cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ssd1675a_commit_twice(cfg, NULL, 0xFF);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "fill white complete");
    }
    return err;
}

esp_err_t ssd1675a_display_partial(const ssd1675a_cfg_t *cfg,
                                   const epd_region_t *regions,
                                   size_t count)
{
    if (cfg == NULL || regions == NULL || count == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    for (size_t i = 0; i < count; i++) {
        const epd_region_t *r = &regions[i];
        if (r->width == 0 || r->height == 0 || (r->x % 8) != 0 ||
            (r->width % 8) != 0 ||
            r->stride != (uint16_t)(r->width / 8) ||
            r->old_data == NULL || r->new_data == NULL ||
            (uint32_t)r->x + r->width > cfg->layout.width ||
            (uint32_t)r->y + r->height > cfg->layout.height) {
            ESP_LOGE(TAG, "region %u invalid (%u,%u %ux%u stride=%u)",
                     (unsigned)i, r->x, r->y, r->width, r->height, r->stride);
            return ESP_ERR_INVALID_ARG;
        }
    }

    ESP_LOGI(TAG, "partial refresh: %u region(s)", (unsigned)count);

    esp_err_t err = ssd1675a_hw_init(cfg, LUT_PARTIAL_UPDATE);
    if (err != ESP_OK) {
        return err;
    }

    for (size_t i = 0; i < count; i++) {
        const epd_region_t *r = &regions[i];

        /* New -> black RAM (0x24); old -> red/previous RAM (0x26). */
        err = ssd1675a_write_window(r->x, r->y, r->width, r->height,
                                    r->stride, r->new_data, 0x24);
        if (err != ESP_OK) {
            return err;
        }
        err = ssd1675a_write_window(r->x, r->y, r->width, r->height,
                                    r->stride, r->old_data, 0x26);
        if (err != ESP_OK) {
            return err;
        }
        err = ssd1675a_display_frame();
        if (err != ESP_OK) {
            return err;
        }

        ESP_LOGI(TAG, "region %u screen=(%u,%u) %ux%u done",
                 (unsigned)i, r->x, r->y, r->width, r->height);
    }

    err = ssd1675a_sleep();
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(TAG, "partial refresh complete");
    return ESP_OK;
}
