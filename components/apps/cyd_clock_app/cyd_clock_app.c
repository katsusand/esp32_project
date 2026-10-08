#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "app_stack_monitor.h"
#include "app_registry.h"
#include "app_shell.h"
#include "cyd_clock_alarm.h"
#include "cyd_clock_app.h"
#include "cyd_clock_view.h"
#include "cyd_clock_settings_app.h"
#include "cyd_display.h"
#include "cyd_input.h"
#include "cyd_system_apps.h"
#include "cyd_ui.h"
#include "cyd_wifi_setup.h"
#include "sd_card_status_icon.h"
#include "time_tick.h"
#include "time_sync.h"
#include "wifi_connection.h"

#ifndef CONFIG_CYD_CLOCK_APP_TASK_PRIORITY
#define CONFIG_CYD_CLOCK_APP_TASK_PRIORITY 5
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
/* The SD card problem icon: two grid cells square, in the top-right corner,
   which the title (row 2 and below) never reaches. */

static cyd_display_screen_t s_clock_screen;
static bool s_clock_use_24_hour = true;

/* What the ALARM button shows and cycles through; the alarms themselves are
   cyd_clock_alarm's. */
typedef enum {
    CYD_CLOCK_APP_ALARM_MODE_OFF = 0,
    CYD_CLOCK_APP_ALARM_MODE_1,
    CYD_CLOCK_APP_ALARM_MODE_2,
    CYD_CLOCK_APP_ALARM_MODE_1_2,
} cyd_clock_app_alarm_mode_t;

typedef enum {
    CYD_CLOCK_APP_MODE_CLOCK = 0,
    CYD_CLOCK_APP_MODE_WIFI_FAILED,
    CYD_CLOCK_APP_MODE_WIFI_RETRYING,
} cyd_clock_app_mode_t;

static cyd_clock_app_mode_t s_clock_mode = CYD_CLOCK_APP_MODE_CLOCK;
static QueueHandle_t s_clock_tick_queue;
static bool s_clock_needs_redraw;

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

static cyd_clock_app_alarm_mode_t cyd_clock_app_alarm_mode(void)
{
    bool alarm1_enabled = cyd_clock_alarm_is_enabled(CYD_CLOCK_ALARM_1);
    bool alarm2_enabled = cyd_clock_alarm_is_enabled(CYD_CLOCK_ALARM_2);

    if (alarm1_enabled && alarm2_enabled) {
        return CYD_CLOCK_APP_ALARM_MODE_1_2;
    }
    if (alarm1_enabled) {
        return CYD_CLOCK_APP_ALARM_MODE_1;
    }
    if (alarm2_enabled) {
        return CYD_CLOCK_APP_ALARM_MODE_2;
    }
    return CYD_CLOCK_APP_ALARM_MODE_OFF;
}

static esp_err_t cyd_clock_app_cycle_alarm_mode(void)
{
    cyd_clock_app_alarm_mode_t next_mode = CYD_CLOCK_APP_ALARM_MODE_OFF;

    switch (cyd_clock_app_alarm_mode()) {
    case CYD_CLOCK_APP_ALARM_MODE_OFF:
        next_mode = CYD_CLOCK_APP_ALARM_MODE_1;
        break;
    case CYD_CLOCK_APP_ALARM_MODE_1:
        next_mode = CYD_CLOCK_APP_ALARM_MODE_2;
        break;
    case CYD_CLOCK_APP_ALARM_MODE_2:
        next_mode = CYD_CLOCK_APP_ALARM_MODE_1_2;
        break;
    case CYD_CLOCK_APP_ALARM_MODE_1_2:
    default:
        next_mode = CYD_CLOCK_APP_ALARM_MODE_OFF;
        break;
    }

    bool enable_alarm1 = next_mode == CYD_CLOCK_APP_ALARM_MODE_1 || next_mode == CYD_CLOCK_APP_ALARM_MODE_1_2;
    bool enable_alarm2 = next_mode == CYD_CLOCK_APP_ALARM_MODE_2 || next_mode == CYD_CLOCK_APP_ALARM_MODE_1_2;

    ESP_RETURN_ON_ERROR(cyd_clock_alarm_set_enabled(CYD_CLOCK_ALARM_1, enable_alarm1),
                        TAG,
                        "set alarm1 enabled failed");
    return cyd_clock_alarm_set_enabled(CYD_CLOCK_ALARM_2, enable_alarm2);
}

esp_err_t cyd_clock_app_register(void)
{
    /* Borrowed by app_registry, so it must outlive registration. */
    /*
     * The settings screen is attached to the clock rather than registered on
     * its own, so it is a child of Clock instead of a peer app, and installing
     * or omitting the clock takes its settings screen with it.
     */
    static app_registry_entry_t entry = {
        .id = "clock",
        .title = "時計",
    };

    entry.app = cyd_clock_app_get_app();
    entry.settings_app = cyd_clock_settings_app_get_app();
    ESP_RETURN_ON_ERROR(app_registry_register(&entry), TAG, "register clock app failed");

    /*
     * The alarm belongs to the clock the same way: registering the clock is
     * what installs it, so a product without the clock has no alarm and
     * swapping the clock out takes the alarm with it. Needs
     * app_scheduler_init() to have run. On failure the clock itself stays
     * registered and usable; only the alarms are missing.
     */
    ESP_RETURN_ON_ERROR(cyd_clock_alarm_register(), TAG, "clock alarms unavailable");
    return ESP_OK;
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
            tracker->pending = cyd_display_screen_hit_test(&s_clock_screen,
                                                           event->data.touch.x,
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
                         cyd_display_screen_hit_test(&s_clock_screen,
                                                     event->data.touch.x,
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

static cyd_clock_view_sync_t cyd_clock_app_view_sync(struct tm *at)
{
    esp_err_t last_status = ESP_OK;
    time_t last_success_at = 0;

    if (!time_sync_get_last_attempt_status(&last_status)) {
        return CYD_CLOCK_VIEW_SYNC_NONE;
    }
    if (last_status != ESP_OK) {
        return CYD_CLOCK_VIEW_SYNC_FAILED;
    }
    if (!time_sync_get_last_success_at(&last_success_at)) {
        return CYD_CLOCK_VIEW_SYNC_NONE;
    }
    localtime_r(&last_success_at, at);
    return CYD_CLOCK_VIEW_SYNC_OK_AT;
}

static cyd_clock_view_wifi_t cyd_clock_app_view_wifi(void)
{
    wifi_connection_state_t state = WIFI_CONNECTION_STATE_STOPPED;

    if (wifi_connection_get_state(&state) != ESP_OK) {
        return CYD_CLOCK_VIEW_WIFI_UNAVAILABLE;
    }
    switch (state) {
    case WIFI_CONNECTION_STATE_STOPPED: return CYD_CLOCK_VIEW_WIFI_STOPPED;
    case WIFI_CONNECTION_STATE_INIT: return CYD_CLOCK_VIEW_WIFI_INIT;
    case WIFI_CONNECTION_STATE_OFF: return CYD_CLOCK_VIEW_WIFI_OFF;
    case WIFI_CONNECTION_STATE_CONNECTING: return CYD_CLOCK_VIEW_WIFI_CONNECTING;
    case WIFI_CONNECTION_STATE_CONNECTED: return CYD_CLOCK_VIEW_WIFI_CONNECTED;
    case WIFI_CONNECTION_STATE_RECONNECTING: return CYD_CLOCK_VIEW_WIFI_RECONNECTING;
    case WIFI_CONNECTION_STATE_FAILED: return CYD_CLOCK_VIEW_WIFI_FAILED;
    case WIFI_CONNECTION_STATE_SETUP_REQUIRED: return CYD_CLOCK_VIEW_WIFI_SETUP_REQUIRED;
    case WIFI_CONNECTION_STATE_SETUP_RUNNING: return CYD_CLOCK_VIEW_WIFI_SETUP_RUNNING;
    default: return CYD_CLOCK_VIEW_WIFI_UNAVAILABLE;
    }
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

    return col < (CYD_CLOCK_VIEW_TIME_COL + CYD_CLOCK_VIEW_TIME_SPAN_COLS) &&
           row >= CYD_CLOCK_VIEW_TIME_ROW &&
           row < (CYD_CLOCK_VIEW_TIME_ROW + CYD_CLOCK_VIEW_TIME_SPAN_ROWS);
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
            if (action_id == CYD_CLOCK_APP_ACTION_SETTINGS) {
                ESP_RETURN_ON_ERROR(app_shell_switch_to(system_settings_app_get_app()),
                                    TAG,
                                    "switch to settings failed");
                continue;
            }
            if (action_id == CYD_CLOCK_APP_ACTION_INFO) {
                ESP_RETURN_ON_ERROR(app_shell_switch_to(system_info_app_get_app()),
                                    TAG,
                                    "switch to info failed");
                continue;
            }
            if (action_id == CYD_CLOCK_APP_ACTION_ALARM) {
                ESP_RETURN_ON_ERROR(cyd_clock_app_cycle_alarm_mode(), TAG, "cycle alarm mode failed");
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

static bool cyd_clock_app_process_time_ticks(void)
{
    bool redraw = false;
    time_tick_event_t tick = { 0 };

    if (s_clock_tick_queue == NULL) {
        return false;
    }

    while (xQueueReceive(s_clock_tick_queue, &tick, 0) == pdTRUE) {
        redraw = true;
    }

    return redraw;
}

static esp_err_t cyd_clock_app_show_clock(void)
{
    static const cyd_clock_view_alarm_t alarm_views[] = {
        [CYD_CLOCK_APP_ALARM_MODE_OFF] = CYD_CLOCK_VIEW_ALARM_OFF,
        [CYD_CLOCK_APP_ALARM_MODE_1] = CYD_CLOCK_VIEW_ALARM_1,
        [CYD_CLOCK_APP_ALARM_MODE_2] = CYD_CLOCK_VIEW_ALARM_2,
        [CYD_CLOCK_APP_ALARM_MODE_1_2] = CYD_CLOCK_VIEW_ALARM_1_2,
    };
    time_t now = 0;
    cyd_clock_view_model_t model = {
        .screen = CYD_CLOCK_VIEW_SCREEN_FACE,
        .use_24_hour = s_clock_use_24_hour,
        .alarm = alarm_views[cyd_clock_app_alarm_mode()],
        /* The clock is drawn again every second, which is also how soon a card
           pulled or put back shows up here. Nothing is drawn while the card
           works. */
        .sd_icon = sd_card_status_icon_for_state(sd_card_status_get_state()),
    };

    time(&now);
    localtime_r(&now, &model.local_time);
    model.time_known = cyd_clock_app_time_is_synced(&model.local_time);
    model.sync = cyd_clock_app_view_sync(&model.last_sync_at);
    model.wifi = cyd_clock_app_view_wifi();
    cyd_clock_view_build(&s_clock_screen, &model);
    return cyd_ui_submit(&s_clock_screen);
}

static bool cyd_clock_app_should_enter_wifi_setup(void)
{
    wifi_connection_state_t state = WIFI_CONNECTION_STATE_STOPPED;

    if (wifi_connection_get_state(&state) != ESP_OK) {
        return false;
    }

    return state == WIFI_CONNECTION_STATE_SETUP_REQUIRED &&
           (wifi_connection_is_setup_requested_explicitly() ||
            !cyd_clock_app_has_time_sync_success());
}

static bool cyd_clock_app_should_show_wifi_failed(void)
{
    wifi_connection_state_t state = WIFI_CONNECTION_STATE_STOPPED;

    if (cyd_clock_app_has_time_sync_success()) {
        return false;
    }

    if (wifi_connection_get_state(&state) != ESP_OK) {
        return false;
    }

    return state == WIFI_CONNECTION_STATE_FAILED;
}

static cyd_clock_view_failure_t cyd_clock_app_view_failure(esp32_wifi_sta_failure_reason_t reason)
{
    switch (reason) {
    case ESP32_WIFI_STA_FAILURE_NO_SAVED_PROFILE: return CYD_CLOCK_VIEW_FAILURE_NO_SAVED_PROFILE;
    case ESP32_WIFI_STA_FAILURE_NO_AP_IN_RANGE: return CYD_CLOCK_VIEW_FAILURE_NO_AP_IN_RANGE;
    case ESP32_WIFI_STA_FAILURE_AUTH: return CYD_CLOCK_VIEW_FAILURE_AUTH;
    case ESP32_WIFI_STA_FAILURE_TIMEOUT: return CYD_CLOCK_VIEW_FAILURE_TIMEOUT;
    case ESP32_WIFI_STA_FAILURE_CONNECT: return CYD_CLOCK_VIEW_FAILURE_CONNECT;
    default: return CYD_CLOCK_VIEW_FAILURE_UNKNOWN;
    }
}

/*
 * The screens below log a failed draw instead of ESP_ERROR_CHECK-ing it: a
 * display hiccup inside an app must not reboot the device (app_shell.h).
 */
static void cyd_clock_app_log_on_error(esp_err_t err, const char *what)
{
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "%s failed: %s", what, esp_err_to_name(err));
    }
}

static void cyd_clock_app_begin_wifi_setup(void)
{
    ESP_LOGI(TAG, "switching to Wi-Fi setup app");
    cyd_clock_app_log_on_error(app_shell_switch_to(cyd_wifi_setup_get_app()), "switch to Wi-Fi setup");
}

static cyd_clock_app_mode_t cyd_clock_app_run_wifi_failed(void)
{
    const cyd_clock_view_model_t model = {
        .screen = CYD_CLOCK_VIEW_SCREEN_WIFI_FAILED,
        .failure = cyd_clock_app_view_failure(wifi_connection_get_last_failure_reason()),
    };
    cyd_clock_mode_button_tracker_t tracker = { 0 };

    cyd_clock_view_build(&s_clock_screen, &model);
    cyd_clock_app_log_on_error(cyd_ui_submit(&s_clock_screen), "show Wi-Fi failed screen");

    while (true) {
        cyd_input_event_t event = { 0 };
        if (cyd_input_read_event(&event, pdMS_TO_TICKS(CYD_CLOCK_APP_IDLE_POLL_MS)) != ESP_OK) {
            if (app_shell_is_idle_timeout_elapsed()) {
                return CYD_CLOCK_APP_MODE_CLOCK;
            }
            continue;
        }
        uint16_t action_id = 0;
        if (cyd_clock_app_touch_confirmed_action(&event, &tracker, &action_id)) {
            if (action_id == CYD_CLOCK_APP_ACTION_WIFI_RETRY) {
                ESP_LOGI(TAG, "retrying saved Wi-Fi profiles");
                if (wifi_connection_retry_connection_without_setup_async() != ESP_OK) {
                    ESP_LOGW(TAG, "failed to start Wi-Fi retry");
                    return CYD_CLOCK_APP_MODE_WIFI_FAILED;
                }
                time_sync_request_soon_and_release_wifi();
                return CYD_CLOCK_APP_MODE_WIFI_RETRYING;
            }
            if (action_id == CYD_CLOCK_APP_ACTION_WIFI_SETUP) {
                cyd_clock_app_begin_wifi_setup();
                return CYD_CLOCK_APP_MODE_CLOCK;
            }
        }

        if (cyd_clock_app_touch_is_tap(&event)) {
            continue;
        }
    }
}

static cyd_clock_app_mode_t cyd_clock_app_run_wifi_retrying(void)
{
    wifi_connection_progress_t last_progress = {
        .phase = (wifi_connection_progress_phase_t)-1,
    };

    while (true) {
        cyd_input_event_t event = { 0 };
        (void)cyd_input_read_event(&event, pdMS_TO_TICKS(CYD_CLOCK_APP_INPUT_POLL_MS));
        (void)cyd_clock_app_touch_is_tap(&event);

        wifi_connection_progress_t progress = wifi_connection_get_progress();
        if (progress.phase != last_progress.phase ||
            strncmp(progress.ssid, last_progress.ssid, sizeof(progress.ssid)) != 0) {
            cyd_clock_view_model_t model = {
                .screen = CYD_CLOCK_VIEW_SCREEN_WIFI_RETRYING,
                .retry = CYD_CLOCK_VIEW_RETRY_CONNECTING,
                .retry_ssid = progress.ssid,
            };

            if (progress.phase == WIFI_CONNECTION_PROGRESS_CONNECTING && progress.ssid[0] != '\0') {
                model.retry = CYD_CLOCK_VIEW_RETRY_TRYING;
            } else if (progress.phase == WIFI_CONNECTION_PROGRESS_SEARCHING) {
                model.retry = CYD_CLOCK_VIEW_RETRY_SEARCHING;
            }
            cyd_clock_view_build(&s_clock_screen, &model);
            cyd_clock_app_log_on_error(cyd_ui_submit(&s_clock_screen), "show Wi-Fi retry screen");
            last_progress = progress;
        }

        wifi_connection_state_t state = WIFI_CONNECTION_STATE_STOPPED;
        if (wifi_connection_get_state(&state) != ESP_OK) {
            return CYD_CLOCK_APP_MODE_WIFI_FAILED;
        }

        if (state == WIFI_CONNECTION_STATE_CONNECTED) {
            ESP_LOGI(TAG, "saved Wi-Fi retry connected");
            return CYD_CLOCK_APP_MODE_CLOCK;
        }
        if (state == WIFI_CONNECTION_STATE_FAILED ||
            state == WIFI_CONNECTION_STATE_OFF ||
            state == WIFI_CONNECTION_STATE_SETUP_REQUIRED) {
            ESP_LOGW(TAG, "saved Wi-Fi retry ended with state=%d", (int)state);
            return CYD_CLOCK_APP_MODE_WIFI_FAILED;
        }
    }
}

static esp_err_t cyd_clock_app_enter(void *ctx, const app_shell_app_t *from_app)
{
    (void)ctx;
    s_clock_mode = CYD_CLOCK_APP_MODE_CLOCK;
    s_clock_needs_redraw = true;
    if (s_clock_tick_queue == NULL) {
        ESP_RETURN_ON_ERROR(time_tick_subscribe(&s_clock_tick_queue), TAG, "time tick subscribe failed");
    }
    if (from_app == NULL && !cyd_input_has_saved_touch_calibration()) {
        ESP_LOGI(TAG, "no saved touch calibration, switching to touch calibration app");
        return app_shell_switch_to(system_touch_calibration_app_get_app());
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
        s_clock_needs_redraw = true;
        return ESP_OK;
    }
    if (s_clock_mode == CYD_CLOCK_APP_MODE_WIFI_RETRYING) {
        s_clock_mode = cyd_clock_app_run_wifi_retrying();
        s_clock_needs_redraw = true;
        return ESP_OK;
    }

    bool redraw = s_clock_needs_redraw;
    redraw |= cyd_clock_app_process_input();
    redraw |= cyd_clock_app_process_time_ticks();

    if (cyd_clock_app_should_enter_wifi_setup()) {
        cyd_clock_app_begin_wifi_setup();
        s_clock_mode = CYD_CLOCK_APP_MODE_CLOCK;
        return ESP_OK;
    }
    if (cyd_clock_app_should_show_wifi_failed()) {
        s_clock_mode = CYD_CLOCK_APP_MODE_WIFI_FAILED;
        return ESP_OK;
    }

    if (redraw) {
        ESP_RETURN_ON_ERROR(cyd_clock_app_show_clock(), TAG, "show clock failed");
        s_clock_needs_redraw = false;
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
