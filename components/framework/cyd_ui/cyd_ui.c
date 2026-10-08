#include <stdio.h>
#include <string.h>
#include "esp_check.h"
#include "esp_log.h"
#include "cyd_ui.h"

#define TAG "cyd_ui"

void cyd_ui_screen_clear(cyd_display_screen_t *screen)
{
    if (screen != NULL) {
        memset(screen, 0, sizeof(*screen));
    }
}

static bool cyd_ui_add_widget(cyd_display_screen_t *screen, const cyd_display_widget_t *widget)
{
    if (screen == NULL || widget == NULL || screen->widget_count >= CYD_DISPLAY_MAX_WIDGETS) {
        return false;
    }
    screen->widgets[screen->widget_count++] = *widget;
    return true;
}

static uint16_t cyd_ui_button_fg_color(uint16_t fg_color, bool enabled)
{
    return enabled ? fg_color : CYD_UI_COLOR_DISABLED_FG;
}

static uint16_t cyd_ui_button_bg_color(uint16_t bg_color, bool enabled)
{
    return enabled ? bg_color : CYD_UI_COLOR_DISABLED_BG;
}

static uint16_t cyd_ui_button_border_color(uint16_t border_color, bool enabled)
{
    return enabled ? border_color : CYD_UI_COLOR_DISABLED_BORDER;
}

bool cyd_ui_add_text(cyd_display_screen_t *screen,
                     const char *text,
                     uint8_t col,
                     uint8_t row,
                     uint8_t span_cols,
                     uint8_t span_rows,
                     cyd_display_align_t align,
                     uint8_t scale,
                     uint16_t fg_color)
{
    cyd_display_widget_t widget = {
        .type = CYD_DISPLAY_WIDGET_TEXT,
        .col = col,
        .row = row,
        .span_cols = span_cols,
        .span_rows = span_rows,
        .align = align,
        .scale_x = scale,
        .scale_y = scale,
        .fg_color = fg_color,
        .bg_color = CYD_UI_COLOR_BLACK,
        .enabled = true,
    };
    cyd_display_widget_set_text(&widget, text);
    return cyd_ui_add_widget(screen, &widget);
}

bool cyd_ui_add_button(cyd_display_screen_t *screen,
                       const char *text,
                       uint8_t col,
                       uint8_t row,
                       uint8_t span_cols,
                       uint8_t span_rows,
                       uint16_t bg_color,
                       uint16_t border_color,
                       uint16_t action_id)
{
    return cyd_ui_add_button_enabled(screen,
                                     text,
                                     col,
                                     row,
                                     span_cols,
                                     span_rows,
                                     bg_color,
                                     border_color,
                                     action_id,
                                     true);
}

bool cyd_ui_add_button_enabled(cyd_display_screen_t *screen,
                               const char *text,
                               uint8_t col,
                               uint8_t row,
                               uint8_t span_cols,
                               uint8_t span_rows,
                               uint16_t bg_color,
                               uint16_t border_color,
                               uint16_t action_id,
                               bool enabled)
{
    return cyd_ui_add_button_with_fg_enabled(screen,
                                            text,
                                            col,
                                            row,
                                            span_cols,
                                            span_rows,
                                            CYD_UI_COLOR_WHITE,
                                            bg_color,
                                            border_color,
                                            action_id,
                                            enabled);
}

bool cyd_ui_add_button_with_fg(cyd_display_screen_t *screen,
                               const char *text,
                               uint8_t col,
                               uint8_t row,
                               uint8_t span_cols,
                               uint8_t span_rows,
                               uint16_t fg_color,
                               uint16_t bg_color,
                               uint16_t border_color,
                               uint16_t action_id)
{
    return cyd_ui_add_button_with_fg_enabled(screen,
                                            text,
                                            col,
                                            row,
                                            span_cols,
                                            span_rows,
                                            fg_color,
                                            bg_color,
                                            border_color,
                                            action_id,
                                            true);
}

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
                                       bool enabled)
{
    cyd_display_widget_t widget = {
        .type = CYD_DISPLAY_WIDGET_BUTTON,
        .col = col,
        .row = row,
        .span_cols = span_cols,
        .span_rows = span_rows,
        .align = CYD_DISPLAY_ALIGN_CENTER,
        .scale_x = 1,
        .scale_y = 1,
        .fg_color = cyd_ui_button_fg_color(fg_color, enabled),
        .bg_color = cyd_ui_button_bg_color(bg_color, enabled),
        .border_color = cyd_ui_button_border_color(border_color, enabled),
        .action_id = action_id,
        .enabled = enabled,
    };
    cyd_display_widget_set_text(&widget, text);
    return cyd_ui_add_widget(screen, &widget);
}

bool cyd_ui_add_rect(cyd_display_screen_t *screen,
                     uint8_t col,
                     uint8_t row,
                     uint8_t span_cols,
                     uint8_t span_rows,
                     uint16_t bg_color,
                     uint16_t border_color,
                     bool filled,
                     uint8_t radius)
{
    cyd_display_widget_t widget = {
        .type = CYD_DISPLAY_WIDGET_RECT,
        .col = col,
        .row = row,
        .span_cols = span_cols,
        .span_rows = span_rows,
        .bg_color = bg_color,
        .border_color = border_color,
        .enabled = true,
        .rect = {
            .filled = filled,
            .radius = radius,
        },
    };
    return cyd_ui_add_widget(screen, &widget);
}

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
                    uint16_t border_color)
{
    cyd_display_widget_t widget = {
        .type = CYD_DISPLAY_WIDGET_BAR,
        .col = col,
        .row = row,
        .span_cols = span_cols,
        .span_rows = span_rows,
        .fg_color = fg_color,
        .bg_color = bg_color,
        .border_color = border_color,
        .enabled = true,
        .bar = {
            .value = value,
            .min_value = min_value,
            .max_value = max_value,
            .vertical = vertical,
        },
    };
    return cyd_ui_add_widget(screen, &widget);
}

bool cyd_ui_add_sparkline(cyd_display_screen_t *screen,
                          uint8_t col,
                          uint8_t row,
                          uint8_t span_cols,
                          uint8_t span_rows,
                          const cyd_display_sparkline_t *sparkline,
                          uint16_t fg_color,
                          uint16_t bg_color,
                          uint16_t border_color)
{
    if (sparkline == NULL) {
        return false;
    }

    cyd_display_widget_t widget = {
        .type = CYD_DISPLAY_WIDGET_SPARKLINE,
        .col = col,
        .row = row,
        .span_cols = span_cols,
        .span_rows = span_rows,
        .fg_color = fg_color,
        .bg_color = bg_color,
        .border_color = border_color,
        .enabled = true,
        .sparkline = *sparkline,
    };
    return cyd_ui_add_widget(screen, &widget);
}

bool cyd_ui_add_icon(cyd_display_screen_t *screen,
                     const cyd_display_bitmap_t *bitmap,
                     uint8_t col,
                     uint8_t row,
                     uint8_t span_cols,
                     uint8_t span_rows)
{
    if (bitmap == NULL || bitmap->data == NULL || bitmap->width_px == 0 || bitmap->height_px == 0) {
        return false;
    }

    cyd_display_widget_t widget = {
        .type = CYD_DISPLAY_WIDGET_ICON,
        .col = col,
        .row = row,
        .span_cols = span_cols,
        .span_rows = span_rows,
        .enabled = true,
        .bitmap = bitmap,
    };
    return cyd_ui_add_widget(screen, &widget);
}

static cyd_display_widget_t cyd_ui_label_widget(uint8_t col,
                                                uint8_t row,
                                                uint8_t span_cols,
                                                uint8_t span_rows,
                                                cyd_display_align_t align,
                                                cyd_display_font_t font,
                                                uint16_t fg_color)
{
    cyd_display_widget_t widget = {
        .type = CYD_DISPLAY_WIDGET_TEXT,
        .col = col,
        .row = row,
        .span_cols = span_cols,
        .span_rows = span_rows,
        .align = (uint8_t)align,
        .scale_x = 1,
        .scale_y = 1,
        .fg_color = fg_color,
        .bg_color = CYD_UI_THEME_BG,
        .enabled = true,
        .font = (uint8_t)font,
    };
    return widget;
}

bool cyd_ui_add_label(cyd_display_screen_t *screen,
                      const char *text,
                      uint8_t col,
                      uint8_t row,
                      uint8_t span_cols,
                      uint8_t span_rows,
                      cyd_display_align_t align,
                      cyd_display_font_t font,
                      uint16_t fg_color)
{
    cyd_display_widget_t widget = cyd_ui_label_widget(col, row, span_cols, span_rows, align, font, fg_color);
    cyd_display_widget_set_text(&widget, text);
    return cyd_ui_add_widget(screen, &widget);
}

bool cyd_ui_add_label_pinned(cyd_display_screen_t *screen,
                             const char *text,
                             uint8_t col,
                             uint8_t row,
                             uint8_t span_cols,
                             uint8_t span_rows,
                             cyd_display_align_t align,
                             cyd_display_font_t font,
                             uint16_t fg_color)
{
    cyd_display_widget_t widget = cyd_ui_label_widget(col, row, span_cols, span_rows, align, font, fg_color);
    cyd_display_widget_set_text_pinned(&widget, text);
    return cyd_ui_add_widget(screen, &widget);
}

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
                              bool enabled)
{
    cyd_display_widget_t widget = {
        .type = CYD_DISPLAY_WIDGET_BUTTON,
        .col = col,
        .row = row,
        .span_cols = span_cols,
        .span_rows = span_rows,
        .align = CYD_DISPLAY_ALIGN_CENTER,
        .scale_x = 1,
        .scale_y = 1,
        .fg_color = enabled ? fg_color : CYD_UI_THEME_SUBTEXT,
        .bg_color = enabled ? bg_color : CYD_UI_THEME_SURFACE,
        .border_color = enabled ? border_color : CYD_UI_THEME_LINE,
        .action_id = action_id,
        .enabled = enabled,
        .font = (uint8_t)font,
        .border_width = border_width,
    };
    cyd_display_widget_set_text(&widget, text);
    return cyd_ui_add_widget(screen, &widget);
}

bool cyd_ui_add_panel(cyd_display_screen_t *screen,
                      uint8_t col,
                      uint8_t row,
                      uint8_t span_cols,
                      uint8_t span_rows,
                      uint16_t bg_color,
                      uint16_t border_color,
                      uint8_t border_width,
                      uint8_t radius)
{
    cyd_display_widget_t widget = {
        .type = CYD_DISPLAY_WIDGET_RECT,
        .col = col,
        .row = row,
        .span_cols = span_cols,
        .span_rows = span_rows,
        .bg_color = bg_color,
        .border_color = border_color,
        .enabled = true,
        .border_width = border_width,
        .rect = {
            .filled = true,
            .radius = radius,
        },
    };
    return cyd_ui_add_widget(screen, &widget);
}

/*
 * 16px bold when `text` fits a button of the given size, else the legacy ASCII
 * font. Small legacy-sized buttons keep a readable "-" instead of an ellipsis
 * until their page is laid out again.
 *
 * Settings screens stay at 16px on purpose: they carry many rows, and a 24px
 * value or button next to 16px rows reads as unbalanced even where it fits.
 */
static cyd_display_font_t cyd_ui_button_font_for(const char *text, uint8_t span_cols, uint8_t span_rows)
{
    /* WIDGET_BUTTON_TEXT_INSET_PX on each side, in cyd_display_render.hpp. */
    const int32_t inner_w = (int32_t)span_cols * CYD_DISPLAY_GRID_CELL_PX - 2 * 6;
    const int32_t h = (int32_t)span_rows * CYD_DISPLAY_GRID_CELL_PX;

    if (h >= 16 && cyd_display_text_width(CYD_DISPLAY_FONT_BODY_BOLD, text) <= inner_w) {
        return CYD_DISPLAY_FONT_BODY_BOLD;
    }
    return CYD_DISPLAY_FONT_LEGACY;
}

esp_err_t cyd_ui_add_stepper_row(cyd_display_screen_t *screen,
                                 const cyd_ui_stepper_row_t *row)
{
    ESP_RETURN_ON_FALSE(screen != NULL, ESP_ERR_INVALID_ARG, TAG, "screen is null");
    ESP_RETURN_ON_FALSE(row != NULL, ESP_ERR_INVALID_ARG, TAG, "row is null");
    ESP_RETURN_ON_FALSE(row->label_text != NULL, ESP_ERR_INVALID_ARG, TAG, "row label is null");
    ESP_RETURN_ON_FALSE(row->value_text != NULL, ESP_ERR_INVALID_ARG, TAG, "row value is null");

    /* The *_scale fields belong to the legacy font and are ignored here. */
    const cyd_display_font_t value_font = CYD_DISPLAY_FONT_BODY_BOLD;
    const cyd_display_font_t button_font =
        cyd_ui_button_font_for("−", row->button_span_cols, row->button_span_rows);
    /* The legacy font has no U+2212. */
    const char *minus = button_font == CYD_DISPLAY_FONT_LEGACY ? "-" : "−";
    const uint16_t fg = row->has_button_fg_color ? row->button_fg_color : CYD_UI_THEME_PRIMARY_SOFT;
    const uint16_t bg = row->has_button_bg_color ? row->button_bg_color : CYD_UI_THEME_SURFACE;
    const uint16_t border = row->has_button_border_color ? row->button_border_color : CYD_UI_THEME_PRIMARY;

    /* A label written for the legacy font (e.g. "LcdBrightness:") can be too
       wide for 16px bold; it stays readable in the legacy font rather than
       being cut with "…" until its page is reworded. */
    const cyd_display_font_t label_font =
        cyd_display_text_width(CYD_DISPLAY_FONT_BODY_BOLD, row->label_text) <=
                (int32_t)row->label_span_cols * CYD_DISPLAY_GRID_CELL_PX
            ? CYD_DISPLAY_FONT_BODY_BOLD
            : CYD_DISPLAY_FONT_LEGACY;

    ESP_RETURN_ON_FALSE(cyd_ui_add_label(screen,
                                         row->label_text,
                                         row->label_col,
                                         row->row,
                                         row->label_span_cols,
                                         row->button_span_rows,
                                         CYD_DISPLAY_ALIGN_LEFT,
                                         label_font,
                                         CYD_UI_THEME_SUBTEXT),
                        ESP_ERR_NO_MEM,
                        TAG,
                        "add stepper label failed");
    ESP_RETURN_ON_FALSE(cyd_ui_add_label(screen,
                                         row->value_text,
                                         row->value_col,
                                         row->row,
                                         row->value_span_cols,
                                         row->button_span_rows,
                                         CYD_DISPLAY_ALIGN_CENTER,
                                         value_font,
                                         CYD_UI_THEME_TEXT),
                        ESP_ERR_NO_MEM,
                        TAG,
                        "add stepper value failed");
    ESP_RETURN_ON_FALSE(cyd_ui_add_styled_button(screen,
                                                 minus,
                                                 row->button_left_col,
                                                 row->row,
                                                 row->button_span_cols,
                                                 row->button_span_rows,
                                                 button_font,
                                                 fg,
                                                 bg,
                                                 border,
                                                 CYD_UI_THEME_BORDER_PX,
                                                 row->decrease_action_id,
                                                 row->can_decrease),
                        ESP_ERR_NO_MEM,
                        TAG,
                        "add stepper decrease button failed");
    ESP_RETURN_ON_FALSE(cyd_ui_add_styled_button(screen,
                                                 "+",
                                                 row->button_right_col,
                                                 row->row,
                                                 row->button_span_cols,
                                                 row->button_span_rows,
                                                 button_font,
                                                 fg,
                                                 bg,
                                                 border,
                                                 CYD_UI_THEME_BORDER_PX,
                                                 row->increase_action_id,
                                                 row->can_increase),
                        ESP_ERR_NO_MEM,
                        TAG,
                        "add stepper increase button failed");
    return ESP_OK;
}

esp_err_t cyd_ui_submit(const cyd_display_screen_t *screen)
{
    return cyd_display_submit_screen(screen);
}

/*
 * Settings chrome geometry. The single definition of this layout.
 *
 * The header band (rows 0-3) and the page navigation (rows 27-29) stay
 * outside CYD_UI_SETTINGS_CONTENT_FIRST_ROW..LAST_ROW, so pages never collide
 * with them. The chrome paints no screen background: a page that still uses
 * the legacy font keeps its black background, and its text (which fills its
 * own box black) does not show as dark blocks on a themed one.
 */
#define CYD_UI_SETTINGS_HEADER_ROWS 4
#define CYD_UI_SETTINGS_BACK_COL 0
#define CYD_UI_SETTINGS_BACK_SPAN_COLS 8
#define CYD_UI_SETTINGS_TITLE_COL 9
#define CYD_UI_SETTINGS_TITLE_SPAN_COLS 30
#define CYD_UI_SETTINGS_PAGE_ROW 27
#define CYD_UI_SETTINGS_PAGE_SPAN_ROWS 3
#define CYD_UI_SETTINGS_PAGE_PREV_COL 0
#define CYD_UI_SETTINGS_PAGE_NEXT_COL 30
#define CYD_UI_SETTINGS_PAGE_BUTTON_SPAN_COLS 10
#define CYD_UI_SETTINGS_PAGE_LABEL_COL 10
#define CYD_UI_SETTINGS_PAGE_LABEL_SPAN_COLS 20

_Static_assert(CYD_UI_SETTINGS_HEADER_ROWS <= CYD_UI_SETTINGS_CONTENT_FIRST_ROW,
                  "settings header overlaps the content area");
_Static_assert(CYD_UI_SETTINGS_PAGE_ROW > CYD_UI_SETTINGS_CONTENT_LAST_ROW,
                  "settings page navigation overlaps the content area");

void cyd_ui_add_settings_title(cyd_display_screen_t *screen, const char *app_title)
{
    if (screen == NULL || app_title == NULL) {
        return;
    }
    cyd_ui_add_panel(screen, 0, 0, CYD_DISPLAY_GRID_COLS, CYD_UI_SETTINGS_HEADER_ROWS,
                     CYD_UI_THEME_SURFACE, 0, 0, 0);
    cyd_ui_add_label(screen,
                     app_title,
                     CYD_UI_SETTINGS_TITLE_COL,
                     0,
                     CYD_UI_SETTINGS_TITLE_SPAN_COLS,
                     CYD_UI_SETTINGS_HEADER_ROWS,
                     CYD_DISPLAY_ALIGN_LEFT,
                     CYD_DISPLAY_FONT_BODY_BOLD,
                     CYD_UI_THEME_TEXT);
}

void cyd_ui_add_settings_back(cyd_display_screen_t *screen, uint16_t back_action_id)
{
    if (screen == NULL) {
        return;
    }
    cyd_ui_add_styled_button(screen,
                             "戻る",
                             CYD_UI_SETTINGS_BACK_COL,
                             0,
                             CYD_UI_SETTINGS_BACK_SPAN_COLS,
                             CYD_UI_SETTINGS_HEADER_ROWS,
                             CYD_DISPLAY_FONT_BODY_BOLD,
                             CYD_UI_THEME_PRIMARY_SOFT,
                             CYD_UI_THEME_SURFACE,
                             CYD_UI_THEME_PRIMARY,
                             CYD_UI_THEME_BORDER_PX,
                             back_action_id,
                             true);
}

esp_err_t cyd_ui_add_settings_page_nav(cyd_display_screen_t *screen,
                                       const char *page_title,
                                       size_t page_index,
                                       size_t page_count,
                                       uint16_t prev_page_action_id,
                                       uint16_t next_page_action_id)
{
    char page_line[CYD_DISPLAY_TEXT_MAX_LEN + 1] = { 0 };

    ESP_RETURN_ON_FALSE(screen != NULL, ESP_ERR_INVALID_ARG, TAG, "screen is null");
    ESP_RETURN_ON_FALSE(page_title != NULL, ESP_ERR_INVALID_ARG, TAG, "page title is null");
    ESP_RETURN_ON_FALSE(page_count > 0 && page_index < page_count,
                        ESP_ERR_INVALID_ARG,
                        TAG,
                        "page_index %u out of range for %u pages",
                        (unsigned)page_index,
                        (unsigned)page_count);

    /* Navigation stops at the ends; the buttons render disabled there. */
    cyd_ui_add_styled_button(screen,
                             "前へ",
                             CYD_UI_SETTINGS_PAGE_PREV_COL,
                             CYD_UI_SETTINGS_PAGE_ROW,
                             CYD_UI_SETTINGS_PAGE_BUTTON_SPAN_COLS,
                             CYD_UI_SETTINGS_PAGE_SPAN_ROWS,
                             CYD_DISPLAY_FONT_BODY_BOLD,
                             CYD_UI_THEME_PRIMARY_SOFT,
                             CYD_UI_THEME_SURFACE,
                             CYD_UI_THEME_PRIMARY,
                             CYD_UI_THEME_BORDER_PX,
                             prev_page_action_id,
                             page_index > 0);
    /* The page title is cut on a character boundary, never the "n/N". */
    char count[24] = { 0 };
    snprintf(count, sizeof(count), " %u/%u", (unsigned)page_index + 1U, (unsigned)page_count);
    const size_t count_len = strlen(count);
    (void)cyd_display_utf8_copy(page_line, sizeof(page_line) - count_len, page_title);
    memcpy(page_line + strlen(page_line), count, count_len + 1U);
    cyd_ui_add_label(screen,
                     page_line,
                     CYD_UI_SETTINGS_PAGE_LABEL_COL,
                     CYD_UI_SETTINGS_PAGE_ROW,
                     CYD_UI_SETTINGS_PAGE_LABEL_SPAN_COLS,
                     CYD_UI_SETTINGS_PAGE_SPAN_ROWS,
                     CYD_DISPLAY_ALIGN_CENTER,
                     CYD_DISPLAY_FONT_BODY,
                     CYD_UI_THEME_SUBTEXT);
    cyd_ui_add_styled_button(screen,
                             "次へ",
                             CYD_UI_SETTINGS_PAGE_NEXT_COL,
                             CYD_UI_SETTINGS_PAGE_ROW,
                             CYD_UI_SETTINGS_PAGE_BUTTON_SPAN_COLS,
                             CYD_UI_SETTINGS_PAGE_SPAN_ROWS,
                             CYD_DISPLAY_FONT_BODY_BOLD,
                             CYD_UI_THEME_PRIMARY_SOFT,
                             CYD_UI_THEME_SURFACE,
                             CYD_UI_THEME_PRIMARY,
                             CYD_UI_THEME_BORDER_PX,
                             next_page_action_id,
                             page_index + 1U < page_count);
    return ESP_OK;
}

esp_err_t cyd_ui_add_settings_chrome(cyd_display_screen_t *screen,
                                     const cyd_ui_settings_chrome_t *chrome)
{
    ESP_RETURN_ON_FALSE(screen != NULL, ESP_ERR_INVALID_ARG, TAG, "screen is null");
    ESP_RETURN_ON_FALSE(chrome != NULL, ESP_ERR_INVALID_ARG, TAG, "chrome is null");

    /* The title draws the header band, so it goes first. */
    cyd_ui_add_settings_title(screen, chrome->app_title);
    cyd_ui_add_settings_back(screen, chrome->back_action_id);
    return cyd_ui_add_settings_page_nav(screen,
                                        chrome->page_title,
                                        chrome->page_index,
                                        chrome->page_count,
                                        chrome->prev_page_action_id,
                                        chrome->next_page_action_id);
}
