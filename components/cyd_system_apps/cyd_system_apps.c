#include <stdbool.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_app_desc.h"
#include "esp_check.h"
#include "esp_chip_info.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "app_shell.h"
#include "cyd_display.h"
#include "cyd_input.h"
#include "cyd_system_apps.h"
#include "cyd_ui.h"
#include "cyd_wifi_setup.h"
#include "wifi_manager.h"

#define TAG "cyd_system_apps"
#define CYD_SYSTEM_APPS_INPUT_POLL_MS 50
#define CYD_INFO_APP_ACTION_BACK 0x2101
#define CYD_SETTINGS_APP_ACTION_WIFI 0x2201
#define CYD_SETTINGS_APP_ACTION_BACK 0x2202
#define CYD_SYSTEM_APPS_BACK_COL 0
#define CYD_SYSTEM_APPS_BACK_ROW 0
#define CYD_SYSTEM_APPS_BACK_SPAN_COLS 6
#define CYD_SYSTEM_APPS_BACK_SPAN_ROWS 3
#define CYD_SYSTEM_APPS_TITLE_COL 8
#define CYD_SYSTEM_APPS_TITLE_ROW 0
#define CYD_SYSTEM_APPS_TITLE_SPAN_COLS 32
#define CYD_SYSTEM_APPS_TITLE_SPAN_ROWS 2

typedef struct {
    bool pending;
    bool long_pressed;
    uint16_t action_id;
} cyd_system_apps_touch_tracker_t;

static cyd_display_screen_t s_info_screen;
static cyd_display_screen_t s_settings_screen;
static const app_shell_app_t *s_info_return_app;
static const app_shell_app_t *s_settings_return_app;
static cyd_system_apps_touch_tracker_t s_info_touch_tracker;
static cyd_system_apps_touch_tracker_t s_settings_touch_tracker;

static bool cyd_system_apps_touch_confirmed_action(const cyd_input_event_t *event,
                                                   cyd_system_apps_touch_tracker_t *tracker,
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

static const char *cyd_system_apps_wifi_state_text(wifi_manager_state_t state)
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

static void cyd_system_apps_format_wifi_status(char *status_text, size_t status_size)
{
    wifi_manager_state_t state = WIFI_MANAGER_STATE_STOPPED;

    if (status_text == NULL || status_size == 0) {
        return;
    }

    if (wifi_manager_get_state(&state) != ESP_OK) {
        snprintf(status_text, status_size, "wifi: unavailable");
        return;
    }

    snprintf(status_text, status_size, "%s", cyd_system_apps_wifi_state_text(state));
}

static esp_err_t cyd_info_app_show(void)
{
    const esp_app_desc_t *app_desc = esp_app_get_description();
    esp_chip_info_t chip_info = { 0 };
    char app_line[CYD_DISPLAY_TEXT_MAX_LEN + 1] = { 0 };
    char idf_line[CYD_DISPLAY_TEXT_MAX_LEN + 1] = { 0 };
    char chip_line[CYD_DISPLAY_TEXT_MAX_LEN + 1] = { 0 };
    char heap_line[CYD_DISPLAY_TEXT_MAX_LEN + 1] = { 0 };
    char wifi_line[CYD_DISPLAY_TEXT_MAX_LEN + 1] = { 0 };
    cyd_display_screen_t *screen = &s_info_screen;

    esp_chip_info(&chip_info);
    snprintf(app_line, sizeof(app_line), "app: %.20s %.12s", app_desc->project_name, app_desc->version);
    snprintf(idf_line, sizeof(idf_line), "idf: %.28s", app_desc->idf_ver);
    snprintf(chip_line,
             sizeof(chip_line),
             "chip: rev%u %u cores",
             (unsigned)chip_info.revision,
             (unsigned)chip_info.cores);
    snprintf(heap_line, sizeof(heap_line), "heap: %u bytes", (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT));
    cyd_system_apps_format_wifi_status(wifi_line, sizeof(wifi_line));

    cyd_ui_screen_clear(screen);
    cyd_ui_add_text(screen,
                    "INFO",
                    CYD_SYSTEM_APPS_TITLE_COL,
                    CYD_SYSTEM_APPS_TITLE_ROW,
                    CYD_SYSTEM_APPS_TITLE_SPAN_COLS,
                    CYD_SYSTEM_APPS_TITLE_SPAN_ROWS,
                    CYD_DISPLAY_ALIGN_RIGHT,
                    2,
                    CYD_UI_COLOR_CYAN);
    cyd_ui_add_text(screen, app_line, 2, 8, 36, 2, CYD_DISPLAY_ALIGN_LEFT, 1, CYD_UI_COLOR_WHITE);
    cyd_ui_add_text(screen, idf_line, 2, 11, 36, 2, CYD_DISPLAY_ALIGN_LEFT, 1, CYD_UI_COLOR_WHITE);
    cyd_ui_add_text(screen, chip_line, 2, 14, 36, 2, CYD_DISPLAY_ALIGN_LEFT, 1, CYD_UI_COLOR_WHITE);
    cyd_ui_add_text(screen, heap_line, 2, 17, 36, 2, CYD_DISPLAY_ALIGN_LEFT, 1, CYD_UI_COLOR_WHITE);
    cyd_ui_add_text(screen, wifi_line, 2, 20, 36, 2, CYD_DISPLAY_ALIGN_LEFT, 1, CYD_UI_COLOR_WHITE);
    cyd_ui_add_button(screen,
                      "<<",
                      CYD_SYSTEM_APPS_BACK_COL,
                      CYD_SYSTEM_APPS_BACK_ROW,
                      CYD_SYSTEM_APPS_BACK_SPAN_COLS,
                      CYD_SYSTEM_APPS_BACK_SPAN_ROWS,
                      CYD_UI_COLOR_BLUE,
                      CYD_UI_COLOR_CYAN,
                      CYD_INFO_APP_ACTION_BACK);

    return cyd_ui_submit(screen);
}

static esp_err_t cyd_settings_app_show(void)
{
    char wifi_line[CYD_DISPLAY_TEXT_MAX_LEN + 1] = { 0 };
    cyd_display_screen_t *screen = &s_settings_screen;

    cyd_system_apps_format_wifi_status(wifi_line, sizeof(wifi_line));

    cyd_ui_screen_clear(screen);
    cyd_ui_add_text(screen,
                    "SETTINGS",
                    CYD_SYSTEM_APPS_TITLE_COL,
                    CYD_SYSTEM_APPS_TITLE_ROW,
                    CYD_SYSTEM_APPS_TITLE_SPAN_COLS,
                    CYD_SYSTEM_APPS_TITLE_SPAN_ROWS,
                    CYD_DISPLAY_ALIGN_RIGHT,
                    2,
                    CYD_UI_COLOR_CYAN);
    cyd_ui_add_text(screen, wifi_line, 2, 8, 36, 2, CYD_DISPLAY_ALIGN_LEFT, 1, CYD_UI_COLOR_WHITE);
    cyd_ui_add_button(screen,
                      "Wi-Fi Setup",
                      6,
                      13,
                      28,
                      3,
                      CYD_UI_COLOR_BLUE,
                      CYD_UI_COLOR_CYAN,
                      CYD_SETTINGS_APP_ACTION_WIFI);
    cyd_ui_add_button(screen,
                      "<<",
                      CYD_SYSTEM_APPS_BACK_COL,
                      CYD_SYSTEM_APPS_BACK_ROW,
                      CYD_SYSTEM_APPS_BACK_SPAN_COLS,
                      CYD_SYSTEM_APPS_BACK_SPAN_ROWS,
                      CYD_UI_COLOR_BLUE,
                      CYD_UI_COLOR_CYAN,
                      CYD_SETTINGS_APP_ACTION_BACK);

    return cyd_ui_submit(screen);
}

static esp_err_t cyd_info_app_enter(void *ctx, const app_shell_app_t *from_app)
{
    (void)ctx;
    if (from_app != NULL) {
        s_info_return_app = from_app;
    }
    s_info_touch_tracker = (cyd_system_apps_touch_tracker_t){ 0 };
    return cyd_info_app_show();
}

static esp_err_t cyd_info_app_step(void *ctx)
{
    (void)ctx;

    cyd_input_event_t event = { 0 };
    if (cyd_input_read_event(&event, pdMS_TO_TICKS(CYD_SYSTEM_APPS_INPUT_POLL_MS)) == ESP_OK) {
        uint16_t action_id = 0;
        if (cyd_system_apps_touch_confirmed_action(&event, &s_info_touch_tracker, &action_id) &&
            action_id == CYD_INFO_APP_ACTION_BACK) {
            ESP_RETURN_ON_FALSE(s_info_return_app != NULL, ESP_ERR_INVALID_STATE, TAG, "info return app not set");
            ESP_RETURN_ON_ERROR(app_shell_switch_to(s_info_return_app), TAG, "switch back from info failed");
        }
    }

    return ESP_OK;
}

static esp_err_t cyd_info_app_leave(void *ctx)
{
    (void)ctx;
    return ESP_OK;
}

static esp_err_t cyd_settings_app_enter(void *ctx, const app_shell_app_t *from_app)
{
    (void)ctx;
    if (from_app != NULL && from_app != cyd_wifi_setup_get_app()) {
        s_settings_return_app = from_app;
    }
    s_settings_touch_tracker = (cyd_system_apps_touch_tracker_t){ 0 };
    return cyd_settings_app_show();
}

static esp_err_t cyd_settings_app_step(void *ctx)
{
    (void)ctx;

    cyd_input_event_t event = { 0 };
    if (cyd_input_read_event(&event, pdMS_TO_TICKS(CYD_SYSTEM_APPS_INPUT_POLL_MS)) == ESP_OK) {
        uint16_t action_id = 0;
        if (!cyd_system_apps_touch_confirmed_action(&event, &s_settings_touch_tracker, &action_id)) {
            return ESP_OK;
        }

        if (action_id == CYD_SETTINGS_APP_ACTION_WIFI) {
            ESP_RETURN_ON_ERROR(app_shell_switch_to(cyd_wifi_setup_get_app()), TAG, "switch to wifi setup failed");
            return ESP_OK;
        }

        if (action_id == CYD_SETTINGS_APP_ACTION_BACK) {
            ESP_RETURN_ON_FALSE(s_settings_return_app != NULL, ESP_ERR_INVALID_STATE, TAG, "settings return app not set");
            ESP_RETURN_ON_ERROR(app_shell_switch_to(s_settings_return_app), TAG, "switch back from settings failed");
        }
    }

    return ESP_OK;
}

static esp_err_t cyd_settings_app_leave(void *ctx)
{
    (void)ctx;
    return ESP_OK;
}

static const app_shell_app_t s_cyd_info_shell_app = {
    .id = "info",
    .ctx = NULL,
    .enter = cyd_info_app_enter,
    .step = cyd_info_app_step,
    .leave = cyd_info_app_leave,
};

static const app_shell_app_t s_cyd_settings_shell_app = {
    .id = "settings",
    .ctx = NULL,
    .enter = cyd_settings_app_enter,
    .step = cyd_settings_app_step,
    .leave = cyd_settings_app_leave,
};

const app_shell_app_t *cyd_info_app_get_app(void)
{
    return &s_cyd_info_shell_app;
}

const app_shell_app_t *cyd_settings_app_get_app(void)
{
    return &s_cyd_settings_shell_app;
}
