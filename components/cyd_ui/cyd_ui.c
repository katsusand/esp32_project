#include <string.h>
#include "esp_check.h"
#include "cyd_ui.h"

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
