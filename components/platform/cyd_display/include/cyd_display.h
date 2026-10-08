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
 * Text face of a TEXT or BUTTON widget.
 *
 * LEGACY is the built-in 6x8 ASCII font scaled by scale_x/scale_y, drawn
 * exactly as before this enum existed; it is the zero value so every screen
 * that never sets a font keeps its look. The other faces are the anti-aliased
 * Japanese UI fonts from cyd_ui_fonts and ignore the scale fields.
 *
 * English contract for the anti-aliased faces:
 *  - Text is centred vertically in the widget box for every alignment, and is
 *    clipped to the box, so it can never paint over a neighbour.
 *  - Text wider than the box falls back to the next smaller face of the same
 *    kind (TITLE -> BODY_BOLD, CLOCK_LARGE -> CLOCK_MEDIUM) and is then cut with
 *    "…". Nothing is ever silently clipped mid-character.
 *  - Glyphs are blended over whatever was drawn underneath; the widget's
 *    bg_color is not painted behind TEXT widgets.
 */
typedef enum {
    CYD_DISPLAY_FONT_LEGACY = 0,
    CYD_DISPLAY_FONT_BODY,         /* 16px regular; all of JIS X 0208 when the font profile asks */
    CYD_DISPLAY_FONT_BODY_BOLD,    /* 16px bold */
    CYD_DISPLAY_FONT_TITLE,        /* 24px bold */
    CYD_DISPLAY_FONT_CLOCK_MEDIUM, /* 48px bold, digits, ":/-. " and "✓!" only */
    CYD_DISPLAY_FONT_CLOCK_LARGE,  /* 64px bold, digits, ":/-. " and "✓!" only */
    CYD_DISPLAY_FONT_COUNT,
} cyd_display_font_t;

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
    /* cyd_display_widget_type_t, stored in one byte: see the size note below. */
    uint8_t type;
    uint8_t col;
    uint8_t row;
    uint8_t span_cols;
    uint8_t span_rows;
    /* cyd_display_align_t */
    uint8_t align;
    uint8_t scale_x;
    uint8_t scale_y;
    uint16_t fg_color;
    uint16_t bg_color;
    uint16_t border_color;
    uint16_t action_id;
    bool enabled;
    /* cyd_display_font_t; LEGACY (0) unless set. */
    uint8_t font;
    /* Border thickness in pixels for BUTTON and RECT; 0 draws the 1px default. */
    uint8_t border_width;
    const cyd_display_bitmap_t *bitmap;
    /*
     * Text that lives in read-only storage (a string literal or other
     * `static const` data) is referenced here instead of being copied into
     * `text`, so a label can be longer than CYD_DISPLAY_TEXT_MAX_LEN bytes.
     * Japanese takes three bytes per character, so most labels need this.
     *
     * English contract: set it only through cyd_display_widget_set_text(),
     * which takes the reference only for immutable storage and copies
     * anything else. A pointer to a stack or heap buffer here would be read
     * after submit() returned, on the display task.
     */
    const char *text_ref;
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

/*
 * Size note: every screen buffer holds CYD_DISPLAY_MAX_WIDGETS widgets and the
 * firmware keeps a couple of dozen buffers, so each byte here costs about a
 * kilobyte of RAM. `type` and `align` are bytes rather than enums so that
 * `font`, `border_width` and `text_ref` fit without growing the struct.
 */
typedef struct {
    uint8_t widget_count;
    cyd_display_widget_t widgets[CYD_DISPLAY_MAX_WIDGETS];
} cyd_display_screen_t;

/*
 * Sets the text of a TEXT or BUTTON widget.
 *
 * Text in immutable storage is referenced (see `text_ref`) and may be any
 * length. Anything else is copied into the widget's buffer and, when it does
 * not fit, cut at a UTF-8 character boundary - never mid-character.
 */
void cyd_display_widget_set_text(cyd_display_widget_t *widget, const char *text);

/*
 * Sets the text of a TEXT or BUTTON widget by reference, wherever it lives.
 *
 * For text in RAM that is longer than CYD_DISPLAY_TEXT_MAX_LEN bytes, such as
 * an announcement fetched from the backend. Literals do not need this;
 * cyd_display_widget_set_text() already references them.
 *
 * English contract, same as cyd_display_bitmap_t and sparkline samples: the
 * text is read later, on the display task. It must stay valid AND unchanged
 * for as long as any submitted screen references it. Change the buffer only
 * while the screen on the panel does not use it - for example when entering
 * the view that shows it - because the frame diff compares the previous and
 * the current screen by content, and an in-place edit makes both read the new
 * words, so the change would never be drawn.
 */
void cyd_display_widget_set_text_pinned(cyd_display_widget_t *widget, const char *text);

/* The text a TEXT or BUTTON widget shows; never NULL. */
const char *cyd_display_widget_text(const cyd_display_widget_t *widget);

/*
 * Copies `src` into `dst` like strlcpy, but never splits a UTF-8 sequence:
 * when `src` does not fit, the copy stops before the first character that
 * would be cut. Returns true when the whole string fit.
 */
bool cyd_display_utf8_copy(char *dst, size_t dst_size, const char *src);

/* Pixel width `text` takes in an anti-aliased face; 0 for LEGACY. */
int32_t cyd_display_text_width(cyd_display_font_t font, const char *text);

esp_err_t cyd_display_init(void);
esp_err_t cyd_display_set_brightness(uint8_t brightness);
esp_err_t cyd_display_save_brightness(void);
uint8_t cyd_display_get_brightness(void);
esp_err_t cyd_display_claim_owner(void);
esp_err_t cyd_display_release_owner(void);
esp_err_t cyd_display_submit_screen(const cyd_display_screen_t *screen);
/*
 * Canned screens. Each is built into the caller's `screen` and then submitted,
 * so that buffer always holds what is on the display and the caller can
 * hit-test it (see cyd_display_screen_hit_test()). A text or lines screen has
 * no buttons; mode screen buttons get action ids 0 .. button_count - 1.
 *
 * Legacy ASCII font: do not use in new code. These screens cannot show
 * Japanese and ignore the colour theme; build the screen with cyd_ui labels
 * and styled buttons instead. Kept for derived projects that still call them.
 * The boot and log screens are developer diagnostics and stay in English.
 */
esp_err_t cyd_display_show_boot_screen(void);
esp_err_t cyd_display_show_text(cyd_display_screen_t *screen, const char *title, const char *message);
esp_err_t cyd_display_show_lines(cyd_display_screen_t *screen,
                                 const char *title,
                                 const char *const *lines,
                                 size_t line_count);
esp_err_t cyd_display_show_mode_screen(cyd_display_screen_t *screen,
                                       const char *title,
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
/*
 * Finds the enabled button of `screen` under the touch point.
 *
 * English contract: an app hit-tests the screen buffer it built and last
 * submitted, on its own task. This component keeps no copy of anyone's
 * buttons, so what a tap means always matches what that app drew, with no
 * lock and no dependence on how far the display task has got. An app that
 * lets another component draw (a text input session, say) must hand touch
 * handling over to it for that time rather than test its own hidden screen.
 */
bool cyd_display_screen_hit_test(const cyd_display_screen_t *screen, int16_t x, int16_t y, uint16_t *action_id);
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
