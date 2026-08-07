#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Unified panel interface.
 *
 * Each compiled-in panel driver defines one `epd_panel_ops_t` instance
 * carrying its capabilities (for manifest validation) and its operations
 * (for the upper layers). Panels are registered in epd/panel.c; which
 * panels get compiled is chosen in menuconfig (EPD Panel Drivers).
 *
 * `caps` fields let display_client validate an incoming manifest against
 * the active panel without any hard-coded dimensions/encoding, so panels
 * with different resolution, color depth or encoding are all supported.
 */

/*
 * A partial-refresh region. The server ships each region file as two
 * concatenated planes [old][new] (epd-bw-msb-v1, 1bpp, MSB-first), each
 * stride*height bytes; display_client splits them and hands the pointers
 * here. (x, y) is the region's top-left in full-screen coordinates; the
 * panel driver maps that onto its physical panel layout. width is always
 * a multiple of 8.
 */
typedef struct {
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
    uint16_t stride;          /* bytes per row == width / 8 */
    const uint8_t *old_data;  /* baseline plane, stride*height bytes */
    const uint8_t *new_data;  /* target plane, stride*height bytes */
} epd_region_t;

typedef struct {
    /* caps ----------------------------------------------------------------- */
    const char *type;           /* stable id, e.g. "qyeg1085_color"; persisted in NVS */
    const char *label;          /* web dropdown: "型号 · 颜色说明 · 分辨率", e.g. "QYEG1085RYU579 · 彩色 B/W/R/Y · 1360×480" */
    uint16_t width;             /* total pixels across all sub-panels */
    uint16_t height;
    size_t frame_bytes;         /* expected image buffer size */
    const char *color_mode;     /* manifest color_mode, e.g. "bwry" / "bw" */
    const char *encoding;       /* manifest encoding */
    const char *panel_layout;   /* manifest panel_layout, e.g. "dual-panel-left-right" */
    const char *update_type;    /* manifest update_type, e.g. "full" */

    /* ops ------------------------------------------------------------------ */
    esp_err_t (*io_init)(void);
    esp_err_t (*init)(void);
    esp_err_t (*display_image)(const uint8_t *image);
    esp_err_t (*fill_white)(void);
    /*
     * Optional partial refresh. Applies every region in order, writing each
     * region's old plane to the controller's previous-data buffer and its new
     * plane to the new-data buffer, then triggering a partial refresh of the
     * affected windows. NULL when the panel does not support partial refresh
     * (e.g. color panels); callers must fall back to display_image (full).
     */
    esp_err_t (*display_partial)(const epd_region_t *regions, size_t count);
} epd_panel_ops_t;

/* Registered (compiled-in) panels. Returns count and sets *out to the array. */
size_t epd_panel_list(const epd_panel_ops_t * const **out);

/*
 * Select the active panel by type string and cache it.
 * NULL/empty/unknown type -> first registered panel (boot-safe default).
 */
const epd_panel_ops_t *epd_panel_select(const char *type);

/* The panel cached by epd_panel_select(); never NULL once a panel is compiled. */
const epd_panel_ops_t *epd_panel_current(void);

#ifdef __cplusplus
}
#endif
