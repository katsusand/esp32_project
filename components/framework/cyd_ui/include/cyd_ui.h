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
 * Colour themes for the anti-aliased Japanese UI (RGB565).
 *
 * Screens built with the label / styled-button calls below take their colours
 * from the selected theme, so every app reads as one product and follows the
 * theme the user picks in settings. Semantic colours (success, warning,
 * danger) carry meaning and are not decoration: an app should not reuse
 * DANGER for anything that is not an error or a destructive action.
 *
 * English supplement: *_TINT is a fill that pairs with the same-named colour
 * as a border or text (dark in dark themes, pale in the light theme); ON_* is
 * the text colour on a solid fill of that colour; *_SOFT is that colour as
 * text on BG or SURFACE. Every pair meets a contrast ratio that
 * test/host/test_ui_themes.c checks: 7:1 for text in the high-contrast theme,
 * 4.5:1 for text elsewhere, 3:1 for borders (WCAG 2.x formula, after RGB565).
 */
typedef struct {
    uint16_t bg;
    uint16_t surface;  /* panels, secondary buttons */
    uint16_t line;     /* borders, idle spinner track */
    uint16_t text;
    uint16_t subtext;  /* secondary text */
    uint16_t disabled; /* text and border of a disabled button */
    uint16_t success;
    uint16_t success_soft;
    uint16_t on_success;
    uint16_t primary;
    uint16_t primary_soft;
    uint16_t on_primary;
    uint16_t warning;
    uint16_t warning_tint;
    uint16_t on_warning;
    uint16_t info;     /* "do this next" prompts */
    uint16_t info_tint;
    uint16_t on_info;
    uint16_t danger;
    uint16_t danger_soft;
    uint16_t danger_tint;
    uint16_t on_danger;
} cyd_ui_theme_t;

/*
 * The selectable themes. The value is what the settings store, so existing
 * values must never be renumbered; add new themes at the end.
 */
typedef enum {
    CYD_UI_THEME_ID_HIGH_CONTRAST = 0, /* default: black, white and pure colours */
    CYD_UI_THEME_ID_STANDARD,          /* dark navy */
    CYD_UI_THEME_ID_LIGHT,             /* white background */
    CYD_UI_THEME_ID_COLOR_SAFE,        /* Okabe-Ito hues: no red/green pair carries meaning */
    CYD_UI_THEME_ID_COUNT,
} cyd_ui_theme_id_t;

#define CYD_UI_THEME_ID_DEFAULT CYD_UI_THEME_ID_HIGH_CONTRAST

/* The selected theme's colours. Never NULL. */
const cyd_ui_theme_t *cyd_ui_theme(void);
cyd_ui_theme_id_t cyd_ui_theme_id(void);
/* Selects a theme for screens built from now on; an unknown id selects the default. */
void cyd_ui_set_theme(cyd_ui_theme_id_t id);
/* The colours of any theme (an unknown id gives the default's). */
const cyd_ui_theme_t *cyd_ui_theme_colors(cyd_ui_theme_id_t id);
/* "高コントラスト", "標準", ...; an unknown id gives "". */
const char *cyd_ui_theme_name(cyd_ui_theme_id_t id);

/*
 * The selection is kept in NVS (namespace "sys_ui", system scope, so Clear App
 * Data keeps it). Load once at boot, after NVS and before the first screen;
 * save when the setting is committed. A missing or unknown stored value loads
 * the default. Firmware only (cyd_ui_theme_store.c).
 */
esp_err_t cyd_ui_theme_load(void);
esp_err_t cyd_ui_theme_save(void);

/* Colour names used by the screens: the selected theme's values. */
#define CYD_UI_THEME_BG           (cyd_ui_theme()->bg)
#define CYD_UI_THEME_SURFACE      (cyd_ui_theme()->surface)
#define CYD_UI_THEME_LINE         (cyd_ui_theme()->line)
#define CYD_UI_THEME_TEXT         (cyd_ui_theme()->text)
#define CYD_UI_THEME_SUBTEXT      (cyd_ui_theme()->subtext)
#define CYD_UI_THEME_DISABLED     (cyd_ui_theme()->disabled)
#define CYD_UI_THEME_SUCCESS      (cyd_ui_theme()->success)
#define CYD_UI_THEME_SUCCESS_SOFT (cyd_ui_theme()->success_soft)
#define CYD_UI_THEME_ON_SUCCESS   (cyd_ui_theme()->on_success)
#define CYD_UI_THEME_PRIMARY      (cyd_ui_theme()->primary)
#define CYD_UI_THEME_PRIMARY_SOFT (cyd_ui_theme()->primary_soft)
#define CYD_UI_THEME_ON_PRIMARY   (cyd_ui_theme()->on_primary)
#define CYD_UI_THEME_WARNING      (cyd_ui_theme()->warning)
#define CYD_UI_THEME_WARNING_TINT (cyd_ui_theme()->warning_tint)
#define CYD_UI_THEME_ON_WARNING   (cyd_ui_theme()->on_warning)
#define CYD_UI_THEME_INFO         (cyd_ui_theme()->info)
#define CYD_UI_THEME_INFO_TINT    (cyd_ui_theme()->info_tint)
#define CYD_UI_THEME_ON_INFO      (cyd_ui_theme()->on_info)
#define CYD_UI_THEME_DANGER       (cyd_ui_theme()->danger)
#define CYD_UI_THEME_DANGER_SOFT  (cyd_ui_theme()->danger_soft)
#define CYD_UI_THEME_DANGER_TINT  (cyd_ui_theme()->danger_tint)
#define CYD_UI_THEME_ON_DANGER    (cyd_ui_theme()->on_danger)

/* Border thickness of outlined buttons and panels in the themed UI. */
#define CYD_UI_THEME_BORDER_PX 2

/*
 * One "label  [−] value [+]" row of a settings page.
 *
 * Drawn in 16px: the label in subdued bold, the value and the "−" / "+"
 * buttons in bold. Legacy-sized buttons too small for 16px fall back to the
 * legacy font. Settings screens keep to 16px even where 24px would fit; next
 * to 16px rows it reads as unbalanced. `button_span_rows` is the height of the whole row; for the
 * resistive panel, prefer 4 rows (32px) or more.
 * Button colours default to the theme's outlined primary button; a caller sets
 * has_button_*_color only to give a row a different meaning.
 *
 * English supplement: the *_scale fields are left over from the legacy ASCII
 * font and are ignored. They stay so existing initialisers keep compiling.
 */
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
 *   rows 0-3    [戻る] App title                    (header band)
 *   rows 4-26   page content, owned by the caller
 *   rows 27-29  [前へ]     Page title n/N     [次へ]
 *
 * Drawn with the anti-aliased Japanese faces and the theme colours. The chrome
 * paints no screen background, so a page still drawn with the legacy font
 * keeps its black background.
 *
 * English contract: the caller supplies its own action ids so it keeps control
 * of its input handling; this only draws. Page navigation stops at the first
 * and last page rather than wrapping - the buttons render disabled there. Call
 * cyd_ui_add_settings_title() before the page content: it paints the header
 * band that the back button and the content are drawn over.
 */

/* Rows a page may draw into without colliding with the chrome. */
#define CYD_UI_SETTINGS_CONTENT_FIRST_ROW 4
#define CYD_UI_SETTINGS_CONTENT_LAST_ROW 26

typedef struct {
    /* Heading in the header band, e.g. "設定". Cut with "…" when too long. */
    const char *app_title;
    /*
     * Shown in the navigation row; " n/N" is appended. Pages that are numbered
     * within a group pass the number already formatted in, e.g. "ネットワーク2".
     */
    const char *page_title;
    /* 0-based. page_count of 1 renders both page buttons disabled. */
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

/*
 * Legacy ASCII font: do not use in new code.
 *
 * cyd_ui_add_text() and the cyd_ui_add_button*() family draw with the old
 * bitmap font, sized by an integer `scale`. It has no Japanese glyphs, does
 * not follow the colour theme and gives 16px-tall buttons by default. Use
 * cyd_ui_add_label() and cyd_ui_add_styled_button() with the CYD_UI_THEME_*
 * colours instead (see "Themed Japanese UI" in docs/cyd_ui.md).
 *
 * English contract: kept only for screens not yet converted, notably in
 * derived projects. No screen in this repository uses them any more; removing
 * them would break those projects on their next upstream merge.
 */
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
