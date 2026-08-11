# Explicit source list (no GLOB). All panel drivers are always compiled;
# each driver's .c is guarded by `#if CONFIG_EPD_PANEL_*` (defined via
# platformio.ini build_flags), so a disabled driver compiles to an empty
# translation unit with no Flash/RAM cost.

set(app_sources
    main.c
    device/config.c
    device/control.c
    net/display_client.c
    net/provisioning_server.c
    net/wifi_manager.c
    epd/io.c
    epd/panel.c
    epd/layout/dual_lr.c
    epd/layout/single.c
    epd/ctrl/uc8579.c
    epd/ctrl/jd79686.c
    epd/ctrl/ssd1619.c
    epd/ctrl/il3895.c
    epd/ctrl/ssd1675a.c
    epd/drivers/qyeg1085ryu579.c
    epd/drivers/qyeg1085bnf86bf0.c
    epd/drivers/hink_e042a13_a0.c
    epd/drivers/hink_e0213a04.c
    epd/drivers/hink_e0213a07.c
)

idf_component_register(
    SRCS ${app_sources}
    INCLUDE_DIRS "."
    REQUIRES
        driver
        esp_event
        esp_http_client
        esp_http_server
        esp_netif
        esp_wifi
        json
        lwip
        mbedtls
        nvs_flash
)
