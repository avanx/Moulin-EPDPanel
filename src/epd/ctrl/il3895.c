#include "epd/ctrl/il3895.h"

#include "epd/io.h"

#include "esp_log.h"

static const char *TAG = "il3895";

/*
 * Waveshare epd2in13 (reference/epd2in13) — IL3895 / 2.13" V1.
 * LUT length is 30 bytes (Epd::SetLut).
 */
static const uint8_t LUT_FULL_UPDATE[] = {
    0x22, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x11,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static const uint8_t LUT_PARTIAL_UPDATE[] = {
    0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x0F, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

/* BUSY high = busy, low = idle (Epd::WaitUntilIdle). */
static esp_err_t il3895_wait_busy(void)
{
    return epd_wait_busy_idle(0);
}

static void il3895_cmd(uint8_t cmd)
{
    epd_write_command_cs(cmd, EPD_CS_MASTER);
}

static void il3895_data(uint8_t data)
{
    epd_write_data_cs(data, EPD_CS_MASTER);
}

static void il3895_data_buf(const uint8_t *data, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        il3895_data(data[i]);
    }
}

static uint32_t il3895_frame_bytes(const il3895_cfg_t *cfg)
{
    return (uint32_t)epd_single_row_bytes(&cfg->layout) * cfg->layout.height;
}

/* Epd::SetMemoryArea */
static void il3895_set_memory_area(uint16_t x_start, uint16_t y_start,
                                   uint16_t x_end, uint16_t y_end)
{
    il3895_cmd(0x44); /* SET_RAM_X_ADDRESS_START_END_POSITION */
    il3895_data((uint8_t)((x_start >> 3) & 0xFF));
    il3895_data((uint8_t)((x_end >> 3) & 0xFF));
    il3895_cmd(0x45); /* SET_RAM_Y_ADDRESS_START_END_POSITION */
    il3895_data((uint8_t)(y_start & 0xFF));
    il3895_data((uint8_t)(y_start >> 8));
    il3895_data((uint8_t)(y_end & 0xFF));
    il3895_data((uint8_t)(y_end >> 8));
}

/* Epd::SetMemoryPointer */
static esp_err_t il3895_set_memory_pointer(uint16_t x, uint16_t y)
{
    il3895_cmd(0x4E); /* SET_RAM_X_ADDRESS_COUNTER */
    il3895_data((uint8_t)((x >> 3) & 0xFF));
    il3895_cmd(0x4F); /* SET_RAM_Y_ADDRESS_COUNTER */
    il3895_data((uint8_t)(y & 0xFF));
    il3895_data((uint8_t)(y >> 8));
    return il3895_wait_busy();
}

/* Epd::SetLut — 30 bytes */
static void il3895_set_lut(const uint8_t *lut)
{
    il3895_cmd(0x32); /* WRITE_LUT_REGISTER */
    il3895_data_buf(lut, 30);
}

/*
 * Epd::DisplayFrame — always 0xC4 (full and partial LUT modes).
 * Order: 0x22 0xC4 / 0x20 / 0xFF / WaitUntilIdle.
 */
static esp_err_t il3895_display_frame(void)
{
    ESP_LOGI(TAG, "DisplayFrame (0xC4)");
    il3895_cmd(0x22); /* DISPLAY_UPDATE_CONTROL_2 */
    il3895_data(0xC4);
    il3895_cmd(0x20); /* MASTER_ACTIVATION */
    il3895_cmd(0xFF); /* TERMINATE_FRAME_READ_WRITE */
    return il3895_wait_busy();
}

/* Epd::Sleep — command 0x10 only, then wait (no data byte). */
static esp_err_t il3895_sleep(void)
{
    il3895_cmd(0x10); /* DEEP_SLEEP_MODE */
    return il3895_wait_busy();
}

/*
 * Epd::Init — reset + panel regs + LUT.
 * No separate PowerOn; DisplayFrame 0xC4 enables the update sequence.
 */
static esp_err_t il3895_hw_init(const il3895_cfg_t *cfg, const uint8_t *lut)
{
    uint16_t mux = (uint16_t)(cfg->layout.height - 1);

    epd_io_reset(200, 200); /* Epd::Reset: 200 ms / 200 ms */

    il3895_cmd(0x01); /* DRIVER_OUTPUT_CONTROL */
    il3895_data((uint8_t)(mux & 0xFF));
    il3895_data((uint8_t)(mux >> 8));
    il3895_data(0x00); /* GD = 0; SM = 0; TB = 0 */

    il3895_cmd(0x0C); /* BOOSTER_SOFT_START_CONTROL */
    il3895_data(0xD7);
    il3895_data(0xD6);
    il3895_data(0x9D);

    il3895_cmd(0x2C); /* WRITE_VCOM_REGISTER */
    il3895_data(0xA8);

    il3895_cmd(0x3A); /* SET_DUMMY_LINE_PERIOD */
    il3895_data(0x1A);

    il3895_cmd(0x3B); /* SET_GATE_TIME */
    il3895_data(0x08);

    il3895_cmd(0x11); /* DATA_ENTRY_MODE_SETTING */
    il3895_data(0x03); /* X increment; Y increment */

    il3895_set_lut(lut);
    return ESP_OK;
}

/* Epd::ClearFrameMemory / SetFrameMemory(full) — line-by-line WRITE_RAM. */
static esp_err_t il3895_write_full(const il3895_cfg_t *cfg,
                                   const uint8_t *image, uint8_t fill)
{
    uint16_t w = cfg->layout.width;
    uint16_t h = cfg->layout.height;
    uint16_t row_bytes = epd_single_row_bytes(&cfg->layout);

    il3895_set_memory_area(0, 0, (uint16_t)(w - 1), (uint16_t)(h - 1));

    for (uint16_t y = 0; y < h; y++) {
        esp_err_t err = il3895_set_memory_pointer(0, y);
        if (err != ESP_OK) {
            return err;
        }
        il3895_cmd(0x24); /* WRITE_RAM */
        if (image != NULL) {
            const uint8_t *line = image + (uint32_t)y * row_bytes;
            for (uint16_t x = 0; x < row_bytes; x++) {
                il3895_data(line[x]);
            }
        } else {
            for (uint16_t x = 0; x < row_bytes; x++) {
                il3895_data(fill);
            }
        }
    }
    return ESP_OK;
}

/* Epd::SetFrameMemory(buf, x, y, w, h) */
static esp_err_t il3895_write_window(uint16_t x, uint16_t y,
                                     uint16_t w, uint16_t h,
                                     uint16_t stride,
                                     const uint8_t *plane)
{
    /* x / width must be multiples of 8 (caller already validates). */
    uint16_t x_end = (uint16_t)(x + w - 1);
    uint16_t y_end = (uint16_t)(y + h - 1);
    uint16_t bytes_per_row = (uint16_t)(w / 8);

    il3895_set_memory_area(x, y, x_end, y_end);

    for (uint16_t row = 0; row < h; row++) {
        esp_err_t err = il3895_set_memory_pointer(x, (uint16_t)(y + row));
        if (err != ESP_OK) {
            return err;
        }
        il3895_cmd(0x24);
        const uint8_t *line = plane + (uint32_t)row * stride;
        for (uint16_t bx = 0; bx < bytes_per_row; bx++) {
            il3895_data(line[bx]);
        }
    }
    return ESP_OK;
}

/*
 * Demo note: after DisplayFrame the next SetFrameMemory targets the other
 * RAM, so a full image must be written and refreshed twice.
 */
static esp_err_t il3895_commit_twice(const il3895_cfg_t *cfg,
                                     const uint8_t *image, uint8_t fill)
{
    for (int pass = 0; pass < 2; pass++) {
        esp_err_t err = il3895_write_full(cfg, image, fill);
        if (err != ESP_OK) {
            return err;
        }
        err = il3895_display_frame();
        if (err != ESP_OK) {
            return err;
        }
    }
    return il3895_sleep();
}

esp_err_t il3895_init(const il3895_cfg_t *cfg)
{
    if (cfg == NULL || cfg->layout.bpp != 1 ||
        cfg->layout.width == 0 || cfg->layout.height == 0 ||
        (cfg->layout.width % 8) != 0) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "init start (%ux%u, epd2in13 lut_full)",
             cfg->layout.width, cfg->layout.height);

    esp_err_t err = il3895_hw_init(cfg, LUT_FULL_UPDATE);
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(TAG, "init done");
    return ESP_OK;
}

esp_err_t il3895_display_image(const il3895_cfg_t *cfg, const uint8_t *image)
{
    if (cfg == NULL || image == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = il3895_commit_twice(cfg, image, 0xFF);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "update complete");
    }
    return err;
}

esp_err_t il3895_fill_white(const il3895_cfg_t *cfg)
{
    if (cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* ClearFrameMemory(0xFF) + DisplayFrame, twice, then Sleep. */
    esp_err_t err = il3895_commit_twice(cfg, NULL, 0xFF);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "fill white complete");
    }
    return err;
}

esp_err_t il3895_display_partial(const il3895_cfg_t *cfg,
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

    for (size_t i = 0; i < count; i++) {
        const epd_region_t *r = &regions[i];

        /* Epd::Init(lut_partial_update) */
        esp_err_t err = il3895_hw_init(cfg, LUT_PARTIAL_UPDATE);
        if (err != ESP_OK) {
            return err;
        }

        /*
         * After Sleep both RAMs are undefined. Prime with old (write+0xC4
         * twice), then apply new the same way — same rule as epd2in13.ino
         * IMAGE_DATA block.
         */
        for (int pass = 0; pass < 2; pass++) {
            err = il3895_write_window(r->x, r->y, r->width, r->height,
                                      r->stride, r->old_data);
            if (err != ESP_OK) {
                return err;
            }
            err = il3895_display_frame();
            if (err != ESP_OK) {
                return err;
            }
        }
        for (int pass = 0; pass < 2; pass++) {
            err = il3895_write_window(r->x, r->y, r->width, r->height,
                                      r->stride, r->new_data);
            if (err != ESP_OK) {
                return err;
            }
            err = il3895_display_frame();
            if (err != ESP_OK) {
                return err;
            }
        }

        err = il3895_sleep();
        if (err != ESP_OK) {
            return err;
        }

        ESP_LOGI(TAG, "region %u screen=(%u,%u) %ux%u done",
                 (unsigned)i, r->x, r->y, r->width, r->height);
    }

    ESP_LOGI(TAG, "partial refresh complete");
    return ESP_OK;
}
