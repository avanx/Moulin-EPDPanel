#include "epd/panel.h"

#include <string.h>

#include "esp_log.h"

static const char *TAG = "epd_panel";

/* Resolve each Kconfig bool into a 0/1 macro regardless of whether the
 * symbol is defined (disabled bools are undefined in sdkconfig.h). */
#if defined(CONFIG_EPD_PANEL_QYEG1085_COLOR) && CONFIG_EPD_PANEL_QYEG1085_COLOR
#define EPD_HAS_COLOR 1
#else
#define EPD_HAS_COLOR 0
#endif
#if defined(CONFIG_EPD_PANEL_QYEG1085_MONO) && CONFIG_EPD_PANEL_QYEG1085_MONO
#define EPD_HAS_MONO 1
#else
#define EPD_HAS_MONO 0
#endif
#if defined(CONFIG_EPD_PANEL_HINK_E042A13_A0) && CONFIG_EPD_PANEL_HINK_E042A13_A0
#define EPD_HAS_HINK042 1
#else
#define EPD_HAS_HINK042 0
#endif
#if defined(CONFIG_EPD_PANEL_HINK_E0213A04) && CONFIG_EPD_PANEL_HINK_E0213A04
#define EPD_HAS_HINK213 1
#else
#define EPD_HAS_HINK213 0
#endif
#if defined(CONFIG_EPD_PANEL_HINK_E0213A07) && CONFIG_EPD_PANEL_HINK_E0213A07
#define EPD_HAS_HINK213A07 1
#else
#define EPD_HAS_HINK213A07 0
#endif

#define EPD_PANEL_COUNT (EPD_HAS_COLOR + EPD_HAS_MONO + EPD_HAS_HINK042 + \
                         EPD_HAS_HINK213 + EPD_HAS_HINK213A07)

#if EPD_PANEL_COUNT == 0
#error "No EPD panel driver enabled. Enable at least one under EPD Panel Drivers in menuconfig."
#endif

#if EPD_HAS_COLOR
extern const epd_panel_ops_t color_panel;
#endif
#if EPD_HAS_MONO
extern const epd_panel_ops_t mono_panel;
#endif
#if EPD_HAS_HINK042
extern const epd_panel_ops_t hink_e042a13_a0_panel;
#endif
#if EPD_HAS_HINK213
extern const epd_panel_ops_t hink_e0213a04_panel;
#endif
#if EPD_HAS_HINK213A07
extern const epd_panel_ops_t hink_e0213a07_panel;
#endif

static const epd_panel_ops_t *const s_registry[] = {
#if EPD_HAS_COLOR
    &color_panel,
#endif
#if EPD_HAS_MONO
    &mono_panel,
#endif
#if EPD_HAS_HINK042
    &hink_e042a13_a0_panel,
#endif
#if EPD_HAS_HINK213
    &hink_e0213a04_panel,
#endif
#if EPD_HAS_HINK213A07
    &hink_e0213a07_panel,
#endif
};

#define S_REGISTRY_LEN (sizeof(s_registry) / sizeof(s_registry[0]))

static const epd_panel_ops_t *s_current;

size_t epd_panel_list(const epd_panel_ops_t *const **out)
{
    *out = s_registry;
    return S_REGISTRY_LEN;
}

const epd_panel_ops_t *epd_panel_select(const char *type)
{
    if (type != NULL && type[0] != '\0') {
        for (size_t i = 0; i < S_REGISTRY_LEN; i++) {
            if (strcmp(type, s_registry[i]->type) == 0) {
                s_current = s_registry[i];
                ESP_LOGI(TAG, "selected panel '%s' (%s)", s_current->type, s_current->label);
                return s_current;
            }
        }
        ESP_LOGW(TAG, "panel type '%s' not registered; falling back to default", type);
    }
    s_current = s_registry[0];
    ESP_LOGI(TAG, "selected default panel '%s' (%s)", s_current->type, s_current->label);
    return s_current;
}

const epd_panel_ops_t *epd_panel_current(void)
{
    if (s_current == NULL) {
        s_current = s_registry[0];
    }
    return s_current;
}
