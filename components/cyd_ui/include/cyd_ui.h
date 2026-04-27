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
esp_err_t cyd_ui_submit(const cyd_display_screen_t *screen);

#ifdef __cplusplus
}
#endif

#endif
