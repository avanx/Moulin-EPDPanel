#include "epd/drivers/qyeg1085bnf86bf0.h"

#if defined(CONFIG_EPD_PANEL_QYEG1085_MONO) && CONFIG_EPD_PANEL_QYEG1085_MONO

#include "epd/io.h"

#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "epd_mono";

/*s
 * Dual-IC layout (see also qyeg1085bnf86bf0.h):
 *
 *        physical X: 0 -------- 679 | 680 -------- 1359
 *                     [ Master CS ] | [  Slave CS2 ]
 *        local X:     0 -------- 679 | 0   --------  679
 *
 * Full frame: each row is 170 bytes = Master[0..84] then Slave[85..169].
 * Partial: map screen (X,Y,W,H) to IC-local windows; Slave local_x = X - 680.
 * Cross-seam regions must be split. 0x90 takes local coords + PT_SCAN=0x00.
 * BUSY is on Master only — always prep both ICs, then broadcast 0x12.
 */

static esp_err_t mono_init(void)
{
    ESP_LOGI(TAG, "panel init start (%dx%d)%s",
             EPD_BNF_WIDTH, EPD_BNF_HEIGHT,
             EPD_MASTER_ONLY_TEST ? " [MASTER only test]" : "");
    gpio_set_level(EPD_PIN_RST, 0);
    epd_delay_ms(100);
    gpio_set_level(EPD_PIN_RST, 1);
    epd_delay_ms(100);

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

    /* Slave-only FITI setting. */
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

    epd_write_command(0x61); /* Resolution setting */
    epd_write_data(0x02);
    epd_write_data(0xA8);    /* 680 */
    epd_write_data(0x01);
    epd_write_data(0xE0);    /* 480 */

    epd_write_command(0x62); /* Source and gate start setting */
    epd_write_data(0x00);
    epd_write_data(0x00);
    epd_write_data(0x00);
    epd_write_data(0x00);

    epd_write_command(0xE8);
    epd_write_data(0x01);

    ESP_LOGI(TAG, "panel init done");
    return ESP_OK;
}

/*
 * The slave controller needs its power parameters immediately before the
 * broadcast power-on command. This ordering is required by the vendor sample.
 */
static esp_err_t mono_update_and_sleep(void)
{
    epd_write_command_cs(0x01, EPD_CS_SLAVE);
    epd_write_data_cs(0x00, EPD_CS_SLAVE);
    epd_write_data_cs(0x00, EPD_CS_SLAVE);
    epd_write_data_cs(0x3F, EPD_CS_SLAVE);
    epd_write_data_cs(0x3F, EPD_CS_SLAVE);
    epd_write_data_cs(0x28, EPD_CS_SLAVE);
    return epd_update_and_sleep();
}

static esp_err_t mono_display_image(const uint8_t *image)
{
    if (image == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_LOGI(TAG, "display image (%lu bytes)", (unsigned long)EPD_BNF_FULL_BYTES);

    const uint32_t row_bytes = EPD_BNF_LINE_BYTES * 2; /* 170: 85 master + 85 slave */

    /* Master (left): old RAM cleared, new RAM = image rows [0..84] */
    ESP_LOGI(TAG, "write master (left) panel");
    epd_write_command_cs(0x10, EPD_CS_MASTER);
    for (uint32_t i = 0; i < EPD_BNF_FULL_BYTES; i++) {
        epd_write_data_cs(0x00, EPD_CS_MASTER);
    }
    epd_write_command_cs(0x13, EPD_CS_MASTER);
    for (uint32_t j = 0; j < EPD_BNF_HEIGHT; j++) {
        for (uint32_t i = 0; i < EPD_BNF_LINE_BYTES; i++) {
            epd_write_data_cs(image[j * row_bytes + i], EPD_CS_MASTER);
        }
    }

#if !EPD_MASTER_ONLY_TEST
    /* Slave (right): old RAM cleared, new RAM = image rows [85..169] */
    ESP_LOGI(TAG, "write slave (right) panel");
    epd_write_command_cs(0x10, EPD_CS_SLAVE);
    for (uint32_t i = 0; i < EPD_BNF_FULL_BYTES; i++) {
        epd_write_data_cs(0x00, EPD_CS_SLAVE);
    }
    epd_write_command_cs(0x13, EPD_CS_SLAVE);
    for (uint32_t j = 0; j < EPD_BNF_HEIGHT; j++) {
        for (uint32_t i = 0; i < EPD_BNF_LINE_BYTES; i++) {
            epd_write_data_cs(image[j * row_bytes + EPD_BNF_LINE_BYTES + i], EPD_CS_SLAVE);
        }
    }
#else
    ESP_LOGI(TAG, "MASTER only test: slave RAM write skipped");
#endif

    return mono_update_and_sleep();
}

static esp_err_t mono_fill_white(void)
{
    ESP_LOGI(TAG, "fill white");
    epd_write_command(0x10);
    for (uint32_t i = 0; i < EPD_BNF_FULL_BYTES; i++) {
        epd_write_data(0x00);
    }
    epd_write_command(0x13);
    for (uint32_t i = 0; i < EPD_BNF_FULL_BYTES; i++) {
        epd_write_data(0x00);
    }
    return mono_update_and_sleep();
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

static esp_err_t mono_partial_init(void)
{
    ESP_LOGI(TAG, "partial mode init start");
    gpio_set_level(EPD_PIN_RST, 0);
    epd_delay_ms(100);
    gpio_set_level(EPD_PIN_RST, 1);
    epd_delay_ms(100);

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

    epd_write_command(0x06); /* Booster Setting */
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

    epd_write_command(0x61);
    epd_write_data(0x02);
    epd_write_data(0xA8);
    epd_write_data(0x01);
    epd_write_data(0xE0);

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

/*
 * One slice of a full-screen region on a single IC, in that IC's local space.
 *
 * Example (vendor digit demo): screen digits straddling the seam
 *   Master local x=424 / 552  -> physical 424..679
 *   Slave  local x=0   / 128  -> physical 680..935
 * Slave right-edge content uses a large local x (e.g. screen 968 -> local 288).
 */
typedef struct {
    uint8_t cs;             /* EPD_CS_MASTER / EPD_CS_SLAVE */
    uint16_t px, py;        /* local window origin inside that IC */
    uint16_t pw, ph;        /* window size inside that IC */
    uint16_t col0_bytes;    /* first byte column within the region plane */
} subregion_t;

/*
 * Full-screen (X,Y,W,H) -> Master/Slave local windows:
 *   X+W <= 680          -> Master only  (local_x = X)
 *   X >= 680            -> Slave only   (local_x = X - 680)
 *   else                -> both; Slave starts at local 0, width X+W-680
 */
static int split_region(const epd_region_t *r, subregion_t out[2])
{
    const uint32_t panel_w = EPD_BNF_PANEL_WIDTH;   /* 680 */
    const uint32_t right = (uint32_t)r->x + r->width;
    int n = 0;

    if ((uint32_t)r->x < panel_w) {
        out[n].cs = EPD_CS_MASTER;
        out[n].px = r->x;
        out[n].py = r->y;
        out[n].ph = r->height;
        out[n].pw = (uint16_t)(right > panel_w ? panel_w - r->x : r->width);
        out[n].col0_bytes = 0;
        n++;
    }
    if (right > panel_w) {
        uint32_t start = (uint32_t)r->x > panel_w ? (uint32_t)r->x : panel_w;
        out[n].cs = EPD_CS_SLAVE;
        out[n].px = (uint16_t)(start - panel_w);
        out[n].py = r->y;
        out[n].ph = r->height;
        out[n].pw = (uint16_t)(right - start);
        out[n].col0_bytes = (uint16_t)((start - (uint32_t)r->x) / 8);
        n++;
    }
    return n;
}

/* R90H: xH,xL,xEndH,xEndL,yH,yL,yEndH,yEndL,PT_SCAN=0x00 (local pixels). */
static void set_partial_window(const subregion_t *s)
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

/* Vendor writes count=width*height/8 as a flat stream into the 0x90 window. */
static void write_subregion_plane(uint8_t cmd, const subregion_t *s,
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
 * differential no-op), then broadcast 0x12 — same as main.c digit updates.
 */
static void write_idle_noop(uint8_t cs, uint16_t py)
{
    subregion_t idle = {
        .cs = cs,
        .px = 0,
        .py = py,
        .pw = 8,
        .ph = 8,
        .col0_bytes = 0,
    };
    if ((uint32_t)idle.py + idle.ph > EPD_BNF_HEIGHT) {
        idle.py = (uint16_t)(EPD_BNF_HEIGHT - idle.ph);
    }
    set_partial_window(&idle);
    static const uint8_t z[8] = {0};
    write_subregion_plane(0x10, &idle, 1, z);
    write_subregion_plane(0x13, &idle, 1, z);
}

static esp_err_t mono_display_partial(const epd_region_t *regions, size_t count)
{
    if (regions == NULL || count == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    for (size_t i = 0; i < count; i++) {
        const epd_region_t *r = &regions[i];
        if (r->width == 0 || r->height == 0 || (r->x % 8) != 0 ||
            (r->width % 8) != 0 ||
            r->stride != (uint16_t)(r->width / 8) ||
            r->old_data == NULL || r->new_data == NULL ||
            (uint32_t)r->x + r->width > EPD_BNF_WIDTH ||
            (uint32_t)r->y + r->height > EPD_BNF_HEIGHT) {
            ESP_LOGE(TAG, "region %u invalid (%u,%u %ux%u stride=%u)",
                     (unsigned)i, r->x, r->y, r->width, r->height, r->stride);
            return ESP_ERR_INVALID_ARG;
        }
    }

    /*
     *   EPD_Partial_Init -> write Master + Slave windows -> EPD_Update_And_DeepSleep
     * 0x90 holds only one window per IC, so multiple regions cannot share one
     * powered 0x12 burst; re-init + power cycle between regions is required.
     */
    ESP_LOGI(TAG, "partial refresh: %u region(s)", (unsigned)count);
    for (size_t i = 0; i < count; i++) {
        const epd_region_t *r = &regions[i];
        esp_err_t err = mono_partial_init();
        if (err != ESP_OK) {
            return err;
        }

        subregion_t subs[2];
        int n = split_region(r, subs);
        const subregion_t *master_sub = NULL;
        const subregion_t *slave_sub = NULL;
        for (int s = 0; s < n; s++) {
            if (subs[s].cs == EPD_CS_MASTER) {
                master_sub = &subs[s];
            } else {
                slave_sub = &subs[s];
            }
        }

        /* Vendor always programs Master before Slave. */
        if (master_sub != NULL) {
            set_partial_window(master_sub);
            write_subregion_plane(0x10, master_sub, r->stride, r->old_data);
            write_subregion_plane(0x13, master_sub, r->stride, r->new_data);
            ESP_LOGI(TAG, "region %u -> Master local=(%u,%u) %ux%u (phys x=%u)",
                     (unsigned)i, master_sub->px, master_sub->py,
                     master_sub->pw, master_sub->ph, master_sub->px);
        } else {
            write_idle_noop(EPD_CS_MASTER, r->y);
        }
        if (slave_sub != NULL) {
            set_partial_window(slave_sub);
            write_subregion_plane(0x10, slave_sub, r->stride, r->old_data);
            write_subregion_plane(0x13, slave_sub, r->stride, r->new_data);
            ESP_LOGI(TAG, "region %u -> Slave local=(%u,%u) %ux%u (phys x=%u)",
                     (unsigned)i, slave_sub->px, slave_sub->py,
                     slave_sub->pw, slave_sub->ph,
                     (unsigned)(slave_sub->px + EPD_BNF_PANEL_WIDTH));
        } else {
            write_idle_noop(EPD_CS_SLAVE, r->y);
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

        /* Broadcast 0x12 so Master BUSY tracks the full ~570 ms waveform. */
        err = mono_update_and_sleep();
        if (err != ESP_OK) {
            return err;
        }
    }

    ESP_LOGI(TAG, "partial refresh complete");
    return ESP_OK;
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
    .init = mono_init,
    .display_image = mono_display_image,
    .fill_white = mono_fill_white,
#if EPD_MASTER_ONLY_TEST
    .display_partial = NULL,
#else
    .display_partial = mono_display_partial,
#endif
};

#endif /* CONFIG_EPD_PANEL_QYEG1085_MONO */
