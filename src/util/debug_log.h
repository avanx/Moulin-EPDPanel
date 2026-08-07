#pragma once

#include "esp_log.h"

#ifndef EPD_DEBUG
#define EPD_DEBUG 0
#endif

#if EPD_DEBUG
#define EPD_LOGD(tag, fmt, ...) ESP_LOGI(tag, fmt, ##__VA_ARGS__)
#else
#define EPD_LOGD(tag, fmt, ...) do { (void)(tag); } while (0)
#endif

#define EPD_LOGI(tag, fmt, ...) ESP_LOGI(tag, fmt, ##__VA_ARGS__)
#define EPD_LOGW(tag, fmt, ...) ESP_LOGW(tag, fmt, ##__VA_ARGS__)
#define EPD_LOGE(tag, fmt, ...) ESP_LOGE(tag, fmt, ##__VA_ARGS__)
