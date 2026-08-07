#include "epd/ctrl/jd79686.h"

#include "epd/io.h"

#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "jd79686";

/*
 * Dual-IC layout (see also dual_lr + product header):
 *
 *        physical X: 0 -------- 679 | 680 -------- 1359
 *                     [ Master CS ] | [  Slave CS2 ]
 *        local X:     0 -------- 679 | 0   --------  679
 *
 * Full frame: each row is interleaved [Master | Slave].
 * Partial: map screen (X,Y,W,H) via epd_dual_lr_split_region.
 * BUSY is on Master only — always prep both ICs, then broadcast 0x12.
 */

/* JD79686 refresh sequence (0x04 / 0x12 / 0x02 / 0x07 0xA5). Family-local. */
static esp_err_t jd79686_power_on(void)
{
    ESP_LOGI(TAG, "power on (BUSY before=%d)", gpio_get_level(EPD_PIN_BUSY));
    epd_write_command(0x04);
    /* Vendor reference: allow power rails to start before BUSY. */
    epd_delay_ms(200);
    return epd_wait_busy();
}

static esp_err_t jd79686_power_off_deep_sleep(void)
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

static esp_err_t jd79686_refresh_and_sleep(void)
{
    esp_err_t err = jd79686_power_on();
    if (err != ESP_OK) {
        return err;
    }
    ESP_LOGI(TAG, "refresh display");
    epd_write_command(0x12);
    err = epd_wait_busy();
    if (err != ESP_OK) {
        return err;
    }
    err = jd79686_power_off_deep_sleep();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "update complete");
    }
    return err;
}

static esp_err_t jd79686_update_and_sleep(void)
{
    /* Slave needs power params immediately before broadcast power-on. */
    epd_write_command_cs(0x01, EPD_CS_SLAVE);
    epd_write_data_cs(0x00, EPD_CS_SLAVE);
    epd_write_data_cs(0x00, EPD_CS_SLAVE);
    epd_write_data_cs(0x3F, EPD_CS_SLAVE);
    epd_write_data_cs(0x3F, EPD_CS_SLAVE);
    epd_write_data_cs(0x28, EPD_CS_SLAVE);
    return jd79686_refresh_and_sleep();
}

esp_err_t jd79686_init(const jd79686_cfg_t *cfg)
{
    if (cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "init start (%ux%u)%s",
             cfg->layout.total_w, cfg->layout.height,
             EPD_MASTER_ONLY_TEST ? " [MASTER only test]" : "");

    epd_io_reset(100, 100);

    esp_err_t err = epd_wait_busy();
    if (err != ESP_OK) {
        return err;
    }

    /* FITI command sequence from the 2024-09-26 vendor reference. */
    epd_write_command(0x4D);
    epd_write_data(0x55);
    epd_write_command(0xA6);
    epd_write_data(0x38);
    epd_write_command(0xB4);
    epd_write_data(0x5D);
    epd_write_command(0xB6);
    epd_write_data(0x80);
    epd_write_command(0xB7);
    epd_write_data(0x00);
    epd_write_command(0xF7);
    epd_write_data(0x02);

    epd_write_command_cs(0xAE, EPD_CS_SLAVE);
    epd_write_data_cs(0xA0, EPD_CS_SLAVE);

    epd_write_command(0xE0);
    epd_write_data(0x01);

    epd_write_command(0x00); /* Panel setting: B/W, OTP LUT */
    epd_write_data(0x9F);
    epd_write_data(0x0D);

    epd_write_command(0x06); /* Booster Setting */
    epd_write_data(0x57);
    epd_write_data(0x24);
    epd_write_data(0x28);
    epd_write_data(0x32);
    epd_write_data(0x08);
    epd_write_data(0x48);

    epd_write_command(0x30); /* OSC Setting */
    epd_write_data(0x3C);

    epd_write_command(0x50); /* VCOM and DATA interval setting */
    epd_write_data(0x47);

    epd_write_command(0x60); /* TCON setting */
    epd_write_data(0x31);

    uint16_t panel_w = cfg->layout.panel_w;
    uint16_t height = cfg->layout.height;
    epd_write_command(0x61); /* Resolution setting */
    epd_write_data((uint8_t)(panel_w / 256));
    epd_write_data((uint8_t)(panel_w % 256));
    epd_write_data((uint8_t)(height / 256));
    epd_write_data((uint8_t)(height % 256));

    epd_write_command(0x62); /* Source and gate start setting */
    epd_write_data(0x00);
    epd_write_data(0x00);
    epd_write_data(0x00);
    epd_write_data(0x00);

    epd_write_command(0xE8);
    epd_write_data(0x01);

    ESP_LOGI(TAG, "init done");
    return ESP_OK;
}

esp_err_t jd79686_display_image(const jd79686_cfg_t *cfg, const uint8_t *image)
{
    if (cfg == NULL || image == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t ic_bytes = epd_dual_lr_ic_row_bytes(&cfg->layout);
    uint32_t panel_bytes = (uint32_t)cfg->layout.height * ic_bytes;
    uint32_t full_bytes = panel_bytes * 2;
    ESP_LOGI(TAG, "display image (%lu bytes)", (unsigned long)full_bytes);

    /* Master (left): old RAM cleared, new RAM = interleaved left half */
    ESP_LOGI(TAG, "write master (left) panel");
    epd_write_command_cs(0x10, EPD_CS_MASTER);
    for (uint32_t i = 0; i < full_bytes; i++) {
        epd_write_data_cs(0x00, EPD_CS_MASTER);
    }
    epd_write_command_cs(0x13, EPD_CS_MASTER);
    for (uint16_t row = 0; row < cfg->layout.height; row++) {
        const uint8_t *src = epd_dual_lr_ic_row_ptr(&cfg->layout, image, row, EPD_CS_MASTER);
        for (uint16_t i = 0; i < ic_bytes; i++) {
            epd_write_data_cs(src[i], EPD_CS_MASTER);
        }
    }

#if !EPD_MASTER_ONLY_TEST
    ESP_LOGI(TAG, "write slave (right) panel");
    epd_write_command_cs(0x10, EPD_CS_SLAVE);
    for (uint32_t i = 0; i < full_bytes; i++) {
        epd_write_data_cs(0x00, EPD_CS_SLAVE);
    }
    epd_write_command_cs(0x13, EPD_CS_SLAVE);
    for (uint16_t row = 0; row < cfg->layout.height; row++) {
        const uint8_t *src = epd_dual_lr_ic_row_ptr(&cfg->layout, image, row, EPD_CS_SLAVE);
        for (uint16_t i = 0; i < ic_bytes; i++) {
            epd_write_data_cs(src[i], EPD_CS_SLAVE);
        }
    }
#else
    ESP_LOGI(TAG, "MASTER only test: slave RAM write skipped");
#endif

    return jd79686_update_and_sleep();
}

esp_err_t jd79686_fill_white(const jd79686_cfg_t *cfg)
{
    if (cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t ic_bytes = epd_dual_lr_ic_row_bytes(&cfg->layout);
    uint32_t full_bytes = (uint32_t)cfg->layout.height * ic_bytes * 2;
    ESP_LOGI(TAG, "fill white");
    epd_write_command(0x10);
    for (uint32_t i = 0; i < full_bytes; i++) {
        epd_write_data(0x00);
    }
    epd_write_command(0x13);
    for (uint32_t i = 0; i < full_bytes; i++) {
        epd_write_data(0x00);
    }
    return jd79686_update_and_sleep();
}

#if !EPD_MASTER_ONLY_TEST
/*
 * Partial-refresh tables from the 2024-09-26 vendor reference. The reference
 * sends 60 VCOM bytes but only initializes 42; explicitly zero-fill the
 * reserved tail here instead of reproducing its out-of-bounds read.
 */
static const uint8_t partial_params[] = {
    0x02, 0x00, 0x3F, 0x3F, 0xA0, 0x00, 0x00, 0x00,
    0x3C, 0x3C, 0x3C, 0x3C, 0x3C, 0x3C,
};

static const uint8_t lut_vcom_p[60] = {
    0x01, 0x19, 0x00, 0x00, 0x00, 0x01, 0x01,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01,
};

static const uint8_t lut_ww_p[42] = {
    0x01, 0x19, 0x00, 0x00, 0x00, 0x01, 0x01,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01,
};

static const uint8_t lut_bw_p[42] = {
    0x01, 0x99, 0x00, 0x00, 0x00, 0x01, 0x01,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01,
};

static const uint8_t lut_wb_p[42] = {
    0x01, 0x59, 0x00, 0x00, 0x00, 0x01, 0x01,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01,
};

static const uint8_t lut_bb_p[42] = {
    0x01, 0x19, 0x00, 0x00, 0x00, 0x01, 0x01,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01,
};

static void download_partial_lut(void)
{
    epd_write_command(0x82);
    epd_write_data(partial_params[0]);

    epd_write_command(0x01);
    epd_write_data(0x03);
    for (size_t i = 1; i <= 4; i++) {
        epd_write_data(partial_params[i]);
    }

    epd_write_command(0x2A);
    for (size_t i = 5; i <= 7; i++) {
        epd_write_data(partial_params[i]);
    }

    epd_write_command(0x31);
    epd_write_data(0x00);
    epd_write_command(0x32);
    for (size_t i = 8; i <= 13; i++) {
        epd_write_data(partial_params[i]);
    }

    epd_write_command(0x20);
    for (size_t i = 0; i < sizeof(lut_vcom_p); i++) {
        epd_write_data(lut_vcom_p[i]);
    }
    epd_write_command(0x21);
    for (size_t i = 0; i < sizeof(lut_ww_p); i++) {
        epd_write_data(lut_ww_p[i]);
    }
    epd_write_command(0x22);
    for (size_t i = 0; i < sizeof(lut_bw_p); i++) {
        epd_write_data(lut_bw_p[i]);
    }
    epd_write_command(0x23);
    for (size_t i = 0; i < sizeof(lut_wb_p); i++) {
        epd_write_data(lut_wb_p[i]);
    }
    epd_write_command(0x24);
    for (size_t i = 0; i < sizeof(lut_bb_p); i++) {
        epd_write_data(lut_bb_p[i]);
    }
}

static esp_err_t jd79686_partial_init(const jd79686_cfg_t *cfg)
{
    ESP_LOGI(TAG, "partial mode init start");
    epd_io_reset(100, 100);

    esp_err_t err = epd_wait_busy();
    if (err != ESP_OK) {
        return err;
    }

    epd_write_command(0x4D);
    epd_write_data(0x55);
    epd_write_command(0xA6);
    epd_write_data(0x38);
    epd_write_command(0xB4);
    epd_write_data(0x5D);
    epd_write_command(0xB6);
    epd_write_data(0x80);
    epd_write_command(0xB7);
    epd_write_data(0x00);
    epd_write_command(0xF7);
    epd_write_data(0x02);

    epd_write_command_cs(0xAE, EPD_CS_SLAVE);
    epd_write_data_cs(0xA0, EPD_CS_SLAVE);

    epd_write_command(0xE0);
    epd_write_data(0x01);

    epd_write_command(0x00); /* B/W, register LUT */
    epd_write_data(0xBF);
    epd_write_data(0x0D);

    epd_write_command(0x06);
    epd_write_data(0x57);
    epd_write_data(0x24);
    epd_write_data(0x28);
    epd_write_data(0x32);
    epd_write_data(0x08);
    epd_write_data(0x48);

    epd_write_command(0x50);
    epd_write_data(0x47);

    epd_write_command(0x60);
    epd_write_data(0x31);

    uint16_t panel_w = cfg->layout.panel_w;
    uint16_t height = cfg->layout.height;
    epd_write_command(0x61);
    epd_write_data((uint8_t)(panel_w / 256));
    epd_write_data((uint8_t)(panel_w % 256));
    epd_write_data((uint8_t)(height / 256));
    epd_write_data((uint8_t)(height % 256));

    epd_write_command(0x62);
    epd_write_data(0x00);
    epd_write_data(0x00);
    epd_write_data(0x00);
    epd_write_data(0x00);

    epd_write_command(0xE8);
    epd_write_data(0x01);

    download_partial_lut();
    ESP_LOGI(TAG, "partial mode init done");
    return ESP_OK;
}

/* R90H: xH,xL,xEndH,xEndL,yH,yL,yEndH,yEndL,PT_SCAN=0x00 (local pixels). */
static void set_partial_window(const epd_dual_lr_subregion_t *s)
{
    uint16_t x_end = (uint16_t)(s->px + s->pw - 1);
    uint16_t y_end = (uint16_t)(s->py + s->ph - 1);

    epd_write_command_cs(0x91, s->cs);
    epd_write_command_cs(0x90, s->cs);
    epd_write_data_cs((uint8_t)(s->px / 256), s->cs);
    epd_write_data_cs((uint8_t)(s->px % 256), s->cs);
    epd_write_data_cs((uint8_t)(x_end / 256), s->cs);
    epd_write_data_cs((uint8_t)(x_end % 256), s->cs);
    epd_write_data_cs((uint8_t)(s->py / 256), s->cs);
    epd_write_data_cs((uint8_t)(s->py % 256), s->cs);
    epd_write_data_cs((uint8_t)(y_end / 256), s->cs);
    epd_write_data_cs((uint8_t)(y_end % 256), s->cs);
    epd_write_data_cs(0x00, s->cs);

    ESP_LOGI(TAG, "%s 0x90 local x=%u..%u y=%u..%u",
             s->cs == EPD_CS_MASTER ? "Master" : "Slave",
             s->px, x_end, s->py, y_end);
}

static void write_subregion_plane(uint8_t cmd, const epd_dual_lr_subregion_t *s,
                                  uint16_t stride, const uint8_t *plane)
{
    epd_write_command_cs(cmd, s->cs);
    uint16_t bytes_per_row = s->pw / 8;
    for (uint16_t ry = 0; ry < s->ph; ry++) {
        uint32_t row = (uint32_t)ry * stride + s->col0_bytes;
        for (uint16_t bx = 0; bx < bytes_per_row; bx++) {
            epd_write_data_cs(plane[row + bx], s->cs);
        }
    }
}

/*
 * BUSY is wired to Master only. Always program both ICs (real window or
 * differential no-op), then broadcast 0x12.
 */
static void write_idle_noop(const jd79686_cfg_t *cfg, uint8_t cs, uint16_t py)
{
    epd_dual_lr_subregion_t idle = {
        .cs = cs,
        .px = 0,
        .py = py,
        .pw = 8,
        .ph = 8,
        .col0_bytes = 0,
    };
    if ((uint32_t)idle.py + idle.ph > cfg->layout.height) {
        idle.py = (uint16_t)(cfg->layout.height - idle.ph);
    }
    set_partial_window(&idle);
    static const uint8_t z[8] = {0};
    write_subregion_plane(0x10, &idle, 1, z);
    write_subregion_plane(0x13, &idle, 1, z);
}

esp_err_t jd79686_display_partial(const jd79686_cfg_t *cfg,
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
            (uint32_t)r->x + r->width > cfg->layout.total_w ||
            (uint32_t)r->y + r->height > cfg->layout.height) {
            ESP_LOGE(TAG, "region %u invalid (%u,%u %ux%u stride=%u)",
                     (unsigned)i, r->x, r->y, r->width, r->height, r->stride);
            return ESP_ERR_INVALID_ARG;
        }
    }

    /*
     * 0x90 holds only one window per IC, so multiple regions cannot share one
     * powered 0x12 burst; re-init + power cycle between regions is required.
     */
    ESP_LOGI(TAG, "partial refresh: %u region(s)", (unsigned)count);
    for (size_t i = 0; i < count; i++) {
        const epd_region_t *r = &regions[i];
        esp_err_t err = jd79686_partial_init(cfg);
        if (err != ESP_OK) {
            return err;
        }

        epd_dual_lr_subregion_t subs[2];
        int n = epd_dual_lr_split_region(&cfg->layout, r->x, r->y,
                                         r->width, r->height, subs);
        const epd_dual_lr_subregion_t *master_sub = NULL;
        const epd_dual_lr_subregion_t *slave_sub = NULL;
        for (int s = 0; s < n; s++) {
            if (subs[s].cs == EPD_CS_MASTER) {
                master_sub = &subs[s];
            } else {
                slave_sub = &subs[s];
            }
        }

        if (master_sub != NULL) {
            set_partial_window(master_sub);
            write_subregion_plane(0x10, master_sub, r->stride, r->old_data);
            write_subregion_plane(0x13, master_sub, r->stride, r->new_data);
            ESP_LOGI(TAG, "region %u -> Master local=(%u,%u) %ux%u (phys x=%u)",
                     (unsigned)i, master_sub->px, master_sub->py,
                     master_sub->pw, master_sub->ph, master_sub->px);
        } else {
            write_idle_noop(cfg, EPD_CS_MASTER, r->y);
        }
        if (slave_sub != NULL) {
            set_partial_window(slave_sub);
            write_subregion_plane(0x10, slave_sub, r->stride, r->old_data);
            write_subregion_plane(0x13, slave_sub, r->stride, r->new_data);
            ESP_LOGI(TAG, "region %u -> Slave local=(%u,%u) %ux%u (phys x=%u)",
                     (unsigned)i, slave_sub->px, slave_sub->py,
                     slave_sub->pw, slave_sub->ph,
                     (unsigned)(slave_sub->px + cfg->layout.panel_w));
        } else {
            write_idle_noop(cfg, EPD_CS_SLAVE, r->y);
        }

        size_t plane_bytes = (size_t)r->stride * r->height;
        size_t changed = 0;
        for (size_t b = 0; b < plane_bytes; b++) {
            if (r->old_data[b] != r->new_data[b]) {
                changed++;
            }
        }
        ESP_LOGI(TAG, "region %u screen=(%u,%u) %ux%u changed_bytes=%u/%u",
                 (unsigned)i, r->x, r->y, r->width, r->height,
                 (unsigned)changed, (unsigned)plane_bytes);

        err = jd79686_update_and_sleep();
        if (err != ESP_OK) {
            return err;
        }
    }

    ESP_LOGI(TAG, "partial refresh complete");
    return ESP_OK;
}
#endif /* !EPD_MASTER_ONLY_TEST */
