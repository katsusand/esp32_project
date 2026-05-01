#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "esp_check.h"
#include "esp_log.h"
#include "app_shell.h"
#include "cyd_alarm.h"
#include "cyd_clock_settings_app.h"
#include "cyd_display.h"
#include "cyd_input.h"
#include "cyd_ui.h"

#define TAG "cyd_clock_settings"
#define CYD_CLOCK_SETTINGS_INPUT_POLL_MS 50
#define CYD_CLOCK_SETTINGS_ACTION_BACK 0x3101
#define CYD_CLOCK_SETTINGS_ACTION_PREV_PAGE 0x3102
#define CYD_CLOCK_SETTINGS_ACTION_NEXT_PAGE 0x3103
#define CYD_CLOCK_SETTINGS_ACTION_ALARM1_HOUR_DOWN 0x3104
#define CYD_CLOCK_SETTINGS_ACTION_ALARM1_HOUR_UP 0x3105
#define CYD_CLOCK_SETTINGS_ACTION_ALARM1_MINUTE_DOWN 0x3106
#define CYD_CLOCK_SETTINGS_ACTION_ALARM1_MINUTE_UP 0x3107
#define CYD_CLOCK_SETTINGS_ACTION_ALARM2_HOUR_DOWN 0x3108
#define CYD_CLOCK_SETTINGS_ACTION_ALARM2_HOUR_UP 0x3109
#define CYD_CLOCK_SETTINGS_ACTION_ALARM2_MINUTE_DOWN 0x310a
#define CYD_CLOCK_SETTINGS_ACTION_ALARM2_MINUTE_UP 0x310b
#define CYD_CLOCK_SETTINGS_ACTION_ALARM1_WEEKDAY_SUN 0x310c
#define CYD_CLOCK_SETTINGS_ACTION_ALARM1_WEEKDAY_MON 0x310d
#define CYD_CLOCK_SETTINGS_ACTION_ALARM1_WEEKDAY_TUE 0x310e
#define CYD_CLOCK_SETTINGS_ACTION_ALARM1_WEEKDAY_WED 0x310f
#define CYD_CLOCK_SETTINGS_ACTION_ALARM1_WEEKDAY_THU 0x3110
#define CYD_CLOCK_SETTINGS_ACTION_ALARM1_WEEKDAY_FRI 0x3111
#define CYD_CLOCK_SETTINGS_ACTION_ALARM1_WEEKDAY_SAT 0x3112
#define CYD_CLOCK_SETTINGS_BACK_COL 0
#define CYD_CLOCK_SETTINGS_BACK_ROW 0
#define CYD_CLOCK_SETTINGS_BACK_SPAN_COLS 6
#define CYD_CLOCK_SETTINGS_BACK_SPAN_ROWS 3
#define CYD_CLOCK_SETTINGS_TITLE_COL 8
#define CYD_CLOCK_SETTINGS_TITLE_ROW 0
#define CYD_CLOCK_SETTINGS_TITLE_SPAN_COLS 32
#define CYD_CLOCK_SETTINGS_TITLE_SPAN_ROWS 2
#define CYD_CLOCK_SETTINGS_PAGE_BUTTON_ROW 26
#define CYD_CLOCK_SETTINGS_PAGE_BUTTON_SPAN_COLS 7
#define CYD_CLOCK_SETTINGS_PAGE_BUTTON_SPAN_ROWS 3
#define CYD_CLOCK_SETTINGS_PAGE_LABEL_COL 12
#define CYD_CLOCK_SETTINGS_PAGE_LABEL_ROW 25
#define CYD_CLOCK_SETTINGS_PAGE_LABEL_SPAN_COLS 16
#define CYD_CLOCK_SETTINGS_PAGE_LABEL_SPAN_ROWS 3
#define CYD_CLOCK_SETTINGS_ITEM_LABEL_COL 2
#define CYD_CLOCK_SETTINGS_ITEM_LABEL_SPAN_COLS 16
#define CYD_CLOCK_SETTINGS_ITEM_VALUE_COL 26
#define CYD_CLOCK_SETTINGS_ITEM_VALUE_SPAN_COLS 8
#define CYD_CLOCK_SETTINGS_ITEM_BUTTON_LEFT_COL 21
#define CYD_CLOCK_SETTINGS_ITEM_BUTTON_RIGHT_COL 35
#define CYD_CLOCK_SETTINGS_ITEM_BUTTON_SPAN_COLS 3
#define CYD_CLOCK_SETTINGS_ITEM_BUTTON_SPAN_ROWS 2
#define CYD_CLOCK_SETTINGS_ITEM_BUTTON_SCALE 1

typedef enum {
    CYD_CLOCK_SETTINGS_PAGE_ALARM1 = 0,
    CYD_CLOCK_SETTINGS_PAGE_ALARM2,
    CYD_CLOCK_SETTINGS_PAGE_COUNT,
} cyd_clock_settings_page_t;

typedef struct {
    bool pending;
    bool long_pressed;
    uint16_t action_id;
} cyd_clock_settings_touch_tracker_t;

static cyd_display_screen_t s_clock_settings_screen;
static const app_shell_app_t *s_clock_settings_return_app;
static cyd_clock_settings_page_t s_clock_settings_page = CYD_CLOCK_SETTINGS_PAGE_ALARM1;
static cyd_clock_settings_touch_tracker_t s_clock_settings_touch_tracker;

static bool cyd_clock_settings_touch_confirmed_action(const cyd_input_event_t *event,
                                                      cyd_clock_settings_touch_tracker_t *tracker,
                                                      uint16_t *action_id)
{
    if (event == NULL || tracker == NULL || event->type != CYD_INPUT_EVENT_TOUCH) {
        return false;
    }

    switch (event->data.touch.action) {
    case CYD_INPUT_TOUCH_ACTION_PRESS:
        tracker->pending = cyd_display_hit_test_action(event->data.touch.x,
                                                       event->data.touch.y,
                                                       &tracker->action_id);
        tracker->long_pressed = false;
        return false;
    case CYD_INPUT_TOUCH_ACTION_LONG_PRESS:
    case CYD_INPUT_TOUCH_ACTION_REPEAT:
        if (tracker->pending) {
            tracker->long_pressed = true;
        }
        return false;
    case CYD_INPUT_TOUCH_ACTION_RELEASE: {
        uint16_t release_action_id = 0;
        bool confirmed = tracker->pending &&
                         !tracker->long_pressed &&
                         cyd_display_hit_test_action(event->data.touch.x,
                                                     event->data.touch.y,
                                                     &release_action_id) &&
                         release_action_id == tracker->action_id;
        if (confirmed && action_id != NULL) {
            *action_id = release_action_id;
        }
        tracker->pending = false;
        tracker->long_pressed = false;
        tracker->action_id = 0;
        return confirmed;
    }
    default:
        return false;
    }
}

static bool cyd_clock_settings_is_stepper_action(uint16_t action_id)
{
    switch (action_id) {
    case CYD_CLOCK_SETTINGS_ACTION_ALARM1_HOUR_DOWN:
    case CYD_CLOCK_SETTINGS_ACTION_ALARM1_HOUR_UP:
    case CYD_CLOCK_SETTINGS_ACTION_ALARM1_MINUTE_DOWN:
    case CYD_CLOCK_SETTINGS_ACTION_ALARM1_MINUTE_UP:
    case CYD_CLOCK_SETTINGS_ACTION_ALARM2_HOUR_DOWN:
    case CYD_CLOCK_SETTINGS_ACTION_ALARM2_HOUR_UP:
    case CYD_CLOCK_SETTINGS_ACTION_ALARM2_MINUTE_DOWN:
    case CYD_CLOCK_SETTINGS_ACTION_ALARM2_MINUTE_UP:
        return true;
    default:
        return false;
    }
}

static bool cyd_clock_settings_touch_stepper_action(const cyd_input_event_t *event, uint16_t *action_id)
{
    uint16_t pressed_action_id = 0;

    if (event == NULL || action_id == NULL || event->type != CYD_INPUT_EVENT_TOUCH) {
        return false;
    }

    if (event->data.touch.action != CYD_INPUT_TOUCH_ACTION_PRESS &&
        event->data.touch.action != CYD_INPUT_TOUCH_ACTION_REPEAT) {
        return false;
    }

    if (!cyd_display_hit_test_action(event->data.touch.x, event->data.touch.y, &pressed_action_id)) {
        return false;
    }

    if (!cyd_clock_settings_is_stepper_action(pressed_action_id)) {
        return false;
    }

    *action_id = pressed_action_id;
    return true;
}

static const char *cyd_clock_settings_page_title(cyd_clock_settings_page_t page)
{
    switch (page) {
    case CYD_CLOCK_SETTINGS_PAGE_ALARM1:
        return "ALARM1";
    case CYD_CLOCK_SETTINGS_PAGE_ALARM2:
        return "ALARM2";
    default:
        return "CLOCK";
    }
}

static esp_err_t cyd_clock_settings_add_frame(cyd_display_screen_t *screen)
{
    char page_line[CYD_DISPLAY_TEXT_MAX_LEN + 1] = { 0 };
    bool can_go_prev = s_clock_settings_page > CYD_CLOCK_SETTINGS_PAGE_ALARM1;
    bool can_go_next = (size_t)s_clock_settings_page + 1 < CYD_CLOCK_SETTINGS_PAGE_COUNT;

    cyd_ui_add_button(screen,
                      "<<",
                      CYD_CLOCK_SETTINGS_BACK_COL,
                      CYD_CLOCK_SETTINGS_BACK_ROW,
                      CYD_CLOCK_SETTINGS_BACK_SPAN_COLS,
                      CYD_CLOCK_SETTINGS_BACK_SPAN_ROWS,
                      CYD_UI_COLOR_BLUE,
                      CYD_UI_COLOR_CYAN,
                      CYD_CLOCK_SETTINGS_ACTION_BACK);
    cyd_ui_add_text(screen,
                    "CLOCK SETTINGS",
                    CYD_CLOCK_SETTINGS_TITLE_COL,
                    CYD_CLOCK_SETTINGS_TITLE_ROW,
                    CYD_CLOCK_SETTINGS_TITLE_SPAN_COLS,
                    CYD_CLOCK_SETTINGS_TITLE_SPAN_ROWS,
                    CYD_DISPLAY_ALIGN_RIGHT,
                    2,
                    CYD_UI_COLOR_CYAN);

    snprintf(page_line,
             sizeof(page_line),
             "%s  %u/%u",
             cyd_clock_settings_page_title(s_clock_settings_page),
             (unsigned)s_clock_settings_page + 1U,
             (unsigned)CYD_CLOCK_SETTINGS_PAGE_COUNT);
    cyd_ui_add_button_with_fg_enabled(screen,
                                      "<",
                                      2,
                                      CYD_CLOCK_SETTINGS_PAGE_BUTTON_ROW,
                                      CYD_CLOCK_SETTINGS_PAGE_BUTTON_SPAN_COLS,
                                      CYD_CLOCK_SETTINGS_PAGE_BUTTON_SPAN_ROWS,
                                      CYD_UI_COLOR_WHITE,
                                      CYD_UI_COLOR_BLUE,
                                      CYD_UI_COLOR_CYAN,
                                      CYD_CLOCK_SETTINGS_ACTION_PREV_PAGE,
                                      can_go_prev);
    cyd_ui_add_text(screen,
                    page_line,
                    CYD_CLOCK_SETTINGS_PAGE_LABEL_COL,
                    CYD_CLOCK_SETTINGS_PAGE_LABEL_ROW,
                    CYD_CLOCK_SETTINGS_PAGE_LABEL_SPAN_COLS,
                    CYD_CLOCK_SETTINGS_PAGE_LABEL_SPAN_ROWS,
                    CYD_DISPLAY_ALIGN_CENTER,
                    1,
                    CYD_UI_COLOR_LIGHTGREY);
    cyd_ui_add_button_with_fg_enabled(screen,
                                      ">",
                                      31,
                                      CYD_CLOCK_SETTINGS_PAGE_BUTTON_ROW,
                                      CYD_CLOCK_SETTINGS_PAGE_BUTTON_SPAN_COLS,
                                      CYD_CLOCK_SETTINGS_PAGE_BUTTON_SPAN_ROWS,
                                      CYD_UI_COLOR_WHITE,
                                      CYD_UI_COLOR_BLUE,
                                      CYD_UI_COLOR_CYAN,
                                      CYD_CLOCK_SETTINGS_ACTION_NEXT_PAGE,
                                      can_go_next);
    return ESP_OK;
}

static uint8_t cyd_clock_settings_wrap_u8_down(uint8_t value, uint8_t min_value, uint8_t max_value)
{
    return (value <= min_value) ? max_value : (uint8_t)(value - 1U);
}

static uint8_t cyd_clock_settings_wrap_u8_up(uint8_t value, uint8_t min_value, uint8_t max_value)
{
    return (value >= max_value) ? min_value : (uint8_t)(value + 1U);
}

static esp_err_t cyd_clock_settings_render_alarm_page(cyd_display_screen_t *screen, cyd_alarm_id_t alarm_id)
{
    char hour_value[CYD_DISPLAY_TEXT_MAX_LEN + 1] = { 0 };
    char minute_value[CYD_DISPLAY_TEXT_MAX_LEN + 1] = { 0 };
    cyd_ui_stepper_row_t rows[2] = { 0 };
    cyd_alarm_config_t alarm = { 0 };
    uint16_t hour_down_action = alarm_id == CYD_ALARM_ID_1 ? CYD_CLOCK_SETTINGS_ACTION_ALARM1_HOUR_DOWN
                                                           : CYD_CLOCK_SETTINGS_ACTION_ALARM2_HOUR_DOWN;
    uint16_t hour_up_action = alarm_id == CYD_ALARM_ID_1 ? CYD_CLOCK_SETTINGS_ACTION_ALARM1_HOUR_UP
                                                         : CYD_CLOCK_SETTINGS_ACTION_ALARM2_HOUR_UP;
    uint16_t minute_down_action = alarm_id == CYD_ALARM_ID_1 ? CYD_CLOCK_SETTINGS_ACTION_ALARM1_MINUTE_DOWN
                                                             : CYD_CLOCK_SETTINGS_ACTION_ALARM2_MINUTE_DOWN;
    uint16_t minute_up_action = alarm_id == CYD_ALARM_ID_1 ? CYD_CLOCK_SETTINGS_ACTION_ALARM1_MINUTE_UP
                                                           : CYD_CLOCK_SETTINGS_ACTION_ALARM2_MINUTE_UP;

    ESP_RETURN_ON_ERROR(cyd_alarm_get_config(alarm_id, &alarm), TAG, "load alarm config failed");

    snprintf(hour_value, sizeof(hour_value), "%02u", (unsigned)alarm.hour);
    snprintf(minute_value, sizeof(minute_value), "%02u", (unsigned)alarm.minute);
    if (alarm_id == CYD_ALARM_ID_1) {
        static const struct {
            const char *label;
            uint8_t mask;
            uint8_t col;
            uint8_t span_cols;
            uint16_t action_id;
        } weekday_buttons[] = {
            { "SUN", CYD_ALARM_WEEKDAY_SUNDAY, 0, 5, CYD_CLOCK_SETTINGS_ACTION_ALARM1_WEEKDAY_SUN },
            { "MON", CYD_ALARM_WEEKDAY_MONDAY, 5, 6, CYD_CLOCK_SETTINGS_ACTION_ALARM1_WEEKDAY_MON },
            { "TUE", CYD_ALARM_WEEKDAY_TUESDAY, 11, 5, CYD_CLOCK_SETTINGS_ACTION_ALARM1_WEEKDAY_TUE },
            { "WED", CYD_ALARM_WEEKDAY_WEDNESDAY, 16, 6, CYD_CLOCK_SETTINGS_ACTION_ALARM1_WEEKDAY_WED },
            { "THU", CYD_ALARM_WEEKDAY_THURSDAY, 22, 6, CYD_CLOCK_SETTINGS_ACTION_ALARM1_WEEKDAY_THU },
            { "FRI", CYD_ALARM_WEEKDAY_FRIDAY, 28, 5, CYD_CLOCK_SETTINGS_ACTION_ALARM1_WEEKDAY_FRI },
            { "SAT", CYD_ALARM_WEEKDAY_SATURDAY, 33, 5, CYD_CLOCK_SETTINGS_ACTION_ALARM1_WEEKDAY_SAT },
        };

        for (size_t i = 0; i < (sizeof(weekday_buttons) / sizeof(weekday_buttons[0])); ++i) {
            bool selected = (alarm.weekday_mask & weekday_buttons[i].mask) != 0;

            cyd_ui_add_button_with_fg(screen,
                                      weekday_buttons[i].label,
                                      weekday_buttons[i].col,
                                      7,
                                      weekday_buttons[i].span_cols,
                                      2,
                                      CYD_UI_COLOR_WHITE,
                                      selected ? CYD_UI_COLOR_BLUE : CYD_UI_COLOR_DIMGREY,
                                      selected ? CYD_UI_COLOR_CYAN : CYD_UI_COLOR_LIGHTGREY,
                                      weekday_buttons[i].action_id);
        }
    }

    rows[0] = (cyd_ui_stepper_row_t){
        .label_text = "Hour:",
        .value_text = hour_value,
        .row = alarm_id == CYD_ALARM_ID_1 ? 12 : 10,
        .label_col = CYD_CLOCK_SETTINGS_ITEM_LABEL_COL,
        .label_span_cols = CYD_CLOCK_SETTINGS_ITEM_LABEL_SPAN_COLS,
        .label_scale = 1,
        .value_col = CYD_CLOCK_SETTINGS_ITEM_VALUE_COL,
        .value_span_cols = CYD_CLOCK_SETTINGS_ITEM_VALUE_SPAN_COLS,
        .value_scale = 2,
        .button_left_col = CYD_CLOCK_SETTINGS_ITEM_BUTTON_LEFT_COL,
        .button_right_col = CYD_CLOCK_SETTINGS_ITEM_BUTTON_RIGHT_COL,
        .button_span_cols = CYD_CLOCK_SETTINGS_ITEM_BUTTON_SPAN_COLS,
        .button_span_rows = CYD_CLOCK_SETTINGS_ITEM_BUTTON_SPAN_ROWS,
        .button_scale = CYD_CLOCK_SETTINGS_ITEM_BUTTON_SCALE,
        .has_button_fg_color = true,
        .button_fg_color = CYD_UI_COLOR_BLACK,
        .has_button_bg_color = true,
        .button_bg_color = CYD_UI_COLOR_GREEN,
        .has_button_border_color = true,
        .button_border_color = CYD_UI_COLOR_LIGHTGREY,
        .decrease_action_id = hour_down_action,
        .increase_action_id = hour_up_action,
        .can_decrease = true,
        .can_increase = true,
    };
    rows[1] = (cyd_ui_stepper_row_t){
        .label_text = "Minute:",
        .value_text = minute_value,
        .row = alarm_id == CYD_ALARM_ID_1 ? 17 : 15,
        .label_col = CYD_CLOCK_SETTINGS_ITEM_LABEL_COL,
        .label_span_cols = CYD_CLOCK_SETTINGS_ITEM_LABEL_SPAN_COLS,
        .label_scale = 1,
        .value_col = CYD_CLOCK_SETTINGS_ITEM_VALUE_COL,
        .value_span_cols = CYD_CLOCK_SETTINGS_ITEM_VALUE_SPAN_COLS,
        .value_scale = 2,
        .button_left_col = CYD_CLOCK_SETTINGS_ITEM_BUTTON_LEFT_COL,
        .button_right_col = CYD_CLOCK_SETTINGS_ITEM_BUTTON_RIGHT_COL,
        .button_span_cols = CYD_CLOCK_SETTINGS_ITEM_BUTTON_SPAN_COLS,
        .button_span_rows = CYD_CLOCK_SETTINGS_ITEM_BUTTON_SPAN_ROWS,
        .button_scale = CYD_CLOCK_SETTINGS_ITEM_BUTTON_SCALE,
        .has_button_fg_color = true,
        .button_fg_color = CYD_UI_COLOR_BLACK,
        .has_button_bg_color = true,
        .button_bg_color = CYD_UI_COLOR_GREEN,
        .has_button_border_color = true,
        .button_border_color = CYD_UI_COLOR_LIGHTGREY,
        .decrease_action_id = minute_down_action,
        .increase_action_id = minute_up_action,
        .can_decrease = true,
        .can_increase = true,
    };

    for (size_t i = 0; i < (sizeof(rows) / sizeof(rows[0])); ++i) {
        ESP_RETURN_ON_ERROR(cyd_ui_add_stepper_row(screen, &rows[i]), TAG, "add alarm settings row failed");
    }

    return ESP_OK;
}

static esp_err_t cyd_clock_settings_show(void)
{
    cyd_display_screen_t *screen = &s_clock_settings_screen;

    cyd_ui_screen_clear(screen);
    ESP_RETURN_ON_ERROR(cyd_clock_settings_add_frame(screen), TAG, "add frame failed");

    if (s_clock_settings_page == CYD_CLOCK_SETTINGS_PAGE_ALARM1) {
        ESP_RETURN_ON_ERROR(cyd_clock_settings_render_alarm_page(screen, CYD_ALARM_ID_1),
                            TAG,
                            "render alarm1 page failed");
    } else {
        ESP_RETURN_ON_ERROR(cyd_clock_settings_render_alarm_page(screen, CYD_ALARM_ID_2),
                            TAG,
                            "render alarm2 page failed");
    }

    return cyd_ui_submit(screen);
}

static esp_err_t cyd_clock_settings_handle_alarm_action(uint16_t action_id, bool *handled)
{
    cyd_alarm_config_t alarm1 = { 0 };
    cyd_alarm_config_t alarm2 = { 0 };

    ESP_RETURN_ON_FALSE(handled != NULL, ESP_ERR_INVALID_ARG, TAG, "handled is null");
    *handled = false;

    ESP_RETURN_ON_ERROR(cyd_alarm_get_config(CYD_ALARM_ID_1, &alarm1), TAG, "get alarm1 config failed");
    ESP_RETURN_ON_ERROR(cyd_alarm_get_config(CYD_ALARM_ID_2, &alarm2), TAG, "get alarm2 config failed");

    if (action_id == CYD_CLOCK_SETTINGS_ACTION_ALARM1_HOUR_DOWN ||
        action_id == CYD_CLOCK_SETTINGS_ACTION_ALARM1_HOUR_UP) {
        alarm1.hour = (action_id == CYD_CLOCK_SETTINGS_ACTION_ALARM1_HOUR_DOWN)
                          ? cyd_clock_settings_wrap_u8_down(alarm1.hour, 0, 23)
                          : cyd_clock_settings_wrap_u8_up(alarm1.hour, 0, 23);
        ESP_RETURN_ON_ERROR(cyd_alarm_set_time(CYD_ALARM_ID_1, alarm1.hour, alarm1.minute),
                            TAG,
                            "set alarm1 hour failed");
        ESP_RETURN_ON_ERROR(cyd_clock_settings_show(), TAG, "refresh clock settings failed");
        *handled = true;
        return ESP_OK;
    }

    if (action_id == CYD_CLOCK_SETTINGS_ACTION_ALARM1_MINUTE_DOWN ||
        action_id == CYD_CLOCK_SETTINGS_ACTION_ALARM1_MINUTE_UP) {
        alarm1.minute = (action_id == CYD_CLOCK_SETTINGS_ACTION_ALARM1_MINUTE_DOWN)
                            ? cyd_clock_settings_wrap_u8_down(alarm1.minute, 0, 59)
                            : cyd_clock_settings_wrap_u8_up(alarm1.minute, 0, 59);
        ESP_RETURN_ON_ERROR(cyd_alarm_set_time(CYD_ALARM_ID_1, alarm1.hour, alarm1.minute),
                            TAG,
                            "set alarm1 minute failed");
        ESP_RETURN_ON_ERROR(cyd_clock_settings_show(), TAG, "refresh clock settings failed");
        *handled = true;
        return ESP_OK;
    }

    if (action_id == CYD_CLOCK_SETTINGS_ACTION_ALARM2_HOUR_DOWN ||
        action_id == CYD_CLOCK_SETTINGS_ACTION_ALARM2_HOUR_UP) {
        alarm2.hour = (action_id == CYD_CLOCK_SETTINGS_ACTION_ALARM2_HOUR_DOWN)
                          ? cyd_clock_settings_wrap_u8_down(alarm2.hour, 0, 23)
                          : cyd_clock_settings_wrap_u8_up(alarm2.hour, 0, 23);
        ESP_RETURN_ON_ERROR(cyd_alarm_set_time(CYD_ALARM_ID_2, alarm2.hour, alarm2.minute),
                            TAG,
                            "set alarm2 hour failed");
        ESP_RETURN_ON_ERROR(cyd_clock_settings_show(), TAG, "refresh clock settings failed");
        *handled = true;
        return ESP_OK;
    }

    if (action_id == CYD_CLOCK_SETTINGS_ACTION_ALARM2_MINUTE_DOWN ||
        action_id == CYD_CLOCK_SETTINGS_ACTION_ALARM2_MINUTE_UP) {
        alarm2.minute = (action_id == CYD_CLOCK_SETTINGS_ACTION_ALARM2_MINUTE_DOWN)
                            ? cyd_clock_settings_wrap_u8_down(alarm2.minute, 0, 59)
                            : cyd_clock_settings_wrap_u8_up(alarm2.minute, 0, 59);
        ESP_RETURN_ON_ERROR(cyd_alarm_set_time(CYD_ALARM_ID_2, alarm2.hour, alarm2.minute),
                            TAG,
                            "set alarm2 minute failed");
        ESP_RETURN_ON_ERROR(cyd_clock_settings_show(), TAG, "refresh clock settings failed");
        *handled = true;
        return ESP_OK;
    }

    if (s_clock_settings_page == CYD_CLOCK_SETTINGS_PAGE_ALARM1) {
        uint8_t toggle_mask = 0;

        switch (action_id) {
        case CYD_CLOCK_SETTINGS_ACTION_ALARM1_WEEKDAY_SUN:
            toggle_mask = CYD_ALARM_WEEKDAY_SUNDAY;
            break;
        case CYD_CLOCK_SETTINGS_ACTION_ALARM1_WEEKDAY_MON:
            toggle_mask = CYD_ALARM_WEEKDAY_MONDAY;
            break;
        case CYD_CLOCK_SETTINGS_ACTION_ALARM1_WEEKDAY_TUE:
            toggle_mask = CYD_ALARM_WEEKDAY_TUESDAY;
            break;
        case CYD_CLOCK_SETTINGS_ACTION_ALARM1_WEEKDAY_WED:
            toggle_mask = CYD_ALARM_WEEKDAY_WEDNESDAY;
            break;
        case CYD_CLOCK_SETTINGS_ACTION_ALARM1_WEEKDAY_THU:
            toggle_mask = CYD_ALARM_WEEKDAY_THURSDAY;
            break;
        case CYD_CLOCK_SETTINGS_ACTION_ALARM1_WEEKDAY_FRI:
            toggle_mask = CYD_ALARM_WEEKDAY_FRIDAY;
            break;
        case CYD_CLOCK_SETTINGS_ACTION_ALARM1_WEEKDAY_SAT:
            toggle_mask = CYD_ALARM_WEEKDAY_SATURDAY;
            break;
        default:
            break;
        }

        if (toggle_mask != 0) {
            alarm1.weekday_mask ^= toggle_mask;
            ESP_RETURN_ON_ERROR(cyd_alarm_set_alarm1_weekday_mask(alarm1.weekday_mask),
                                TAG,
                                "set alarm1 weekday mask failed");
            ESP_RETURN_ON_ERROR(cyd_clock_settings_show(), TAG, "refresh clock settings failed");
            *handled = true;
            return ESP_OK;
        }
    }

    return ESP_OK;
}

static esp_err_t cyd_clock_settings_enter(void *ctx, const app_shell_app_t *from_app)
{
    (void)ctx;

    ESP_RETURN_ON_FALSE(from_app != NULL, ESP_ERR_INVALID_STATE, TAG, "clock settings return app not set");
    s_clock_settings_return_app = from_app;
    s_clock_settings_page = CYD_CLOCK_SETTINGS_PAGE_ALARM1;
    s_clock_settings_touch_tracker = (cyd_clock_settings_touch_tracker_t){ 0 };
    return cyd_clock_settings_show();
}

static esp_err_t cyd_clock_settings_step(void *ctx)
{
    (void)ctx;

    cyd_input_event_t event = { 0 };
    if (cyd_input_read_event(&event, pdMS_TO_TICKS(CYD_CLOCK_SETTINGS_INPUT_POLL_MS)) == ESP_OK) {
        uint16_t action_id = 0;
        bool handled = false;

        if (cyd_clock_settings_touch_stepper_action(&event, &action_id)) {
            return cyd_clock_settings_handle_alarm_action(action_id, &handled);
        }

        if (!cyd_clock_settings_touch_confirmed_action(&event, &s_clock_settings_touch_tracker, &action_id)) {
            return ESP_OK;
        }

        if (action_id == CYD_CLOCK_SETTINGS_ACTION_PREV_PAGE &&
            s_clock_settings_page > CYD_CLOCK_SETTINGS_PAGE_ALARM1) {
            --s_clock_settings_page;
            return cyd_clock_settings_show();
        }
        if (action_id == CYD_CLOCK_SETTINGS_ACTION_NEXT_PAGE &&
            (size_t)s_clock_settings_page + 1 < CYD_CLOCK_SETTINGS_PAGE_COUNT) {
            s_clock_settings_page = (cyd_clock_settings_page_t)((size_t)s_clock_settings_page + 1U);
            return cyd_clock_settings_show();
        }
        if (action_id == CYD_CLOCK_SETTINGS_ACTION_BACK) {
            ESP_RETURN_ON_ERROR(app_shell_switch_to(s_clock_settings_return_app), TAG, "switch back failed");
            return ESP_OK;
        }

        ESP_RETURN_ON_ERROR(cyd_clock_settings_handle_alarm_action(action_id, &handled),
                            TAG,
                            "handle alarm action failed");
    }

    return ESP_OK;
}

static const app_shell_app_t s_cyd_clock_settings_shell_app = {
    .id = "clock_settings",
    .ctx = NULL,
    .enter = cyd_clock_settings_enter,
    .step = cyd_clock_settings_step,
    .leave = NULL,
};

const app_shell_app_t *cyd_clock_settings_app_get_app(void)
{
    return &s_cyd_clock_settings_shell_app;
}
