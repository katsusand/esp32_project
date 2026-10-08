#ifndef CYD_UI_H
#define CYD_UI_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "cyd_display.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CYD_UI_COLOR_BLACK     0x0000
#define CYD_UI_COLOR_WHITE     0xffff
#define CYD_UI_COLOR_YELLOW    0xffe0
#define CYD_UI_COLOR_BLUE      0x001f
#define CYD_UI_COLOR_CYAN      0x07ff
#define CYD_UI_COLOR_DARKGREY  0x7bef
#define CYD_UI_COLOR_DIMGREY   0x39e7
#define CYD_UI_COLOR_LIGHTGREY 0xc618
#define CYD_UI_COLOR_RED       0xf800
#define CYD_UI_COLOR_GREEN     0x07e0
#define CYD_UI_COLOR_DISABLED_FG CYD_UI_COLOR_LIGHTGREY
#define CYD_UI_COLOR_DISABLED_BG CYD_UI_COLOR_DIMGREY
#define CYD_UI_COLOR_DISABLED_BORDER CYD_UI_COLOR_DARKGREY

/*
 * Theme for the anti-aliased Japanese UI (RGB565).
 *
 * Screens built with the label / styled-button calls below take their colours
 * from here so every app reads as one product. Semantic colours (success,
 * warning, danger) carry meaning and are not decoration: an app should not
 * reuse DANGER for anything that is not an error or a destructive action.
 *
 * English supplement: *_TINT is a dark fill that pairs with the same-named
 * colour as a border or text; ON_* is the text colour to use on a solid fill
 * of that colour.
 */
#define CYD_UI_THEME_BG           0x0884 /* #0a1020 */
#define CYD_UI_THEME_SURFACE      0x1106 /* #172033 panels, secondary buttons */
#define CYD_UI_THEME_LINE         0x29aa /* #2a3650 borders, idle spinner track */
#define CYD_UI_THEME_TEXT         0xf7bf /* #f1f4f9 */
#define CYD_UI_THEME_SUBTEXT      0x9517 /* #94a3ba secondary text */
#define CYD_UI_THEME_SUCCESS      0x262b /* #22c55e */
#define CYD_UI_THEME_SUCCESS_SOFT 0x7f14 /* #7ee2a0 success text on dark */
#define CYD_UI_THEME_ON_SUCCESS   0x0101 /* #06210f */
#define CYD_UI_THEME_PRIMARY      0x3c1e /* #3b82f6 */
#define CYD_UI_THEME_PRIMARY_SOFT 0x8ddf /* #8fb8ff primary text on dark */
#define CYD_UI_THEME_ON_PRIMARY   0xffff
#define CYD_UI_THEME_WARNING      0xf4e1 /* #f59e0b */
#define CYD_UI_THEME_WARNING_TINT 0x2901 /* #2b2108 */
#define CYD_UI_THEME_ON_WARNING   0x28c0 /* #2b1a00 */
#define CYD_UI_THEME_INFO         0x269d /* #22d3ee "do this next" prompts */
#define CYD_UI_THEME_INFO_TINT    0x0988 /* #0d3340 */
#define CYD_UI_THEME_ON_INFO      0x0126 /* #032530 */
#define CYD_UI_THEME_DANGER       0xea28 /* #ef4444 */
#define CYD_UI_THEME_DANGER_TINT  0x3882 /* #3a1114 */
#define CYD_UI_THEME_ON_DANGER    0xffff

/* Border thickness of outlined buttons and panels in the themed UI. */
#define CYD_UI_THEME_BORDER_PX 2

typedef struct {
    const char *label_text;
    const char *value_text;
    uint8_t row;
    uint8_t label_col;
    uint8_t label_span_cols;
    uint8_t label_scale;
    uint8_t value_col;
    uint8_t value_span_cols;
    uint8_t value_scale;
    uint8_t button_left_col;
    uint8_t button_right_col;
    uint8_t button_span_cols;
    uint8_t button_span_rows;
    uint8_t button_scale;
    bool has_button_fg_color;
    uint16_t button_fg_color;
    bool has_button_bg_color;
    uint16_t button_bg_color;
    bool has_button_border_color;
    uint16_t button_border_color;
    uint16_t decrease_action_id;
    uint16_t increase_action_id;
    bool can_decrease;
    bool can_increase;
} cyd_ui_stepper_row_t;

/*
 * Shared chrome for settings screens.
 *
 * A settings screen is not a framework concept, but its frame is: every app
 * that grows one has to draw the same back button, the same right-aligned
 * heading and the same bottom navigation row. Three apps had already copied
 * those constants, and the copies drifted - a back button that read "<" instead
 * of "<<", navigation that wrapped instead of stopping at the ends, a missing
 * page counter. Each copy was individually reasonable and collectively wrong.
 *
 * The layout is therefore defined once, here.
 *
 *   row 0    [<<]                              APP TITLE   (right aligned)
 *   rows 4-26  page content, owned by the caller
 *   row 27   [<]        Page title  n/N        [>]
 *
 * English contract: the caller supplies its own action ids so it keeps control
 * of its input handling; this only draws. Page navigation stops at the first
 * and last page rather than wrapping - the buttons render disabled there.
 */

/* Rows a page may draw into without colliding with the chrome. */
#define CYD_UI_SETTINGS_CONTENT_FIRST_ROW 4
#define CYD_UI_SETTINGS_CONTENT_LAST_ROW 26

typedef struct {
    /* Right-aligned heading, e.g. "SETTINGS" or "CLOCK SETTINGS". */
    const char *app_title;
    /*
     * Shown in the navigation row; "  n/N" is appended. Pages that are numbered
     * within a group pass the number already formatted in, e.g. "NETWORK2".
     */
    const char *page_title;
    /* 0-based. page_count of 1 renders both arrows disabled. */
    size_t page_index;
    size_t page_count;
    uint16_t back_action_id;
    uint16_t prev_page_action_id;
    uint16_t next_page_action_id;
} cyd_ui_settings_chrome_t;

/*
 * The pieces, for screens that need only some of them. A settings sub-screen
 * (a confirmation, a list) keeps the heading and the back button but has no
 * pages, so it uses the first two directly.
 */
void cyd_ui_add_settings_title(cyd_display_screen_t *screen, const char *app_title);
void cyd_ui_add_settings_back(cyd_display_screen_t *screen, uint16_t back_action_id);
esp_err_t cyd_ui_add_settings_page_nav(cyd_display_screen_t *screen,
                                       const char *page_title,
                                       size_t page_index,
                                       size_t page_count,
                                       uint16_t prev_page_action_id,
                                       uint16_t next_page_action_id);

/* All three at once, for an ordinary paged settings screen. */
esp_err_t cyd_ui_add_settings_chrome(cyd_display_screen_t *screen,
                                     const cyd_ui_settings_chrome_t *chrome);

void cyd_ui_screen_clear(cyd_display_screen_t *screen);
bool cyd_ui_add_text(cyd_display_screen_t *screen,
                     const char *text,
                     uint8_t col,
                     uint8_t row,
                     uint8_t span_cols,
                     uint8_t span_rows,
                     cyd_display_align_t align,
                     uint8_t scale,
                     uint16_t fg_color);
bool cyd_ui_add_button(cyd_display_screen_t *screen,
                       const char *text,
                       uint8_t col,
                       uint8_t row,
                       uint8_t span_cols,
                       uint8_t span_rows,
                       uint16_t bg_color,
                       uint16_t border_color,
                       uint16_t action_id);
bool cyd_ui_add_button_enabled(cyd_display_screen_t *screen,
                               const char *text,
                               uint8_t col,
                               uint8_t row,
                               uint8_t span_cols,
                               uint8_t span_rows,
                               uint16_t bg_color,
                               uint16_t border_color,
                               uint16_t action_id,
                               bool enabled);
bool cyd_ui_add_button_with_fg(cyd_display_screen_t *screen,
                               const char *text,
                               uint8_t col,
                               uint8_t row,
                               uint8_t span_cols,
                               uint8_t span_rows,
                               uint16_t fg_color,
                               uint16_t bg_color,
                               uint16_t border_color,
                               uint16_t action_id);
bool cyd_ui_add_button_with_fg_enabled(cyd_display_screen_t *screen,
                                       const char *text,
                                       uint8_t col,
                                       uint8_t row,
                                       uint8_t span_cols,
                                       uint8_t span_rows,
                                       uint16_t fg_color,
                                       uint16_t bg_color,
                                       uint16_t border_color,
                                       uint16_t action_id,
                                       bool enabled);
esp_err_t cyd_ui_add_stepper_row(cyd_display_screen_t *screen,
                                 const cyd_ui_stepper_row_t *row);

/* Panel / separator / background plate. radius 0 draws square corners. */
bool cyd_ui_add_rect(cyd_display_screen_t *screen,
                     uint8_t col,
                     uint8_t row,
                     uint8_t span_cols,
                     uint8_t span_rows,
                     uint16_t bg_color,
                     uint16_t border_color,
                     bool filled,
                     uint8_t radius);

/* Level meter / gauge. value is saturated into [min_value, max_value]. */
bool cyd_ui_add_bar(cyd_display_screen_t *screen,
                    uint8_t col,
                    uint8_t row,
                    uint8_t span_cols,
                    uint8_t span_rows,
                    int16_t value,
                    int16_t min_value,
                    int16_t max_value,
                    bool vertical,
                    uint16_t fg_color,
                    uint16_t bg_color,
                    uint16_t border_color);

/*
 * Trend graph.
 *
 * English contract: `samples` is not copied and must stay valid until the
 * screen has been submitted; app-owned static storage is the intended pattern.
 * Bump `revision` on every content change or the dirty-rect diff will treat the
 * graph as unchanged and never redraw it. See cyd_display_sparkline_t.
 */
bool cyd_ui_add_sparkline(cyd_display_screen_t *screen,
                          uint8_t col,
                          uint8_t row,
                          uint8_t span_cols,
                          uint8_t span_rows,
                          const cyd_display_sparkline_t *sparkline,
                          uint16_t fg_color,
                          uint16_t bg_color,
                          uint16_t border_color);

/*
 * RGB565 image block.
 *
 * English contract: `bitmap` and its pixels are NOT copied. Both must stay
 * valid until the submitted frame has been rendered on the display task, so
 * pass `static const` data or a buffer owned for the app's lifetime. A local
 * buffer in the calling function is a use-after-free. See cyd_display_bitmap_t.
 */
bool cyd_ui_add_icon(cyd_display_screen_t *screen,
                     const cyd_display_bitmap_t *bitmap,
                     uint8_t col,
                     uint8_t row,
                     uint8_t span_cols,
                     uint8_t span_rows);

/*
 * Anti-aliased text in one of the cyd_display_font_t faces.
 *
 * The text is centred vertically in the box, clipped to it, and shrunk or cut
 * with "…" when too wide (see cyd_display_font_t). String literals are
 * referenced rather than copied, so a label may be any length; other text is
 * copied and limited to CYD_DISPLAY_TEXT_MAX_LEN bytes.
 */
bool cyd_ui_add_label(cyd_display_screen_t *screen,
                      const char *text,
                      uint8_t col,
                      uint8_t row,
                      uint8_t span_cols,
                      uint8_t span_rows,
                      cyd_display_align_t align,
                      cyd_display_font_t font,
                      uint16_t fg_color);

/*
 * cyd_ui_add_label() for text in RAM that may be longer than
 * CYD_DISPLAY_TEXT_MAX_LEN bytes. The text is referenced, never copied.
 *
 * English contract: see cyd_display_widget_set_text_pinned() - the buffer must
 * not change while a submitted screen uses it.
 */
bool cyd_ui_add_label_pinned(cyd_display_screen_t *screen,
                             const char *text,
                             uint8_t col,
                             uint8_t row,
                             uint8_t span_cols,
                             uint8_t span_rows,
                             cyd_display_align_t align,
                             cyd_display_font_t font,
                             uint16_t fg_color);

/*
 * A button with an anti-aliased label.
 *
 * A solid button passes border_color == bg_color; an outlined one passes a
 * dark fill (a *_TINT or CYD_UI_THEME_SURFACE) with a coloured border and
 * text. border_width is in pixels; 0 draws 1px. A disabled button is drawn in
 * the disabled colours and is skipped by hit testing.
 */
bool cyd_ui_add_styled_button(cyd_display_screen_t *screen,
                              const char *text,
                              uint8_t col,
                              uint8_t row,
                              uint8_t span_cols,
                              uint8_t span_rows,
                              cyd_display_font_t font,
                              uint16_t fg_color,
                              uint16_t bg_color,
                              uint16_t border_color,
                              uint8_t border_width,
                              uint16_t action_id,
                              bool enabled);

/* A rounded panel or plate with a border of `border_width` pixels. */
bool cyd_ui_add_panel(cyd_display_screen_t *screen,
                      uint8_t col,
                      uint8_t row,
                      uint8_t span_cols,
                      uint8_t span_rows,
                      uint16_t bg_color,
                      uint16_t border_color,
                      uint8_t border_width,
                      uint8_t radius);

esp_err_t cyd_ui_submit(const cyd_display_screen_t *screen);

#ifdef __cplusplus
}
#endif

#endif
