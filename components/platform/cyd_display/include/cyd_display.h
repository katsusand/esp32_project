#ifndef CYD_DISPLAY_H
#define CYD_DISPLAY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CYD_DISPLAY_GRID_COLS 40
#define CYD_DISPLAY_GRID_ROWS 30
#define CYD_DISPLAY_GRID_CELL_PX 8
#define CYD_DISPLAY_MAX_STATUS_LINES 8
#define CYD_DISPLAY_MAX_MODE_BUTTONS 6
#define CYD_DISPLAY_MAX_WIDGETS 48
#define CYD_DISPLAY_TEXT_MAX_LEN 40
#define CYD_DISPLAY_LOG_MAX_LINES 32

typedef struct {
    uint8_t col;
    uint8_t row;
    uint8_t width;
    uint8_t height;
} cyd_display_grid_rect_t;

typedef enum {
    CYD_DISPLAY_WIDGET_NONE = 0,
    CYD_DISPLAY_WIDGET_TEXT,
    CYD_DISPLAY_WIDGET_BUTTON,
    CYD_DISPLAY_WIDGET_ICON,
    CYD_DISPLAY_WIDGET_RECT,
    CYD_DISPLAY_WIDGET_BAR,
    CYD_DISPLAY_WIDGET_SPARKLINE,
} cyd_display_widget_type_t;

typedef enum {
    CYD_DISPLAY_ALIGN_LEFT = 0,
    CYD_DISPLAY_ALIGN_CENTER,
    CYD_DISPLAY_ALIGN_RIGHT,
} cyd_display_align_t;

/*
 * RGB565 pixel block, referenced by an ICON widget.
 *
 * English contract, and it is load-bearing: neither this struct nor `data` is
 * copied when a screen is submitted. A screen is queued to the display task by
 * value, and the pixels are read later, on that other task. Both pointers must
 * therefore stay valid until the frame has been rendered.
 *
 * In practice that means `static const` in flash, or a heap buffer owned for at
 * least the app's lifetime. A buffer local to the function that builds the
 * screen is a use-after-free: submit() returns long before the pixels are read.
 *
 * `data` holds width_px * height_px RGB565 pixels. The element type is
 * uint16_t on purpose: LovyanGFX picks the source format from the pointer type,
 * and a uint8_t buffer would be interpreted as RGB332 instead.
 */
typedef struct {
    const uint16_t *data;
    uint8_t width_px;
    uint8_t height_px;
} cyd_display_bitmap_t;

typedef struct {
    bool filled;
    uint8_t radius;
} cyd_display_rect_style_t;

typedef struct {
    int16_t value;
    int16_t min_value;
    int16_t max_value;
    bool vertical;
} cyd_display_bar_t;

/*
 * Time-series polyline drawn at pixel resolution inside its grid box.
 *
 * English contract, both rules matter:
 *  - `samples` is NOT copied. A screen is queued to the display task by value,
 *    so the array must outlive the submit; app-owned static storage is the
 *    intended pattern. Concurrent writes only produce a partially updated
 *    frame, which is acceptable for trend graphs, so no lock is required.
 *  - `revision` must be incremented whenever the sample contents change. The
 *    dirty-rect diff compares widgets by value and cannot see through the
 *    pointer, so a graph with a static revision would never be redrawn.
 */
typedef struct {
    const int16_t *samples;
    uint16_t count;
    uint16_t revision;
    int16_t min_value;
    int16_t max_value;
    bool fill;
    bool has_baseline;
    int16_t baseline_value;
    uint16_t baseline_color;
    /*
     * Optional dropout marker. Samples equal to `gap_value` are not plotted and
     * break the polyline instead of being drawn as a real reading. This keeps
     * the time axis honest when a source has outages: the gap stays visible as
     * a gap rather than silently compressing the graph.
     */
    bool has_gap_value;
    int16_t gap_value;
} cyd_display_sparkline_t;

typedef struct {
    cyd_display_widget_type_t type;
    uint8_t col;
    uint8_t row;
    uint8_t span_cols;
    uint8_t span_rows;
    cyd_display_align_t align;
    uint8_t scale_x;
    uint8_t scale_y;
    uint16_t fg_color;
    uint16_t bg_color;
    uint16_t border_color;
    uint16_t action_id;
    bool enabled;
    const cyd_display_bitmap_t *bitmap;
    /*
     * Payload is per-type and mutually exclusive. Keeping it in an anonymous
     * union is what stops new widget types from growing all
     * CYD_DISPLAY_MAX_WIDGETS entries of every screen buffer.
     */
    union {
        char text[CYD_DISPLAY_TEXT_MAX_LEN + 1];
        cyd_display_rect_style_t rect;
        cyd_display_bar_t bar;
        cyd_display_sparkline_t sparkline;
    };
} cyd_display_widget_t;

typedef struct {
    uint8_t widget_count;
    cyd_display_widget_t widgets[CYD_DISPLAY_MAX_WIDGETS];
} cyd_display_screen_t;

esp_err_t cyd_display_init(void);
esp_err_t cyd_display_set_brightness(uint8_t brightness);
esp_err_t cyd_display_save_brightness(void);
uint8_t cyd_display_get_brightness(void);
esp_err_t cyd_display_claim_owner(void);
esp_err_t cyd_display_release_owner(void);
esp_err_t cyd_display_submit_screen(const cyd_display_screen_t *screen);
esp_err_t cyd_display_show_boot_screen(void);
esp_err_t cyd_display_show_text(const char *title, const char *message);
esp_err_t cyd_display_show_lines(const char *title, const char *const *lines, size_t line_count);
esp_err_t cyd_display_show_mode_screen(const char *title,
                                       const char *const *lines,
                                       size_t line_count,
                                       const char *const *buttons,
                                       size_t button_count,
                                       size_t selected_idx);
esp_err_t cyd_display_log_show(const char *title);
esp_err_t cyd_display_log_hide(void);
esp_err_t cyd_display_log_clear(void);
esp_err_t cyd_display_log_push(const char *line);
esp_err_t cyd_display_log_scroll(int delta);
esp_err_t cyd_display_show_touch_calibration_screen(void);
esp_err_t cyd_display_draw_touch_calibration_target(int32_t x, int32_t y, uint8_t radius, bool visible);
esp_err_t cyd_display_invalidate(void);
int32_t cyd_display_get_width(void);
int32_t cyd_display_get_height(void);
bool cyd_display_touch_to_grid(int16_t x, int16_t y, uint8_t *col, uint8_t *row);
bool cyd_display_hit_test_action(int16_t x, int16_t y, uint16_t *action_id);
bool cyd_display_get_mode_button_grid_rect(size_t index, cyd_display_grid_rect_t *rect);
bool cyd_display_hit_test_mode_button(int16_t x, int16_t y, size_t *button_index);
bool cyd_display_get_mode_button_bounds(size_t button_count,
                                        size_t index,
                                        int32_t *x,
                                        int32_t *y,
                                        int32_t *w,
                                        int32_t *h);

#ifdef __cplusplus
}
#endif

#endif
