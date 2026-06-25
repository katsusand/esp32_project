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
} cyd_display_widget_type_t;

typedef enum {
    CYD_DISPLAY_ALIGN_LEFT = 0,
    CYD_DISPLAY_ALIGN_CENTER,
    CYD_DISPLAY_ALIGN_RIGHT,
} cyd_display_align_t;

typedef struct {
    const uint8_t *data;
    uint8_t width_px;
    uint8_t height_px;
} cyd_display_bitmap_t;

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
    char text[CYD_DISPLAY_TEXT_MAX_LEN + 1];
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
