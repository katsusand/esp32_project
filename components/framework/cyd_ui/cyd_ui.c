#include <stdio.h>
#include <string.h>
#include "esp_check.h"
#include "esp_log.h"
#include "cyd_ui.h"

#define TAG "cyd_ui"

static void cyd_ui_copy_text(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || dst_size == 0) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
}

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

static uint16_t cyd_ui_stepper_row_button_fg_color(const cyd_ui_stepper_row_t *row)
{
    return row->has_button_fg_color ? row->button_fg_color : CYD_UI_COLOR_WHITE;
}

static uint16_t cyd_ui_stepper_row_button_bg_color(const cyd_ui_stepper_row_t *row)
{
    return row->has_button_bg_color ? row->button_bg_color : CYD_UI_COLOR_BLUE;
}

static uint16_t cyd_ui_stepper_row_button_border_color(const cyd_ui_stepper_row_t *row)
{
    return row->has_button_border_color ? row->button_border_color : CYD_UI_COLOR_CYAN;
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
    cyd_ui_copy_text(widget.text, sizeof(widget.text), text);
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
    cyd_ui_copy_text(widget.text, sizeof(widget.text), text);
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

esp_err_t cyd_ui_add_stepper_row(cyd_display_screen_t *screen,
                                 const cyd_ui_stepper_row_t *row)
{
    ESP_RETURN_ON_FALSE(screen != NULL, ESP_ERR_INVALID_ARG, "cyd_ui", "screen is null");
    ESP_RETURN_ON_FALSE(row != NULL, ESP_ERR_INVALID_ARG, "cyd_ui", "row is null");
    ESP_RETURN_ON_FALSE(row->label_text != NULL, ESP_ERR_INVALID_ARG, "cyd_ui", "row label is null");
    ESP_RETURN_ON_FALSE(row->value_text != NULL, ESP_ERR_INVALID_ARG, "cyd_ui", "row value is null");

    ESP_RETURN_ON_FALSE(cyd_ui_add_text(screen,
                                        row->label_text,
                                        row->label_col,
                                        row->row,
                                        row->label_span_cols,
                                        row->button_span_rows,
                                        CYD_DISPLAY_ALIGN_LEFT,
                                        row->label_scale > 0 ? row->label_scale : 1,
                                        CYD_UI_COLOR_WHITE),
                        ESP_ERR_NO_MEM,
                        "cyd_ui",
                        "add stepper label failed");
    ESP_RETURN_ON_FALSE(cyd_ui_add_text(screen,
                                        row->value_text,
                                        row->value_col,
                                        row->row,
                                        row->value_span_cols,
                                        row->button_span_rows,
                                        CYD_DISPLAY_ALIGN_CENTER,
                                        row->value_scale > 0 ? row->value_scale : 1,
                                        CYD_UI_COLOR_WHITE),
                        ESP_ERR_NO_MEM,
                        "cyd_ui",
                        "add stepper value failed");
    ESP_RETURN_ON_FALSE(cyd_ui_add_button_with_fg_enabled(screen,
                                                          "-",
                                                          row->button_left_col,
                                                          row->row,
                                                          row->button_span_cols,
                                                          row->button_span_rows,
                                                          cyd_ui_stepper_row_button_fg_color(row),
                                                          cyd_ui_stepper_row_button_bg_color(row),
                                                          cyd_ui_stepper_row_button_border_color(row),
                                                          row->decrease_action_id,
                                                          row->can_decrease),
                        ESP_ERR_NO_MEM,
                        "cyd_ui",
                        "add stepper decrease button failed");
    screen->widgets[screen->widget_count - 1].scale_x = row->button_scale > 0 ? row->button_scale : 1;
    screen->widgets[screen->widget_count - 1].scale_y = row->button_scale > 0 ? row->button_scale : 1;
    ESP_RETURN_ON_FALSE(cyd_ui_add_button_with_fg_enabled(screen,
                                                          "+",
                                                          row->button_right_col,
                                                          row->row,
                                                          row->button_span_cols,
                                                          row->button_span_rows,
                                                          cyd_ui_stepper_row_button_fg_color(row),
                                                          cyd_ui_stepper_row_button_bg_color(row),
                                                          cyd_ui_stepper_row_button_border_color(row),
                                                          row->increase_action_id,
                                                          row->can_increase),
                        ESP_ERR_NO_MEM,
                        "cyd_ui",
                        "add stepper increase button failed");
    screen->widgets[screen->widget_count - 1].scale_x = row->button_scale > 0 ? row->button_scale : 1;
    screen->widgets[screen->widget_count - 1].scale_y = row->button_scale > 0 ? row->button_scale : 1;
    return ESP_OK;
}

esp_err_t cyd_ui_submit(const cyd_display_screen_t *screen)
{
    return cyd_display_submit_screen(screen);
}

/* Settings chrome geometry. The single definition of this layout. */
#define CYD_UI_SETTINGS_BACK_COL 0
#define CYD_UI_SETTINGS_BACK_ROW 0
#define CYD_UI_SETTINGS_BACK_SPAN_COLS 6
#define CYD_UI_SETTINGS_BACK_SPAN_ROWS 3
#define CYD_UI_SETTINGS_TITLE_COL 8
#define CYD_UI_SETTINGS_TITLE_ROW 0
#define CYD_UI_SETTINGS_TITLE_SPAN_COLS 32
#define CYD_UI_SETTINGS_TITLE_SPAN_ROWS 2
#define CYD_UI_SETTINGS_TITLE_SCALE 2
#define CYD_UI_SETTINGS_PAGE_PREV_COL 2
#define CYD_UI_SETTINGS_PAGE_NEXT_COL 31
#define CYD_UI_SETTINGS_PAGE_BUTTON_ROW 27
#define CYD_UI_SETTINGS_PAGE_BUTTON_SPAN_COLS 7
#define CYD_UI_SETTINGS_PAGE_BUTTON_SPAN_ROWS 3
#define CYD_UI_SETTINGS_PAGE_LABEL_COL 12
#define CYD_UI_SETTINGS_PAGE_LABEL_ROW 27
#define CYD_UI_SETTINGS_PAGE_LABEL_SPAN_COLS 16
#define CYD_UI_SETTINGS_PAGE_LABEL_SPAN_ROWS 3

void cyd_ui_add_settings_title(cyd_display_screen_t *screen, const char *app_title)
{
    if (screen == NULL || app_title == NULL) {
        return;
    }
    cyd_ui_add_text(screen,
                    app_title,
                    CYD_UI_SETTINGS_TITLE_COL,
                    CYD_UI_SETTINGS_TITLE_ROW,
                    CYD_UI_SETTINGS_TITLE_SPAN_COLS,
                    CYD_UI_SETTINGS_TITLE_SPAN_ROWS,
                    CYD_DISPLAY_ALIGN_RIGHT,
                    CYD_UI_SETTINGS_TITLE_SCALE,
                    CYD_UI_COLOR_CYAN);
}

void cyd_ui_add_settings_back(cyd_display_screen_t *screen, uint16_t back_action_id)
{
    if (screen == NULL) {
        return;
    }
    cyd_ui_add_button(screen,
                      "<<",
                      CYD_UI_SETTINGS_BACK_COL,
                      CYD_UI_SETTINGS_BACK_ROW,
                      CYD_UI_SETTINGS_BACK_SPAN_COLS,
                      CYD_UI_SETTINGS_BACK_SPAN_ROWS,
                      CYD_UI_COLOR_BLUE,
                      CYD_UI_COLOR_CYAN,
                      back_action_id);
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

    /* Navigation stops at the ends; the arrows render disabled there. */
    cyd_ui_add_button_with_fg_enabled(screen,
                                      "<",
                                      CYD_UI_SETTINGS_PAGE_PREV_COL,
                                      CYD_UI_SETTINGS_PAGE_BUTTON_ROW,
                                      CYD_UI_SETTINGS_PAGE_BUTTON_SPAN_COLS,
                                      CYD_UI_SETTINGS_PAGE_BUTTON_SPAN_ROWS,
                                      CYD_UI_COLOR_WHITE,
                                      CYD_UI_COLOR_BLUE,
                                      CYD_UI_COLOR_CYAN,
                                      prev_page_action_id,
                                      page_index > 0);
    snprintf(page_line,
             sizeof(page_line),
             "%s  %u/%u",
             page_title,
             (unsigned)page_index + 1U,
             (unsigned)page_count);
    cyd_ui_add_text(screen,
                    page_line,
                    CYD_UI_SETTINGS_PAGE_LABEL_COL,
                    CYD_UI_SETTINGS_PAGE_LABEL_ROW,
                    CYD_UI_SETTINGS_PAGE_LABEL_SPAN_COLS,
                    CYD_UI_SETTINGS_PAGE_LABEL_SPAN_ROWS,
                    CYD_DISPLAY_ALIGN_CENTER,
                    1,
                    CYD_UI_COLOR_LIGHTGREY);
    cyd_ui_add_button_with_fg_enabled(screen,
                                      ">",
                                      CYD_UI_SETTINGS_PAGE_NEXT_COL,
                                      CYD_UI_SETTINGS_PAGE_BUTTON_ROW,
                                      CYD_UI_SETTINGS_PAGE_BUTTON_SPAN_COLS,
                                      CYD_UI_SETTINGS_PAGE_BUTTON_SPAN_ROWS,
                                      CYD_UI_COLOR_WHITE,
                                      CYD_UI_COLOR_BLUE,
                                      CYD_UI_COLOR_CYAN,
                                      next_page_action_id,
                                      page_index + 1U < page_count);
    return ESP_OK;
}

esp_err_t cyd_ui_add_settings_chrome(cyd_display_screen_t *screen,
                                     const cyd_ui_settings_chrome_t *chrome)
{
    ESP_RETURN_ON_FALSE(screen != NULL, ESP_ERR_INVALID_ARG, TAG, "screen is null");
    ESP_RETURN_ON_FALSE(chrome != NULL, ESP_ERR_INVALID_ARG, TAG, "chrome is null");

    cyd_ui_add_settings_back(screen, chrome->back_action_id);
    cyd_ui_add_settings_title(screen, chrome->app_title);
    return cyd_ui_add_settings_page_nav(screen,
                                        chrome->page_title,
                                        chrome->page_index,
                                        chrome->page_count,
                                        chrome->prev_page_action_id,
                                        chrome->next_page_action_id);
}
