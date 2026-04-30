#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "app_stack_monitor.h"
#include "app_shell.h"
#include "cyd_alarm.h"
#include "cyd_clock_app.h"
#include "cyd_display.h"
#include "cyd_input.h"
#include "cyd_system_apps.h"
#include "cyd_ui.h"
#include "cyd_wifi_setup.h"
#include "time_sync.h"
#include "wifi_manager.h"

#ifndef CONFIG_CYD_CLOCK_APP_TASK_STACK_SIZE
#define CONFIG_CYD_CLOCK_APP_TASK_STACK_SIZE 6144
#endif
#ifndef CONFIG_CYD_CLOCK_APP_TASK_PRIORITY
#define CONFIG_CYD_CLOCK_APP_TASK_PRIORITY 5
#endif
#ifndef CONFIG_CYD_CLOCK_APP_UPDATE_INTERVAL_MS
#define CONFIG_CYD_CLOCK_APP_UPDATE_INTERVAL_MS 1000
#endif
#ifndef CONFIG_CYD_CLOCK_APP_STACK_LOG_INTERVAL_MS
#define CONFIG_CYD_CLOCK_APP_STACK_LOG_INTERVAL_MS 30000
#endif
#ifndef CONFIG_ESP32_WIFI_STA_CONNECT_TIMEOUT_MS
#define CONFIG_ESP32_WIFI_STA_CONNECT_TIMEOUT_MS 15000
#endif

#define TAG "cyd_clock_app"
#define CYD_CLOCK_APP_INPUT_POLL_MS 50
#define CYD_CLOCK_APP_IDLE_POLL_MS 250
#define CYD_CLOCK_APP_TAP_SLOP_PX 24
#define CYD_CLOCK_APP_ACTION_SYNC_NOW 0x1001
#define CYD_CLOCK_APP_ACTION_SETTINGS 0x1002
#define CYD_CLOCK_APP_ACTION_INFO 0x1003
#define CYD_CLOCK_APP_ACTION_ALARM 0x1004
#define CYD_CLOCK_APP_TIME_COL 0
#define CYD_CLOCK_APP_TIME_ROW 10
#define CYD_CLOCK_APP_TIME_SPAN_COLS CYD_DISPLAY_GRID_COLS
#define CYD_CLOCK_APP_TIME_SPAN_ROWS 6

static cyd_display_screen_t s_clock_screen;
static bool s_clock_use_24_hour = true;
static bool s_clock_sync_now_pending;

typedef enum {
    CYD_CLOCK_APP_MODE_CLOCK = 0,
    CYD_CLOCK_APP_MODE_WIFI_FAILED,
    CYD_CLOCK_APP_MODE_WIFI_RETRYING,
} cyd_clock_app_mode_t;

static TickType_t s_clock_last_update_tick;
static cyd_clock_app_mode_t s_clock_mode = CYD_CLOCK_APP_MODE_CLOCK;

typedef struct {
    bool pending;
    bool long_pressed;
    bool format_toggle_candidate;
    int16_t x;
    int16_t y;
} cyd_clock_touch_tracker_t;

typedef struct {
    bool pending;
    bool long_pressed;
    size_t button_index;
} cyd_clock_mode_button_tracker_t;

static cyd_clock_touch_tracker_t s_clock_touch_tracker;
static cyd_clock_mode_button_tracker_t s_clock_action_tracker;

static bool cyd_clock_app_touch_confirmed_mode_button(const cyd_input_event_t *event,
                                                      cyd_clock_mode_button_tracker_t *tracker,
                                                      size_t *button_index)
{
    if (event == NULL || tracker == NULL || event->type != CYD_INPUT_EVENT_TOUCH) {
        return false;
    }

    switch (event->data.touch.action) {
    case CYD_INPUT_TOUCH_ACTION_PRESS:
        tracker->pending = cyd_display_hit_test_mode_button(event->data.touch.x,
                                                            event->data.touch.y,
                                                            &tracker->button_index);
        tracker->long_pressed = false;
        return false;
    case CYD_INPUT_TOUCH_ACTION_LONG_PRESS:
    case CYD_INPUT_TOUCH_ACTION_REPEAT:
        if (tracker->pending) {
            tracker->long_pressed = true;
        }
        return false;
    case CYD_INPUT_TOUCH_ACTION_RELEASE: {
        size_t release_button_index = 0;
        bool confirmed = tracker->pending &&
                         !tracker->long_pressed &&
                         cyd_display_hit_test_mode_button(event->data.touch.x,
                                                          event->data.touch.y,
                                                          &release_button_index) &&
                         release_button_index == tracker->button_index;
        if (confirmed && button_index != NULL) {
            *button_index = release_button_index;
        }
        tracker->pending = false;
        tracker->long_pressed = false;
        tracker->button_index = 0;
        return confirmed;
    }
    default:
        return false;
    }
}

static bool cyd_clock_app_touch_confirmed_action(const cyd_input_event_t *event,
                                                 cyd_clock_mode_button_tracker_t *tracker,
                                                 uint16_t *action_id)
{
    if (event == NULL || tracker == NULL || event->type != CYD_INPUT_EVENT_TOUCH) {
        return false;
    }

    switch (event->data.touch.action) {
    case CYD_INPUT_TOUCH_ACTION_PRESS:
        {
            uint16_t pressed_action_id = 0;
            tracker->pending = cyd_display_hit_test_action(event->data.touch.x,
                                                           event->data.touch.y,
                                                           &pressed_action_id);
            tracker->button_index = pressed_action_id;
        }
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
                         release_action_id == (uint16_t)tracker->button_index;
        if (confirmed && action_id != NULL) {
            *action_id = release_action_id;
        }
        tracker->pending = false;
        tracker->long_pressed = false;
        tracker->button_index = 0;
        return confirmed;
    }
    default:
        return false;
    }
}

static void cyd_clock_app_log_stack_usage(void)
{
    APP_STACK_MONITOR_CHECK(TAG, "cyd_clock_app", CONFIG_CYD_CLOCK_APP_STACK_LOG_INTERVAL_MS);
    APP_HEAP_MONITOR_CHECK(TAG, CONFIG_CYD_CLOCK_APP_STACK_LOG_INTERVAL_MS);
}

static bool cyd_clock_app_time_is_synced(const struct tm *timeinfo)
{
    return timeinfo != NULL && (timeinfo->tm_year + 1900) >= 2024;
}

static bool cyd_clock_app_has_time_sync_success(void)
{
    time_t last_success_at = 0;
    return time_sync_get_last_success_at(&last_success_at);
}

static void cyd_clock_app_format_sync_status(char *status_text, size_t status_size)
{
    esp_err_t last_status = ESP_OK;
    time_t last_success_at = 0;

    if (status_text == NULL || status_size == 0) {
        return;
    }

    if (!time_sync_get_last_attempt_status(&last_status)) {
        snprintf(status_text, status_size, "sync: pending");
        return;
    }

    if (last_status != ESP_OK) {
        snprintf(status_text, status_size, "sync: failed");
        return;
    }

    if (!time_sync_get_last_success_at(&last_success_at)) {
        snprintf(status_text, status_size, "sync: pending");
        return;
    }

    struct tm sync_time = { 0 };
    localtime_r(&last_success_at, &sync_time);
    strftime(status_text, status_size, "sync: %m-%d %H:%M OK", &sync_time);
}

static const char *cyd_clock_app_wifi_state_text(wifi_manager_state_t state)
{
    switch (state) {
    case WIFI_MANAGER_STATE_STOPPED:
        return "wifi: stopped";
    case WIFI_MANAGER_STATE_INIT:
        return "wifi: init";
    case WIFI_MANAGER_STATE_OFF:
        return "wifi: off";
    case WIFI_MANAGER_STATE_CONNECTING:
        return "wifi: connecting";
    case WIFI_MANAGER_STATE_CONNECTED:
        return "wifi: connected";
    case WIFI_MANAGER_STATE_RECONNECTING:
        return "wifi: reconnecting";
    case WIFI_MANAGER_STATE_FAILED:
        return "wifi: failed";
    case WIFI_MANAGER_STATE_SETUP_REQUIRED:
        return "wifi: setup needed";
    case WIFI_MANAGER_STATE_SETUP_RUNNING:
        return "wifi: setup";
    default:
        return "wifi: unknown";
    }
}

static void cyd_clock_app_format_wifi_status(char *status_text, size_t status_size)
{
    wifi_manager_state_t state = WIFI_MANAGER_STATE_STOPPED;

    if (status_text == NULL || status_size == 0) {
        return;
    }

    if (wifi_manager_get_state(&state) != ESP_OK) {
        snprintf(status_text, status_size, "wifi: unavailable");
        return;
    }

    snprintf(status_text, status_size, "%s", cyd_clock_app_wifi_state_text(state));
}

static bool cyd_clock_app_sync_now_enabled(void)
{
    wifi_manager_state_t state = WIFI_MANAGER_STATE_STOPPED;

    if (time_sync_is_busy()) {
        return false;
    }

    if (wifi_manager_get_state(&state) != ESP_OK) {
        return false;
    }

    return state == WIFI_MANAGER_STATE_CONNECTED ||
           state == WIFI_MANAGER_STATE_FAILED ||
           state == WIFI_MANAGER_STATE_OFF ||
           state == WIFI_MANAGER_STATE_SETUP_REQUIRED;
}

static int16_t cyd_clock_app_abs_i16(int16_t value)
{
    return value < 0 ? (int16_t)-value : value;
}

static bool cyd_clock_app_touch_is_time_display(int16_t x, int16_t y)
{
    uint8_t col = 0;
    uint8_t row = 0;

    if (!cyd_display_touch_to_grid(x, y, &col, &row)) {
        return false;
    }

    return col >= CYD_CLOCK_APP_TIME_COL &&
           col < (CYD_CLOCK_APP_TIME_COL + CYD_CLOCK_APP_TIME_SPAN_COLS) &&
           row >= CYD_CLOCK_APP_TIME_ROW &&
           row < (CYD_CLOCK_APP_TIME_ROW + CYD_CLOCK_APP_TIME_SPAN_ROWS);
}

static bool cyd_clock_app_touch_is_tap(const cyd_input_event_t *event)
{
    if (event == NULL || event->type != CYD_INPUT_EVENT_TOUCH) {
        return false;
    }

    switch (event->data.touch.action) {
    case CYD_INPUT_TOUCH_ACTION_PRESS:
        s_clock_touch_tracker.pending = true;
        s_clock_touch_tracker.long_pressed = false;
        s_clock_touch_tracker.format_toggle_candidate =
            cyd_clock_app_touch_is_time_display(event->data.touch.x, event->data.touch.y);
        s_clock_touch_tracker.x = event->data.touch.x;
        s_clock_touch_tracker.y = event->data.touch.y;
        return false;
    case CYD_INPUT_TOUCH_ACTION_LONG_PRESS:
    case CYD_INPUT_TOUCH_ACTION_REPEAT:
        if (s_clock_touch_tracker.pending) {
            s_clock_touch_tracker.long_pressed = true;
        }
        return false;
    case CYD_INPUT_TOUCH_ACTION_RELEASE: {
        bool tapped = s_clock_touch_tracker.pending &&
                      !s_clock_touch_tracker.long_pressed &&
                      s_clock_touch_tracker.format_toggle_candidate &&
                      cyd_clock_app_touch_is_time_display(event->data.touch.x, event->data.touch.y) &&
                      cyd_clock_app_abs_i16(event->data.touch.x - s_clock_touch_tracker.x) <= CYD_CLOCK_APP_TAP_SLOP_PX &&
                      cyd_clock_app_abs_i16(event->data.touch.y - s_clock_touch_tracker.y) <= CYD_CLOCK_APP_TAP_SLOP_PX;
        s_clock_touch_tracker.pending = false;
        s_clock_touch_tracker.long_pressed = false;
        s_clock_touch_tracker.format_toggle_candidate = false;
        return tapped;
    }
    default:
        return false;
    }
}

static bool cyd_clock_app_process_input(void)
{
    bool redraw = false;
    cyd_input_event_t event = { 0 };

    while (cyd_input_read_event(&event, 0) == ESP_OK) {
        uint16_t action_id = 0;
        if (cyd_clock_app_touch_confirmed_action(&event, &s_clock_action_tracker, &action_id)) {
            if (action_id == CYD_CLOCK_APP_ACTION_SYNC_NOW) {
                wifi_manager_state_t wifi_state = WIFI_MANAGER_STATE_STOPPED;
                if (wifi_manager_get_state(&wifi_state) == ESP_OK) {
                    if (wifi_state == WIFI_MANAGER_STATE_CONNECTED) {
                        s_clock_sync_now_pending = false;
                    } else {
                        s_clock_sync_now_pending = true;
                    }

                    if (wifi_state == WIFI_MANAGER_STATE_FAILED ||
                        wifi_state == WIFI_MANAGER_STATE_OFF ||
                        wifi_state == WIFI_MANAGER_STATE_SETUP_REQUIRED) {
                        if (wifi_manager_retry_connection_without_setup_async() != ESP_OK) {
                            ESP_LOGW(TAG, "SYNC NOW failed to start Wi-Fi retry");
                        }
                    }
                }
                time_sync_request_soon();
                ESP_LOGI(TAG, "SYNC NOW requested");
                redraw = true;
                continue;
            }
            if (action_id == CYD_CLOCK_APP_ACTION_SETTINGS) {
                ESP_RETURN_ON_ERROR(app_shell_switch_to(cyd_settings_app_get_app()),
                                    TAG,
                                    "switch to settings failed");
                continue;
            }
            if (action_id == CYD_CLOCK_APP_ACTION_INFO) {
                ESP_RETURN_ON_ERROR(app_shell_switch_to(cyd_info_app_get_app()),
                                    TAG,
                                    "switch to info failed");
                continue;
            }
            if (action_id == CYD_CLOCK_APP_ACTION_ALARM) {
                ESP_RETURN_ON_ERROR(cyd_alarm_cycle_mode(NULL), TAG, "cycle alarm mode failed");
                redraw = true;
                continue;
            }
        }

        if (cyd_clock_app_touch_is_tap(&event)) {
            s_clock_use_24_hour = !s_clock_use_24_hour;
            ESP_LOGI(TAG, "clock format changed: %s", s_clock_use_24_hour ? "24-hour" : "12-hour");
            redraw = true;
        }
    }

    return redraw;
}

static void cyd_clock_app_service_sync_now_pending(void)
{
    wifi_manager_state_t wifi_state = WIFI_MANAGER_STATE_STOPPED;

    if (!s_clock_sync_now_pending) {
        return;
    }
    if (wifi_manager_get_state(&wifi_state) != ESP_OK) {
        return;
    }
    if (wifi_state == WIFI_MANAGER_STATE_CONNECTED) {
        ESP_LOGI(TAG, "SYNC NOW resumed after Wi-Fi reconnect");
        time_sync_request_soon();
        s_clock_sync_now_pending = false;
    }
}

static esp_err_t cyd_clock_app_show_clock(void)
{
    time_t now = 0;
    struct tm local_time = { 0 };
    char time_text[16] = { 0 };
    char date_text[24] = { 0 };
    char status_text[CYD_DISPLAY_TEXT_MAX_LEN + 1] = { 0 };
    char wifi_status_text[CYD_DISPLAY_TEXT_MAX_LEN + 1] = { 0 };
    cyd_display_screen_t *screen = &s_clock_screen;
    cyd_alarm_mode_t alarm_mode = cyd_alarm_get_mode();
    const char *alarm_label = cyd_alarm_mode_label(alarm_mode);
    bool sync_now_enabled = false;

    time(&now);
    localtime_r(&now, &local_time);

    if (cyd_clock_app_time_is_synced(&local_time)) {
        strftime(time_text,
                 sizeof(time_text),
                 s_clock_use_24_hour ? "%H:%M:%S" : "%I:%M:%S %p",
                 &local_time);
        strftime(date_text, sizeof(date_text), "%Y-%m-%d", &local_time);
        cyd_clock_app_format_sync_status(status_text, sizeof(status_text));
        cyd_clock_app_format_wifi_status(wifi_status_text, sizeof(wifi_status_text));
    } else {
        snprintf(time_text, sizeof(time_text), "--:--:--");
        snprintf(date_text, sizeof(date_text), "Waiting for NTP");
        cyd_clock_app_format_sync_status(status_text, sizeof(status_text));
        cyd_clock_app_format_wifi_status(wifi_status_text, sizeof(wifi_status_text));
    }
    sync_now_enabled = cyd_clock_app_sync_now_enabled();

    cyd_ui_screen_clear(screen);

    cyd_ui_add_text(screen,
                    "CYD CLOCK",
                    0,
                    2,
                    CYD_DISPLAY_GRID_COLS,
                    2,
                    CYD_DISPLAY_ALIGN_CENTER,
                    2,
                    CYD_UI_COLOR_YELLOW);
    cyd_ui_add_text(screen,
                    date_text,
                    0,
                    7,
                    CYD_DISPLAY_GRID_COLS,
                    2,
                    CYD_DISPLAY_ALIGN_CENTER,
                    2,
                    CYD_UI_COLOR_WHITE);
    cyd_ui_add_text(screen,
                    time_text,
                    CYD_CLOCK_APP_TIME_COL,
                    CYD_CLOCK_APP_TIME_ROW,
                    CYD_CLOCK_APP_TIME_SPAN_COLS,
                    CYD_CLOCK_APP_TIME_SPAN_ROWS,
                    CYD_DISPLAY_ALIGN_CENTER,
                    4,
                    CYD_UI_COLOR_CYAN);
    cyd_ui_add_button_with_fg_enabled(screen,
                                      "SYNC NOW",
                                      12,
                                      18,
                                      16,
                                      3,
                                      CYD_UI_COLOR_WHITE,
                                      CYD_UI_COLOR_BLUE,
                                      CYD_UI_COLOR_CYAN,
                                      CYD_CLOCK_APP_ACTION_SYNC_NOW,
                                      sync_now_enabled);
    cyd_ui_add_text(screen,
                    status_text,
                    0,
                    22,
                    CYD_DISPLAY_GRID_COLS,
                    2,
                    CYD_DISPLAY_ALIGN_CENTER,
                    1,
                    CYD_UI_COLOR_DARKGREY);
    cyd_ui_add_text(screen,
                    wifi_status_text,
                    0,
                    24,
                    CYD_DISPLAY_GRID_COLS,
                    2,
                    CYD_DISPLAY_ALIGN_CENTER,
                    1,
                    CYD_UI_COLOR_DARKGREY);
    cyd_ui_add_button(screen,
        "SETTINGS",
        1,
        26,
        12,
        3,
        CYD_UI_COLOR_BLUE,
        CYD_UI_COLOR_CYAN,
        CYD_CLOCK_APP_ACTION_SETTINGS
    );
    cyd_ui_add_button(screen,
        "INFO",
        14,
        26,
        12,
        3,
        CYD_UI_COLOR_BLUE,
        CYD_UI_COLOR_CYAN,
        CYD_CLOCK_APP_ACTION_INFO
    );
    cyd_ui_add_button_with_fg(screen,
                              alarm_label,
                              27,
                              26,
                              12,
                              3,
                              CYD_UI_COLOR_WHITE,
                              alarm_mode == CYD_ALARM_MODE_OFF ? CYD_UI_COLOR_DIMGREY : CYD_UI_COLOR_RED,
                              alarm_mode == CYD_ALARM_MODE_OFF ? CYD_UI_COLOR_LIGHTGREY : CYD_UI_COLOR_YELLOW,
                              CYD_CLOCK_APP_ACTION_ALARM);
    return cyd_ui_submit(screen);
}

static bool cyd_clock_app_should_enter_wifi_setup(void)
{
    wifi_manager_state_t state = WIFI_MANAGER_STATE_STOPPED;

    if (wifi_manager_get_state(&state) != ESP_OK) {
        return false;
    }

    return state == WIFI_MANAGER_STATE_SETUP_REQUIRED &&
           (wifi_manager_is_setup_requested_explicitly() ||
            !cyd_clock_app_has_time_sync_success());
}

static bool cyd_clock_app_should_show_wifi_failed(void)
{
    wifi_manager_state_t state = WIFI_MANAGER_STATE_STOPPED;

    if (cyd_clock_app_has_time_sync_success()) {
        return false;
    }

    if (wifi_manager_get_state(&state) != ESP_OK) {
        return false;
    }

    return state == WIFI_MANAGER_STATE_FAILED;
}

static const char *cyd_clock_app_wifi_failure_text(esp32_wifi_sta_failure_reason_t reason)
{
    switch (reason) {
    case ESP32_WIFI_STA_FAILURE_NO_SAVED_PROFILE:
        return "No saved Wi-Fi profile";
    case ESP32_WIFI_STA_FAILURE_NO_AP_IN_RANGE:
        return "No saved AP in range";
    case ESP32_WIFI_STA_FAILURE_AUTH:
        return "Authentication failed";
    case ESP32_WIFI_STA_FAILURE_TIMEOUT:
        return "Connection timeout";
    case ESP32_WIFI_STA_FAILURE_CONNECT:
        return "Wi-Fi connect failed";
    case ESP32_WIFI_STA_FAILURE_NONE:
    default:
        return "Wi-Fi unavailable";
    }
}

static void cyd_clock_app_begin_wifi_setup(void)
{
    ESP_LOGI(TAG, "switching to Wi-Fi setup app");
    ESP_ERROR_CHECK(app_shell_switch_to(cyd_wifi_setup_get_app()));
}

static cyd_clock_app_mode_t cyd_clock_app_run_wifi_failed(void)
{
    const char *lines[] = {
        cyd_clock_app_wifi_failure_text(wifi_manager_get_last_failure_reason()),
        "Select RETRY or SETUP",
    };
    const char *buttons[] = { "RETRY", "SETUP" };
    cyd_clock_mode_button_tracker_t tracker = { 0 };

    ESP_ERROR_CHECK(cyd_display_show_mode_screen("Wi-Fi failed", lines, 2, buttons, 2, 0));

    while (true) {
        cyd_input_event_t event = { 0 };
        if (cyd_input_read_event(&event, pdMS_TO_TICKS(CYD_CLOCK_APP_IDLE_POLL_MS)) != ESP_OK) {
            if (app_shell_is_idle_timeout_elapsed()) {
                return CYD_CLOCK_APP_MODE_CLOCK;
            }
            continue;
        }
        size_t button_index = 0;
        if (cyd_clock_app_touch_confirmed_mode_button(&event, &tracker, &button_index)) {
            if (button_index == 0) {
                ESP_LOGI(TAG, "retrying saved Wi-Fi profiles");
                if (wifi_manager_retry_connection_without_setup_async() != ESP_OK) {
                    ESP_LOGW(TAG, "failed to start Wi-Fi retry");
                    return CYD_CLOCK_APP_MODE_WIFI_FAILED;
                }
                return CYD_CLOCK_APP_MODE_WIFI_RETRYING;
            }
            cyd_clock_app_begin_wifi_setup();
            return CYD_CLOCK_APP_MODE_CLOCK;
        }

        if (cyd_clock_app_touch_is_tap(&event)) {
            continue;
        }
    }
}

static cyd_clock_app_mode_t cyd_clock_app_run_wifi_retrying(void)
{
    cyd_wifi_setup_autoconnect_progress_t last_progress = {
        .phase = (cyd_wifi_setup_autoconnect_phase_t)-1,
    };

    while (true) {
        cyd_input_event_t event = { 0 };
        (void)cyd_input_read_event(&event, pdMS_TO_TICKS(CYD_CLOCK_APP_INPUT_POLL_MS));
        (void)cyd_clock_app_touch_is_tap(&event);

        cyd_wifi_setup_autoconnect_progress_t progress = cyd_wifi_setup_get_autoconnect_progress();
        if (progress.phase != last_progress.phase ||
            strncmp(progress.ssid, last_progress.ssid, sizeof(progress.ssid)) != 0) {
            const char *lines[2] = { "", "Please wait" };
            char trying_line[CYD_DISPLAY_TEXT_MAX_LEN + 1] = { 0 };

            if (progress.phase == CYD_WIFI_SETUP_AUTOCONNECT_TRYING && progress.ssid[0] != '\0') {
                snprintf(trying_line, sizeof(trying_line), "Trying %.30s", progress.ssid);
                lines[0] = trying_line;
            } else if (progress.phase == CYD_WIFI_SETUP_AUTOCONNECT_SEARCHING) {
                lines[0] = "Searching saved APs";
            } else {
                lines[0] = "Connecting Wi-Fi";
            }

            ESP_ERROR_CHECK(cyd_display_show_lines("Wi-Fi", lines, 2));
            last_progress = progress;
        }

        wifi_manager_state_t state = WIFI_MANAGER_STATE_STOPPED;
        if (wifi_manager_get_state(&state) != ESP_OK) {
            return CYD_CLOCK_APP_MODE_WIFI_FAILED;
        }

        if (state == WIFI_MANAGER_STATE_CONNECTED) {
            ESP_LOGI(TAG, "saved Wi-Fi retry connected");
            time_sync_request_soon();
            return CYD_CLOCK_APP_MODE_CLOCK;
        }
        if (state == WIFI_MANAGER_STATE_FAILED ||
            state == WIFI_MANAGER_STATE_OFF ||
            state == WIFI_MANAGER_STATE_SETUP_REQUIRED) {
            ESP_LOGW(TAG, "saved Wi-Fi retry ended with state=%d", (int)state);
            return CYD_CLOCK_APP_MODE_WIFI_FAILED;
        }
    }
}

static esp_err_t cyd_clock_app_enter(void *ctx, const app_shell_app_t *from_app)
{
    (void)ctx;
    s_clock_last_update_tick = 0;
    s_clock_mode = CYD_CLOCK_APP_MODE_CLOCK;
    if (from_app == NULL && !cyd_input_has_touch_calibration()) {
        ESP_LOGI(TAG, "no saved touch calibration, switching to touch calibration app");
        return app_shell_switch_to(cyd_touch_calibration_app_get_app());
    }

    return ESP_OK;
}

static esp_err_t cyd_clock_app_leave(void *ctx)
{
    (void)ctx;
    return ESP_OK;
}

static esp_err_t cyd_clock_app_step(void *ctx)
{
    (void)ctx;

    cyd_clock_app_log_stack_usage();

    if (s_clock_mode == CYD_CLOCK_APP_MODE_WIFI_FAILED) {
        s_clock_mode = cyd_clock_app_run_wifi_failed();
        s_clock_last_update_tick = 0;
        return ESP_OK;
    }
    if (s_clock_mode == CYD_CLOCK_APP_MODE_WIFI_RETRYING) {
        s_clock_mode = cyd_clock_app_run_wifi_retrying();
        s_clock_last_update_tick = 0;
        return ESP_OK;
    }

    bool redraw = cyd_clock_app_process_input();
    cyd_clock_app_service_sync_now_pending();
    TickType_t now = xTaskGetTickCount();

    if (cyd_clock_app_should_enter_wifi_setup()) {
        cyd_clock_app_begin_wifi_setup();
        s_clock_mode = CYD_CLOCK_APP_MODE_CLOCK;
        return ESP_OK;
    }
    if (cyd_clock_app_should_show_wifi_failed()) {
        s_clock_mode = CYD_CLOCK_APP_MODE_WIFI_FAILED;
        return ESP_OK;
    }

    if (redraw ||
        s_clock_last_update_tick == 0 ||
        now - s_clock_last_update_tick >= pdMS_TO_TICKS(CONFIG_CYD_CLOCK_APP_UPDATE_INTERVAL_MS)) {
        ESP_RETURN_ON_ERROR(cyd_clock_app_show_clock(), TAG, "show clock failed");
        s_clock_last_update_tick = now;
    }

    vTaskDelay(pdMS_TO_TICKS(CYD_CLOCK_APP_INPUT_POLL_MS));
    return ESP_OK;
}

static const app_shell_app_t s_cyd_clock_shell_app = {
    .id = "clock",
    .ctx = NULL,
    .enter = cyd_clock_app_enter,
    .step = cyd_clock_app_step,
    .leave = cyd_clock_app_leave,
};

const app_shell_app_t *cyd_clock_app_get_app(void)
{
    return &s_cyd_clock_shell_app;
}
