#include <string.h>
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
        .fg_color = fg_color,
        .bg_color = bg_color,
        .border_color = border_color,
        .action_id = action_id,
        .enabled = enabled,
    };
    cyd_ui_copy_text(widget.text, sizeof(widget.text), text);
    return cyd_ui_add_widget(screen, &widget);
}

esp_err_t cyd_ui_submit(const cyd_display_screen_t *screen)
{
    return cyd_display_submit_screen(screen);
}
