#include "epd/io.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "epd_io";

static inline void epd_cs_all_high(void)
{
    gpio_set_level(EPD_PIN_CS, 1);
    gpio_set_level(EPD_PIN_CS2, 1);
}

void epd_delay_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

esp_err_t epd_io_init(void)
{
    ESP_LOGI(TAG, "IO init: BUSY=%d RST=%d DC=%d CS=%d CS2=%d CLK=%d MOSI=%d",
             EPD_PIN_BUSY, EPD_PIN_RST, EPD_PIN_DC, EPD_PIN_CS,
             EPD_PIN_CS2, EPD_PIN_CLK, EPD_PIN_MOSI);

    gpio_config_t out = {
        .pin_bit_mask = (1ULL << EPD_PIN_CS) | (1ULL << EPD_PIN_CS2) |
                        (1ULL << EPD_PIN_CLK) | (1ULL << EPD_PIN_MOSI) |
                        (1ULL << EPD_PIN_DC) | (1ULL << EPD_PIN_RST),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&out);

    gpio_config_t in = {
        .pin_bit_mask = (1ULL << EPD_PIN_BUSY),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&in);

    epd_cs_all_high();
    gpio_set_level(EPD_PIN_CLK, 0);
    gpio_set_level(EPD_PIN_MOSI, 0);
    gpio_set_level(EPD_PIN_DC, 1);
    gpio_set_level(EPD_PIN_RST, 1);
    ESP_LOGI(TAG, "IO init done, BUSY=%d, mode=%s",
             gpio_get_level(EPD_PIN_BUSY),
             EPD_MASTER_ONLY_TEST ? "MASTER only" : "dual CS");
    return ESP_OK;
}

void epd_io_reset(uint32_t low_ms, uint32_t high_ms)
{
    gpio_set_level(EPD_PIN_RST, 0);
    epd_delay_ms(low_ms);
    gpio_set_level(EPD_PIN_RST, 1);
    epd_delay_ms(high_ms);
}

static inline void epd_spi_write_byte(uint8_t tx)
{
    gpio_set_level(EPD_PIN_CLK, 0);
    for (int i = 0; i < 8; i++) {
        gpio_set_level(EPD_PIN_MOSI, (tx & 0x80) ? 1 : 0);
        gpio_set_level(EPD_PIN_CLK, 1);
        esp_rom_delay_us(1);
        gpio_set_level(EPD_PIN_CLK, 0);
        tx <<= 1;
    }
}

esp_err_t epd_wait_busy(void)
{
    /* BUSY low = busy, high = idle */
    int waited_ms = 0;
    while (gpio_get_level(EPD_PIN_BUSY) == 0) {
        vTaskDelay(pdMS_TO_TICKS(10));
        waited_ms += 10;
        if (waited_ms > 0 && (waited_ms % 1000) == 0) {
            ESP_LOGW(TAG, "waiting BUSY... %d ms", waited_ms);
        }
        if (waited_ms >= 60000) {
            ESP_LOGE(TAG, "BUSY timeout after %d ms", waited_ms);
            return ESP_ERR_TIMEOUT;
        }
    }
    ESP_LOGI(TAG, "BUSY released after %d ms", waited_ms);
    return ESP_OK;
}

void epd_write_command(uint8_t cmd)
{
    epd_cs_all_high();
    gpio_set_level(EPD_PIN_CS, 0);
#if !EPD_MASTER_ONLY_TEST
    gpio_set_level(EPD_PIN_CS2, 0);
#endif
    gpio_set_level(EPD_PIN_DC, 0);
    epd_spi_write_byte(cmd);
    epd_cs_all_high();
}

void epd_write_data(uint8_t data)
{
    epd_cs_all_high();
    gpio_set_level(EPD_PIN_CS, 0);
#if !EPD_MASTER_ONLY_TEST
    gpio_set_level(EPD_PIN_CS2, 0);
#endif
    gpio_set_level(EPD_PIN_DC, 1);
    epd_spi_write_byte(data);
    epd_cs_all_high();
}

void epd_write_command_cs(uint8_t cmd, uint8_t cs_num)
{
    epd_cs_all_high();
    if (cs_num == EPD_CS_MASTER) {
        gpio_set_level(EPD_PIN_CS, 0);
    } else {
        gpio_set_level(EPD_PIN_CS2, 0);
    }
    gpio_set_level(EPD_PIN_DC, 0);
    epd_spi_write_byte(cmd);
    epd_cs_all_high();
}

void epd_write_data_cs(uint8_t data, uint8_t cs_num)
{
    epd_cs_all_high();
    if (cs_num == EPD_CS_MASTER) {
        gpio_set_level(EPD_PIN_CS, 0);
    } else {
        gpio_set_level(EPD_PIN_CS2, 0);
    }
    gpio_set_level(EPD_PIN_DC, 1);
    epd_spi_write_byte(data);
    epd_cs_all_high();
}
