#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Unified panel interface (L3 Product).
 *
 * Driver stack (progressive framework):
 *
 *   L3 Product  — this header / epd/panel.c registry / epd/drivers
 *                 epd_panel_ops_t caps + ops; upper layers only see this.
 *   L2 Layout   — epd/layout (dual_lr, single)
 *                 logical full-screen <-> IC-local geometry & frame packing.
 *   L1 Ctrl     — epd/ctrl (uc8579, jd79686)
 *                 controller-family init / RAM load / refresh / sleep.
 *   L0 Bus      — epd/io
 *                 bit-bang SPI, CS routing, BUSY, reset (no refresh opcodes).
 *
 * Each compiled-in panel driver defines one `epd_panel_ops_t` instance
 * carrying its capabilities (for manifest validation) and its operations
 * (for the upper layers). Panels are registered in epd/panel.c; which
 * panels get compiled is chosen via platformio.ini build_flags.
 *
 * Partial-refresh capability matrix (callers use display_partial != NULL):
 *
 *   Panel                  Layout              Partial  Region notes
 *   ---------------------  ------------------  -------  ----------------------------
 *   qyeg1085_color         dual-left-right     no       full refresh only
 *   qyeg1085_mono          dual-left-right     yes*     epd-bw-msb-v1; width multiple of 8;
 *                                                       cross-seam regions split
 *                                                       (*disabled if EPD_MASTER_ONLY_TEST)
 *   hink_e042a13_a0        single              no       BWR epd-bwr-plane-v1; full only
 *   hink_e0213a04          single              yes      BW epd-bw-msb-v1; width multiple of 8
 *   hink_e0213a07          single              yes      BW epd-bw-msb-v1; SSD1675A; width ×8
 *
 * Cross-IC partial: layout/dual_lr splits at panel_w; idle no-op on the
 * unused IC so Master BUSY still tracks a broadcast 0x12.
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
