#include <stdbool.h>
#include <stdint.h>
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "app_shell.h"
#include "cyd_display.h"
#include "cyd_input.h"
#include "cyd_ui.h"
#include "hello_app.h"

#define TAG "hello_app"
#define HELLO_APP_INPUT_POLL_MS 50
#define HELLO_APP_LOG_INTERVAL_MS 1000
#define HELLO_APP_ACTION_INFO 1
#define INFO_APP_ACTION_OK 2

typedef struct {
    bool pending;
    uint16_t action_id;
} hello_app_action_tracker_t;

static cyd_display_screen_t s_hello_screen;
static TickType_t s_hello_last_log_tick;
static hello_app_action_tracker_t s_hello_action_tracker;
static hello_app_action_tracker_t s_info_action_tracker;

static const app_shell_app_t *info_app_get_app(void);

static bool hello_app_touch_confirmed_action(const cyd_input_event_t *event,
                                             hello_app_action_tracker_t *tracker,
                                             uint16_t *action_id)
{
    if (event == NULL || tracker == NULL || event->type != CYD_INPUT_EVENT_TOUCH) {
        return false;
    }

    if (event->data.touch.action == CYD_INPUT_TOUCH_ACTION_PRESS) {
        tracker->pending = cyd_display_hit_test_action(event->data.touch.x,
                                                       event->data.touch.y,
                                                       &tracker->action_id);
        return false;
    }

    if (event->data.touch.action != CYD_INPUT_TOUCH_ACTION_RELEASE) {
        return false;
    }

    uint16_t release_action_id = 0;
    bool confirmed = tracker->pending &&
                     cyd_display_hit_test_action(event->data.touch.x,
                                                 event->data.touch.y,
                                                 &release_action_id) &&
                     release_action_id == tracker->action_id;

    if (confirmed && action_id != NULL) {
        *action_id = release_action_id;
    }

    tracker->pending = false;
    tracker->action_id = 0;
    return confirmed;
}

static esp_err_t hello_app_show_main(void)
{
    cyd_display_screen_t *screen = &s_hello_screen;

    cyd_ui_screen_clear(screen);
    cyd_ui_add_text(screen,
                    "hello_app",
                    0,
                    4,
                    CYD_DISPLAY_GRID_COLS,
                    4,
                    CYD_DISPLAY_ALIGN_CENTER,
                    2,
                    CYD_UI_COLOR_CYAN);
    cyd_ui_add_text(screen,
                    "hello world is logging every second",
                    0,
                    12,
                    CYD_DISPLAY_GRID_COLS,
                    2,
                    CYD_DISPLAY_ALIGN_CENTER,
                    1,
                    CYD_UI_COLOR_LIGHTGREY);
    cyd_ui_add_button(screen,
                      "info",
                      12,
                      27,
                      16,
                      2,
                      CYD_UI_COLOR_BLUE,
                      CYD_UI_COLOR_CYAN,
                      HELLO_APP_ACTION_INFO);

    return cyd_ui_submit(screen);
}

static esp_err_t info_app_show_main(void)
{
    cyd_display_screen_t screen = { 0 };

    cyd_ui_screen_clear(&screen);
    cyd_ui_add_text(&screen,
                    "hello_app",
                    0,
                    4,
                    CYD_DISPLAY_GRID_COLS,
                    4,
                    CYD_DISPLAY_ALIGN_CENTER,
                    2,
                    CYD_UI_COLOR_CYAN);
    cyd_ui_add_text(&screen,
                    "title: hello_app",
                    4,
                    12,
                    32,
                    2,
                    CYD_DISPLAY_ALIGN_LEFT,
                    1,
                    CYD_UI_COLOR_WHITE);
    cyd_ui_add_text(&screen,
                    "author: katsusand",
                    4,
                    15,
                    32,
                    2,
                    CYD_DISPLAY_ALIGN_LEFT,
                    1,
                    CYD_UI_COLOR_WHITE);
    cyd_ui_add_button(&screen,
                      "OK",
                      12,
                      27,
                      16,
                      2,
                      CYD_UI_COLOR_GREEN,
                      CYD_UI_COLOR_CYAN,
                      INFO_APP_ACTION_OK);

    return cyd_ui_submit(&screen);
}

static esp_err_t hello_app_enter(void *ctx, const app_shell_app_t *from_app)
{
    (void)ctx;
    (void)from_app;
    s_hello_last_log_tick = 0;
    s_hello_action_tracker = (hello_app_action_tracker_t){ 0 };
    return hello_app_show_main();
}

static esp_err_t hello_app_step(void *ctx)
{
    (void)ctx;

    cyd_input_event_t event = { 0 };
    if (cyd_input_read_event(&event, pdMS_TO_TICKS(HELLO_APP_INPUT_POLL_MS)) == ESP_OK) {
        uint16_t action_id = 0;
        if (hello_app_touch_confirmed_action(&event, &s_hello_action_tracker, &action_id) &&
            action_id == HELLO_APP_ACTION_INFO) {
            ESP_LOGI(TAG, "switching to info_app");
            ESP_RETURN_ON_ERROR(app_shell_switch_to(info_app_get_app()), TAG, "switch to info_app failed");
        }
    }

    TickType_t now = xTaskGetTickCount();
    if (s_hello_last_log_tick == 0 ||
        now - s_hello_last_log_tick >= pdMS_TO_TICKS(HELLO_APP_LOG_INTERVAL_MS)) {
        ESP_LOGI(TAG, "hello world");
        s_hello_last_log_tick = now;
    }

    return ESP_OK;
}

static esp_err_t hello_app_leave(void *ctx)
{
    (void)ctx;
    return ESP_OK;
}

static esp_err_t info_app_enter(void *ctx, const app_shell_app_t *from_app)
{
    (void)ctx;
    (void)from_app;
    s_info_action_tracker = (hello_app_action_tracker_t){ 0 };
    return info_app_show_main();
}

static esp_err_t info_app_step(void *ctx)
{
    (void)ctx;

    cyd_input_event_t event = { 0 };
    if (cyd_input_read_event(&event, pdMS_TO_TICKS(HELLO_APP_INPUT_POLL_MS)) == ESP_OK) {
        uint16_t action_id = 0;
        if (hello_app_touch_confirmed_action(&event, &s_info_action_tracker, &action_id) &&
            action_id == INFO_APP_ACTION_OK) {
            ESP_LOGI(TAG, "returning to hello_app");
            ESP_RETURN_ON_ERROR(app_shell_switch_to(hello_app_get_app()), TAG, "switch to hello_app failed");
        }
    }

    return ESP_OK;
}

static esp_err_t info_app_leave(void *ctx)
{
    (void)ctx;
    return ESP_OK;
}

static const app_shell_app_t s_hello_shell_app = {
    .id = "hello",
    .ctx = NULL,
    .enter = hello_app_enter,
    .step = hello_app_step,
    .leave = hello_app_leave,
};

static const app_shell_app_t s_info_shell_app = {
    .id = "info",
    .ctx = NULL,
    .enter = info_app_enter,
    .step = info_app_step,
    .leave = info_app_leave,
};

const app_shell_app_t *hello_app_get_app(void)
{
    return &s_hello_shell_app;
}

static const app_shell_app_t *info_app_get_app(void)
{
    return &s_info_shell_app;
}
