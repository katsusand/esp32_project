#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_app_desc.h"
#include "esp_check.h"
#include "esp_chip_info.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "app_shell.h"
#include "cyd_alarm.h"
#include "cyd_display.h"
#include "cyd_input.h"
#include "cyd_speaker.h"
#include "cyd_system_apps.h"
#include "cyd_ui.h"
#include "cyd_wifi_setup.h"
#include "time_sync.h"
#include "wifi_manager.h"
#include "wifi_profile_store.h"

#define TAG "cyd_system_apps"
#define CYD_SYSTEM_APPS_INPUT_POLL_MS 50
#define CYD_INFO_APP_ACTION_BACK 0x2101
#define CYD_SETTINGS_APP_ACTION_WIFI 0x2201
#define CYD_SETTINGS_APP_ACTION_BACK 0x2202
#define CYD_SETTINGS_APP_ACTION_BRIGHTNESS_DOWN 0x2203
#define CYD_SETTINGS_APP_ACTION_BRIGHTNESS_UP 0x2204
#define CYD_SETTINGS_APP_ACTION_PREV_PAGE 0x2205
#define CYD_SETTINGS_APP_ACTION_NEXT_PAGE 0x2206
#define CYD_SETTINGS_APP_ACTION_VOLUME_DOWN 0x2207
#define CYD_SETTINGS_APP_ACTION_VOLUME_UP 0x2208
#define CYD_SETTINGS_APP_ACTION_STORED_SSIDS 0x2209
#define CYD_SETTINGS_APP_ACTION_STORED_PREFER 0x220a
#define CYD_SETTINGS_APP_ACTION_STORED_DELETE 0x220b
#define CYD_SETTINGS_APP_ACTION_STORED_CANCEL_DELETE 0x220c
#define CYD_SETTINGS_APP_ACTION_STORED_CONFIRM_DELETE 0x220d
#define CYD_SETTINGS_APP_ACTION_TIME_SYNC_DOWN 0x220e
#define CYD_SETTINGS_APP_ACTION_TIME_SYNC_UP 0x220f
#define CYD_SETTINGS_APP_ACTION_TIMEZONE_DOWN 0x2210
#define CYD_SETTINGS_APP_ACTION_TIMEZONE_UP 0x2211
#define CYD_SETTINGS_APP_ACTION_TOUCH_CALIBRATE 0x2212
#define CYD_SETTINGS_APP_ACTION_CLEAR_TOUCH_CALIB 0x2213
#define CYD_SETTINGS_APP_ACTION_CLEAR_TOUCH_CALIB_CANCEL 0x2214
#define CYD_SETTINGS_APP_ACTION_CLEAR_TOUCH_CALIB_CONFIRM 0x2215
#define CYD_SETTINGS_APP_ACTION_ALARM1_HOUR_DOWN 0x2216
#define CYD_SETTINGS_APP_ACTION_ALARM1_HOUR_UP 0x2217
#define CYD_SETTINGS_APP_ACTION_ALARM1_MINUTE_DOWN 0x2218
#define CYD_SETTINGS_APP_ACTION_ALARM1_MINUTE_UP 0x2219
#define CYD_SETTINGS_APP_ACTION_ALARM2_HOUR_DOWN 0x221a
#define CYD_SETTINGS_APP_ACTION_ALARM2_HOUR_UP 0x221b
#define CYD_SETTINGS_APP_ACTION_ALARM2_MINUTE_DOWN 0x221c
#define CYD_SETTINGS_APP_ACTION_ALARM2_MINUTE_UP 0x221d
#define CYD_SETTINGS_APP_ACTION_ALARM1_WEEKDAY_SUN 0x221e
#define CYD_SETTINGS_APP_ACTION_ALARM1_WEEKDAY_MON 0x221f
#define CYD_SETTINGS_APP_ACTION_ALARM1_WEEKDAY_TUE 0x2220
#define CYD_SETTINGS_APP_ACTION_ALARM1_WEEKDAY_WED 0x2221
#define CYD_SETTINGS_APP_ACTION_ALARM1_WEEKDAY_THU 0x2222
#define CYD_SETTINGS_APP_ACTION_ALARM1_WEEKDAY_FRI 0x2223
#define CYD_SETTINGS_APP_ACTION_ALARM1_WEEKDAY_SAT 0x2224
#define CYD_SETTINGS_APP_ACTION_STORED_SELECT_BASE 0x2300
#define CYD_SYSTEM_APPS_BACK_COL 0
#define CYD_SYSTEM_APPS_BACK_ROW 0
#define CYD_SYSTEM_APPS_BACK_SPAN_COLS 6
#define CYD_SYSTEM_APPS_BACK_SPAN_ROWS 3
#define CYD_SYSTEM_APPS_TITLE_COL 8
#define CYD_SYSTEM_APPS_TITLE_ROW 0
#define CYD_SYSTEM_APPS_TITLE_SPAN_COLS 32
#define CYD_SYSTEM_APPS_TITLE_SPAN_ROWS 2
#define CYD_SETTINGS_PAGE_BUTTON_ROW 26
#define CYD_SETTINGS_PAGE_BUTTON_SPAN_COLS 7
#define CYD_SETTINGS_PAGE_BUTTON_SPAN_ROWS 3
#define CYD_SETTINGS_PAGE_LABEL_COL 12
#define CYD_SETTINGS_PAGE_LABEL_ROW 25
#define CYD_SETTINGS_PAGE_LABEL_SPAN_COLS 16
#define CYD_SETTINGS_PAGE_LABEL_SPAN_ROWS 3
#define CYD_SETTINGS_ITEM_LABEL_COL 2
#define CYD_SETTINGS_ITEM_LABEL_SPAN_COLS 16
#define CYD_SETTINGS_ITEM_VALUE_COL 26
#define CYD_SETTINGS_ITEM_VALUE_SPAN_COLS 8
#define CYD_SETTINGS_ITEM_BUTTON_LEFT_COL 21
#define CYD_SETTINGS_ITEM_BUTTON_RIGHT_COL 35
#define CYD_SETTINGS_ITEM_BUTTON_SPAN_COLS 3
#define CYD_SETTINGS_ITEM_BUTTON_SPAN_ROWS 2
#define CYD_SETTINGS_ITEM_BUTTON_SCALE 1
static const uint8_t CYD_SETTINGS_BRIGHTNESS_LEVELS[] = {
    255, /* 100% */
    191, /* 75% */
    128, /* 50% */
    102, /* 40% */
    77,  /* 30% */
    64,  /* 25% */
    51,  /* 20% */
    38,  /* 15% */
    26,  /* 10% */
    13,  /* 5% */
};

static const uint8_t CYD_SETTINGS_BRIGHTNESS_PERCENTS[] = {
    100,
    75,
    50,
    40,
    30,
    25,
    20,
    15,
    10,
    5,
};

static const uint8_t CYD_SETTINGS_VOLUME_LEVELS[] = {
    100,
    70,
    50,
    35,
    25,
    18,
    12,
    8,
    5,
};

#define CYD_SETTINGS_TIME_SYNC_MINUTES_MIN 1U
#define CYD_SETTINGS_TIME_SYNC_MINUTES_MAX 1440U

typedef struct {
    const char *label;
    const char *tz;
} cyd_settings_timezone_option_t;

static const cyd_settings_timezone_option_t CYD_SETTINGS_TIMEZONE_OPTIONS[] = {
    { "UTC", "UTC0" },
    { "Japan", "JST-9" },
    { "Korea", "KST-9" },
    { "China", "CHN-8" },
    { "Taiwan", "TWN-8" },
    { "India", "IST-5:30" },
    { "Thailand", "ICT-7" },
    { "Singapore", "SGT-8" },
    { "UAE", "GST-4" },
    { "Germany", "DET-1DEST,M3.5.0/2,M10.5.0/3" },
    { "France", "FRT-1FRST,M3.5.0/2,M10.5.0/3" },
    { "UK", "GMT0BST,M3.5.0/1,M10.5.0/2" },
    { "Brazil", "BRT3" },
    { "US-ET", "EST5EDT,M3.2.0/2,M11.1.0/2" },
    { "US-CT", "CST6CDT,M3.2.0/2,M11.1.0/2" },
    { "US-MT", "MST7MDT,M3.2.0/2,M11.1.0/2" },
    { "US-PT", "PST8PDT,M3.2.0/2,M11.1.0/2" },
    { "Australia", "AEST-10AEDT,M10.1.0/2,M4.1.0/3" },
    { "NewZealand", "NZST-12NZDT,M9.5.0/2,M4.1.0/3" },
};

typedef struct {
    bool pending;
    bool long_pressed;
    uint16_t action_id;
} cyd_system_apps_touch_tracker_t;

typedef enum {
    CYD_SETTINGS_PAGE_GENERAL = 0,
    CYD_SETTINGS_PAGE_ALARM1,
    CYD_SETTINGS_PAGE_ALARM2,
    CYD_SETTINGS_PAGE_NETWORK,
    CYD_SETTINGS_PAGE_NVS,
    CYD_SETTINGS_PAGE_COUNT,
} cyd_settings_page_t;

typedef enum {
    CYD_SETTINGS_VIEW_PAGES = 0,
    CYD_SETTINGS_VIEW_STORED_SSIDS,
    CYD_SETTINGS_VIEW_STORED_SSIDS_DELETE_CONFIRM,
    CYD_SETTINGS_VIEW_CLEAR_TOUCH_CALIB_CONFIRM,
} cyd_settings_view_t;

static cyd_display_screen_t s_info_screen;
static cyd_display_screen_t s_settings_screen;
static const app_shell_app_t *s_info_return_app;
static const app_shell_app_t *s_settings_return_app;
static const app_shell_app_t *s_touch_calibration_return_app;
static cyd_system_apps_touch_tracker_t s_info_touch_tracker;
static cyd_system_apps_touch_tracker_t s_settings_touch_tracker;
static cyd_settings_page_t s_settings_page = CYD_SETTINGS_PAGE_GENERAL;
static cyd_settings_view_t s_settings_view = CYD_SETTINGS_VIEW_PAGES;
static wifi_profile_store_entry_t s_settings_profiles[WIFI_PROFILE_STORE_MAX_ENTRIES];
static size_t s_settings_profile_count = 0;
static size_t s_settings_selected_profile = 0;

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

static bool cyd_settings_is_stepper_action(uint16_t action_id)
{
    switch (action_id) {
    case CYD_SETTINGS_APP_ACTION_BRIGHTNESS_DOWN:
    case CYD_SETTINGS_APP_ACTION_BRIGHTNESS_UP:
    case CYD_SETTINGS_APP_ACTION_VOLUME_DOWN:
    case CYD_SETTINGS_APP_ACTION_VOLUME_UP:
    case CYD_SETTINGS_APP_ACTION_TIME_SYNC_DOWN:
    case CYD_SETTINGS_APP_ACTION_TIME_SYNC_UP:
    case CYD_SETTINGS_APP_ACTION_TIMEZONE_DOWN:
    case CYD_SETTINGS_APP_ACTION_TIMEZONE_UP:
    case CYD_SETTINGS_APP_ACTION_ALARM1_HOUR_DOWN:
    case CYD_SETTINGS_APP_ACTION_ALARM1_HOUR_UP:
    case CYD_SETTINGS_APP_ACTION_ALARM1_MINUTE_DOWN:
    case CYD_SETTINGS_APP_ACTION_ALARM1_MINUTE_UP:
    case CYD_SETTINGS_APP_ACTION_ALARM2_HOUR_DOWN:
    case CYD_SETTINGS_APP_ACTION_ALARM2_HOUR_UP:
    case CYD_SETTINGS_APP_ACTION_ALARM2_MINUTE_DOWN:
    case CYD_SETTINGS_APP_ACTION_ALARM2_MINUTE_UP:
        return true;
    default:
        return false;
    }
}

static bool cyd_settings_touch_stepper_action(const cyd_input_event_t *event, uint16_t *action_id)
{
    uint16_t pressed_action_id = 0;

    if (event == NULL || action_id == NULL || event->type != CYD_INPUT_EVENT_TOUCH) {
        return false;
    }

    if (s_settings_view != CYD_SETTINGS_VIEW_PAGES) {
        return false;
    }

    if (s_settings_page != CYD_SETTINGS_PAGE_GENERAL &&
        s_settings_page != CYD_SETTINGS_PAGE_ALARM1 &&
        s_settings_page != CYD_SETTINGS_PAGE_ALARM2) {
        return false;
    }

    if (event->data.touch.action != CYD_INPUT_TOUCH_ACTION_PRESS &&
        event->data.touch.action != CYD_INPUT_TOUCH_ACTION_REPEAT) {
        return false;
    }

    if (!cyd_display_hit_test_action(event->data.touch.x, event->data.touch.y, &pressed_action_id)) {
        return false;
    }

    if (!cyd_settings_is_stepper_action(pressed_action_id)) {
        return false;
    }

    *action_id = pressed_action_id;
    return true;
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

static size_t cyd_settings_find_brightness_index(uint8_t brightness)
{
    size_t best_index = 0;
    uint16_t best_distance = UINT16_MAX;

    for (size_t i = 0; i < sizeof(CYD_SETTINGS_BRIGHTNESS_LEVELS); ++i) {
        uint8_t level = CYD_SETTINGS_BRIGHTNESS_LEVELS[i];
        uint16_t distance = (brightness > level) ? (uint16_t)(brightness - level) : (uint16_t)(level - brightness);
        if (distance < best_distance) {
            best_distance = distance;
            best_index = i;
        }
    }

    return best_index;
}

static size_t cyd_settings_find_volume_index(uint8_t volume_percent)
{
    size_t best_index = 0;
    uint16_t best_distance = UINT16_MAX;

    for (size_t i = 0; i < sizeof(CYD_SETTINGS_VOLUME_LEVELS); ++i) {
        uint8_t level = CYD_SETTINGS_VOLUME_LEVELS[i];
        uint16_t distance = (volume_percent > level) ? (uint16_t)(volume_percent - level) : (uint16_t)(level - volume_percent);
        if (distance < best_distance) {
            best_distance = distance;
            best_index = i;
        }
    }

    return best_index;
}

static uint16_t cyd_settings_time_sync_step_minutes(uint16_t interval_minutes)
{
    if (interval_minutes >= 360U) {
        return 180U;
    }
    if (interval_minutes >= 120U) {
        return 60U;
    }
    if (interval_minutes >= 60U) {
        return 30U;
    }
    if (interval_minutes >= 10U) {
        return 5U;
    }
    return 1U;
}

static uint16_t cyd_settings_time_sync_decrease(uint16_t interval_minutes)
{
    uint16_t step_minutes = cyd_settings_time_sync_step_minutes(interval_minutes);

    if (interval_minutes <= CYD_SETTINGS_TIME_SYNC_MINUTES_MIN + step_minutes - 1U) {
        return CYD_SETTINGS_TIME_SYNC_MINUTES_MIN;
    }
    return interval_minutes - step_minutes;
}

static uint16_t cyd_settings_time_sync_increase(uint16_t interval_minutes)
{
    uint16_t step_minutes = cyd_settings_time_sync_step_minutes(interval_minutes);

    if (interval_minutes >= CYD_SETTINGS_TIME_SYNC_MINUTES_MAX - step_minutes) {
        return CYD_SETTINGS_TIME_SYNC_MINUTES_MAX;
    }
    return interval_minutes + step_minutes;
}

static size_t cyd_settings_find_timezone_index(const char *timezone)
{
    if (timezone == NULL) {
        return 0;
    }

    for (size_t i = 0; i < (sizeof(CYD_SETTINGS_TIMEZONE_OPTIONS) / sizeof(CYD_SETTINGS_TIMEZONE_OPTIONS[0])); ++i) {
        if (strcmp(timezone, CYD_SETTINGS_TIMEZONE_OPTIONS[i].tz) == 0) {
            return i;
        }
    }

    return 0;
}

static const char *cyd_settings_page_title(cyd_settings_page_t page)
{
    switch (page) {
    case CYD_SETTINGS_PAGE_GENERAL:
        return "GENERAL";
    case CYD_SETTINGS_PAGE_ALARM1:
        return "ALARM1";
    case CYD_SETTINGS_PAGE_ALARM2:
        return "ALARM2";
    case CYD_SETTINGS_PAGE_NETWORK:
        return "NETWORK";
    case CYD_SETTINGS_PAGE_NVS:
        return "NVS";
    default:
        return "SETTINGS";
    }
}

static esp_err_t cyd_settings_load_profiles(void)
{
    ESP_RETURN_ON_ERROR(wifi_profile_store_load_entries(s_settings_profiles,
                                                        WIFI_PROFILE_STORE_MAX_ENTRIES,
                                                        &s_settings_profile_count),
                        TAG,
                        "load stored profiles failed");
    if (s_settings_profile_count == 0) {
        s_settings_selected_profile = 0;
    } else if (s_settings_selected_profile >= s_settings_profile_count) {
        s_settings_selected_profile = s_settings_profile_count - 1;
    }
    return ESP_OK;
}

static esp_err_t cyd_settings_app_add_page_nav(cyd_display_screen_t *screen)
{
    char page_line[CYD_DISPLAY_TEXT_MAX_LEN + 1] = { 0 };
    bool can_go_prev = s_settings_page > CYD_SETTINGS_PAGE_GENERAL;
    bool can_go_next = (size_t)s_settings_page + 1 < CYD_SETTINGS_PAGE_COUNT;

    ESP_RETURN_ON_FALSE(screen != NULL, ESP_ERR_INVALID_ARG, TAG, "screen is null");

    snprintf(page_line,
             sizeof(page_line),
             "%s  %u/%u",
             cyd_settings_page_title(s_settings_page),
             (unsigned)s_settings_page + 1,
             (unsigned)CYD_SETTINGS_PAGE_COUNT);

    cyd_ui_add_button_with_fg_enabled(screen,
                                      "<",
                                      2,
                                      CYD_SETTINGS_PAGE_BUTTON_ROW,
                                      CYD_SETTINGS_PAGE_BUTTON_SPAN_COLS,
                                      CYD_SETTINGS_PAGE_BUTTON_SPAN_ROWS,
                                      CYD_UI_COLOR_WHITE,
                                      CYD_UI_COLOR_BLUE,
                                      CYD_UI_COLOR_CYAN,
                                      CYD_SETTINGS_APP_ACTION_PREV_PAGE,
                                      can_go_prev);
    cyd_ui_add_text(screen,
                    page_line,
                    CYD_SETTINGS_PAGE_LABEL_COL,
                    CYD_SETTINGS_PAGE_LABEL_ROW,
                    CYD_SETTINGS_PAGE_LABEL_SPAN_COLS,
                    CYD_SETTINGS_PAGE_LABEL_SPAN_ROWS,
                    CYD_DISPLAY_ALIGN_CENTER,
                    1,
                    CYD_UI_COLOR_LIGHTGREY);
    cyd_ui_add_button_with_fg_enabled(screen,
                                      ">",
                                      31,
                                      CYD_SETTINGS_PAGE_BUTTON_ROW,
                                      CYD_SETTINGS_PAGE_BUTTON_SPAN_COLS,
                                      CYD_SETTINGS_PAGE_BUTTON_SPAN_ROWS,
                                      CYD_UI_COLOR_WHITE,
                                      CYD_UI_COLOR_BLUE,
                                      CYD_UI_COLOR_CYAN,
                                      CYD_SETTINGS_APP_ACTION_NEXT_PAGE,
                                      can_go_next);
    return ESP_OK;
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

static esp_err_t cyd_settings_render_stored_ssids(cyd_display_screen_t *screen)
{
    char hint_line[CYD_DISPLAY_TEXT_MAX_LEN + 1] = { 0 };

    if (s_settings_profile_count == 0) {
        cyd_ui_add_text(screen, "No stored SSIDs", 2, 8, 36, 2, CYD_DISPLAY_ALIGN_LEFT, 1, CYD_UI_COLOR_WHITE);
    } else {
        for (size_t i = 0; i < s_settings_profile_count && i < WIFI_PROFILE_STORE_MAX_ENTRIES; ++i) {
            char ssid_line[CYD_DISPLAY_TEXT_MAX_LEN + 1] = { 0 };
            bool selected = i == s_settings_selected_profile;
            snprintf(ssid_line, sizeof(ssid_line), "%u. %s", (unsigned)(i + 1), s_settings_profiles[i].ssid);
            cyd_ui_add_button_with_fg(screen,
                                      ssid_line,
                                      2,
                                      (uint8_t)(6 + (i * 3)),
                                      36,
                                      2,
                                      CYD_UI_COLOR_WHITE,
                                      selected ? CYD_UI_COLOR_BLUE : CYD_UI_COLOR_DIMGREY,
                                      selected ? CYD_UI_COLOR_CYAN : CYD_UI_COLOR_DARKGREY,
                                      (uint16_t)(CYD_SETTINGS_APP_ACTION_STORED_SELECT_BASE + i));
        }
    }

    snprintf(hint_line, sizeof(hint_line), "Top item is preferred");
    cyd_ui_add_text(screen, hint_line, 2, 22, 36, 2, CYD_DISPLAY_ALIGN_LEFT, 1, CYD_UI_COLOR_LIGHTGREY);
    cyd_ui_add_button_with_fg_enabled(screen,
                                      "Prefer",
                                      2,
                                      25,
                                      11,
                                      3,
                                      CYD_UI_COLOR_WHITE,
                                      CYD_UI_COLOR_BLUE,
                                      CYD_UI_COLOR_CYAN,
                                      CYD_SETTINGS_APP_ACTION_STORED_PREFER,
                                      s_settings_profile_count > 0);
    cyd_ui_add_button_with_fg_enabled(screen,
                                      "Delete",
                                      14,
                                      25,
                                      11,
                                      3,
                                      CYD_UI_COLOR_WHITE,
                                      CYD_UI_COLOR_RED,
                                      CYD_UI_COLOR_YELLOW,
                                      CYD_SETTINGS_APP_ACTION_STORED_DELETE,
                                      s_settings_profile_count > 0);
    cyd_ui_add_button(screen,
                      "<<",
                      CYD_SYSTEM_APPS_BACK_COL,
                      CYD_SYSTEM_APPS_BACK_ROW,
                      CYD_SYSTEM_APPS_BACK_SPAN_COLS,
                      CYD_SYSTEM_APPS_BACK_SPAN_ROWS,
                      CYD_UI_COLOR_BLUE,
                      CYD_UI_COLOR_CYAN,
                      CYD_SETTINGS_APP_ACTION_BACK);
    return ESP_OK;
}

static esp_err_t cyd_settings_render_delete_confirm(cyd_display_screen_t *screen)
{
    const char *ssid = (s_settings_profile_count > 0) ? s_settings_profiles[s_settings_selected_profile].ssid : "(none)";
    char line[CYD_DISPLAY_TEXT_MAX_LEN + 1] = { 0 };

    cyd_ui_add_text(screen, "Delete stored SSID?", 2, 9, 36, 2, CYD_DISPLAY_ALIGN_LEFT, 1, CYD_UI_COLOR_WHITE);
    snprintf(line, sizeof(line), "%s", ssid);
    cyd_ui_add_text(screen, line, 2, 12, 36, 2, CYD_DISPLAY_ALIGN_LEFT, 1, CYD_UI_COLOR_YELLOW);
    cyd_ui_add_button(screen, "Cancel", 4, 20, 14, 3, CYD_UI_COLOR_BLUE, CYD_UI_COLOR_CYAN, CYD_SETTINGS_APP_ACTION_STORED_CANCEL_DELETE);
    cyd_ui_add_button(screen, "Delete", 22, 20, 14, 3, CYD_UI_COLOR_RED, CYD_UI_COLOR_YELLOW, CYD_SETTINGS_APP_ACTION_STORED_CONFIRM_DELETE);
    return ESP_OK;
}

static esp_err_t cyd_settings_render_clear_touch_calib_confirm(cyd_display_screen_t *screen)
{
    cyd_ui_add_text(screen, "Clear touch calibration?", 2, 9, 36, 2, CYD_DISPLAY_ALIGN_LEFT, 1, CYD_UI_COLOR_WHITE);
    cyd_ui_add_text(screen, "Device will restart", 2, 12, 36, 2, CYD_DISPLAY_ALIGN_LEFT, 1, CYD_UI_COLOR_YELLOW);
    cyd_ui_add_button(screen,
                      "Cancel",
                      4,
                      20,
                      14,
                      3,
                      CYD_UI_COLOR_BLUE,
                      CYD_UI_COLOR_CYAN,
                      CYD_SETTINGS_APP_ACTION_CLEAR_TOUCH_CALIB_CANCEL);
    cyd_ui_add_button(screen,
                      "OK",
                      22,
                      20,
                      14,
                      3,
                      CYD_UI_COLOR_RED,
                      CYD_UI_COLOR_YELLOW,
                      CYD_SETTINGS_APP_ACTION_CLEAR_TOUCH_CALIB_CONFIRM);
    return ESP_OK;
}

static esp_err_t cyd_settings_render_general_page(cyd_display_screen_t *screen)
{
    char brightness_value[CYD_DISPLAY_TEXT_MAX_LEN + 1] = { 0 };
    char volume_value[CYD_DISPLAY_TEXT_MAX_LEN + 1] = { 0 };
    char time_sync_value[CYD_DISPLAY_TEXT_MAX_LEN + 1] = { 0 };
    char timezone_value[CYD_DISPLAY_TEXT_MAX_LEN + 1] = { 0 };
    cyd_ui_stepper_row_t rows[4] = { 0 };
    size_t brightness_index = cyd_settings_find_brightness_index(cyd_display_get_brightness());
    size_t volume_index = cyd_settings_find_volume_index(cyd_speaker_get_volume_percent());
    size_t timezone_index = cyd_settings_find_timezone_index(time_sync_get_timezone());
    uint16_t time_sync_minutes = time_sync_get_interval_minutes();
    uint8_t brightness_percent = CYD_SETTINGS_BRIGHTNESS_PERCENTS[brightness_index];
    bool can_decrease = brightness_index + 1 < sizeof(CYD_SETTINGS_BRIGHTNESS_LEVELS);
    bool can_increase = brightness_index > 0;
    bool can_volume_decrease = volume_index + 1 < sizeof(CYD_SETTINGS_VOLUME_LEVELS);
    bool can_volume_increase = volume_index > 0;
    bool can_time_sync_decrease = time_sync_minutes > CYD_SETTINGS_TIME_SYNC_MINUTES_MIN;
    bool can_time_sync_increase = time_sync_minutes < CYD_SETTINGS_TIME_SYNC_MINUTES_MAX;
    bool can_timezone_decrease = timezone_index > 0;
    bool can_timezone_increase =
        timezone_index + 1 < (sizeof(CYD_SETTINGS_TIMEZONE_OPTIONS) / sizeof(CYD_SETTINGS_TIMEZONE_OPTIONS[0]));

    snprintf(brightness_value, sizeof(brightness_value), "%u", (unsigned)brightness_percent);
    snprintf(volume_value, sizeof(volume_value), "%u", (unsigned)CYD_SETTINGS_VOLUME_LEVELS[volume_index]);
    snprintf(time_sync_value, sizeof(time_sync_value), "%um", (unsigned)time_sync_minutes);
    snprintf(timezone_value, sizeof(timezone_value), "%s", CYD_SETTINGS_TIMEZONE_OPTIONS[timezone_index].label);

    rows[0] = (cyd_ui_stepper_row_t){
        .label_text = "LcdBrightness:",
        .value_text = brightness_value,
        .row = 6,
        .label_col = CYD_SETTINGS_ITEM_LABEL_COL,
        .label_span_cols = CYD_SETTINGS_ITEM_LABEL_SPAN_COLS,
        .label_scale = 1,
        .value_col = CYD_SETTINGS_ITEM_VALUE_COL,
        .value_span_cols = CYD_SETTINGS_ITEM_VALUE_SPAN_COLS,
        .value_scale = 2,
        .button_left_col = CYD_SETTINGS_ITEM_BUTTON_LEFT_COL,
        .button_right_col = CYD_SETTINGS_ITEM_BUTTON_RIGHT_COL,
        .button_span_cols = CYD_SETTINGS_ITEM_BUTTON_SPAN_COLS,
        .button_span_rows = CYD_SETTINGS_ITEM_BUTTON_SPAN_ROWS,
        .button_scale = CYD_SETTINGS_ITEM_BUTTON_SCALE,
        .has_button_fg_color = true,
        .button_fg_color = CYD_UI_COLOR_BLACK,
        .has_button_bg_color = true,
        .button_bg_color = CYD_UI_COLOR_GREEN,
        .has_button_border_color = true,
        .button_border_color = CYD_UI_COLOR_LIGHTGREY,
        .decrease_action_id = CYD_SETTINGS_APP_ACTION_BRIGHTNESS_DOWN,
        .increase_action_id = CYD_SETTINGS_APP_ACTION_BRIGHTNESS_UP,
        .can_decrease = can_decrease,
        .can_increase = can_increase,
    };
    rows[1] = (cyd_ui_stepper_row_t){
        .label_text = "Volume:",
        .value_text = volume_value,
        .row = 10,
        .label_col = CYD_SETTINGS_ITEM_LABEL_COL,
        .label_span_cols = CYD_SETTINGS_ITEM_LABEL_SPAN_COLS,
        .label_scale = 1,
        .value_col = CYD_SETTINGS_ITEM_VALUE_COL,
        .value_span_cols = CYD_SETTINGS_ITEM_VALUE_SPAN_COLS,
        .value_scale = 2,
        .button_left_col = CYD_SETTINGS_ITEM_BUTTON_LEFT_COL,
        .button_right_col = CYD_SETTINGS_ITEM_BUTTON_RIGHT_COL,
        .button_span_cols = CYD_SETTINGS_ITEM_BUTTON_SPAN_COLS,
        .button_span_rows = CYD_SETTINGS_ITEM_BUTTON_SPAN_ROWS,
        .button_scale = CYD_SETTINGS_ITEM_BUTTON_SCALE,
        .has_button_fg_color = true,
        .button_fg_color = CYD_UI_COLOR_BLACK,
        .has_button_bg_color = true,
        .button_bg_color = CYD_UI_COLOR_GREEN,
        .has_button_border_color = true,
        .button_border_color = CYD_UI_COLOR_LIGHTGREY,
        .decrease_action_id = CYD_SETTINGS_APP_ACTION_VOLUME_DOWN,
        .increase_action_id = CYD_SETTINGS_APP_ACTION_VOLUME_UP,
        .can_decrease = can_volume_decrease,
        .can_increase = can_volume_increase,
    };
    rows[2] = (cyd_ui_stepper_row_t){
        .label_text = "TimeSyncInterval:",
        .value_text = time_sync_value,
        .row = 14,
        .label_col = CYD_SETTINGS_ITEM_LABEL_COL,
        .label_span_cols = CYD_SETTINGS_ITEM_LABEL_SPAN_COLS,
        .label_scale = 1,
        .value_col = CYD_SETTINGS_ITEM_VALUE_COL,
        .value_span_cols = CYD_SETTINGS_ITEM_VALUE_SPAN_COLS,
        .value_scale = 2,
        .button_left_col = CYD_SETTINGS_ITEM_BUTTON_LEFT_COL,
        .button_right_col = CYD_SETTINGS_ITEM_BUTTON_RIGHT_COL,
        .button_span_cols = CYD_SETTINGS_ITEM_BUTTON_SPAN_COLS,
        .button_span_rows = CYD_SETTINGS_ITEM_BUTTON_SPAN_ROWS,
        .button_scale = CYD_SETTINGS_ITEM_BUTTON_SCALE,
        .has_button_fg_color = true,
        .button_fg_color = CYD_UI_COLOR_BLACK,
        .has_button_bg_color = true,
        .button_bg_color = CYD_UI_COLOR_GREEN,
        .has_button_border_color = true,
        .button_border_color = CYD_UI_COLOR_LIGHTGREY,
        .decrease_action_id = CYD_SETTINGS_APP_ACTION_TIME_SYNC_DOWN,
        .increase_action_id = CYD_SETTINGS_APP_ACTION_TIME_SYNC_UP,
        .can_decrease = can_time_sync_decrease,
        .can_increase = can_time_sync_increase,
    };
    rows[3] = (cyd_ui_stepper_row_t){
        .label_text = "TimeZone:",
        .value_text = timezone_value,
        .row = 18,
        .label_col = CYD_SETTINGS_ITEM_LABEL_COL,
        .label_span_cols = CYD_SETTINGS_ITEM_LABEL_SPAN_COLS,
        .label_scale = 1,
        .value_col = CYD_SETTINGS_ITEM_VALUE_COL,
        .value_span_cols = CYD_SETTINGS_ITEM_VALUE_SPAN_COLS,
        .value_scale = 1,
        .button_left_col = CYD_SETTINGS_ITEM_BUTTON_LEFT_COL,
        .button_right_col = CYD_SETTINGS_ITEM_BUTTON_RIGHT_COL,
        .button_span_cols = CYD_SETTINGS_ITEM_BUTTON_SPAN_COLS,
        .button_span_rows = CYD_SETTINGS_ITEM_BUTTON_SPAN_ROWS,
        .button_scale = CYD_SETTINGS_ITEM_BUTTON_SCALE,
        .has_button_fg_color = true,
        .button_fg_color = CYD_UI_COLOR_BLACK,
        .has_button_bg_color = true,
        .button_bg_color = CYD_UI_COLOR_GREEN,
        .has_button_border_color = true,
        .button_border_color = CYD_UI_COLOR_LIGHTGREY,
        .decrease_action_id = CYD_SETTINGS_APP_ACTION_TIMEZONE_DOWN,
        .increase_action_id = CYD_SETTINGS_APP_ACTION_TIMEZONE_UP,
        .can_decrease = can_timezone_decrease,
        .can_increase = can_timezone_increase,
    };

    for (size_t i = 0; i < (sizeof(rows) / sizeof(rows[0])); ++i) {
        ESP_RETURN_ON_ERROR(cyd_ui_add_stepper_row(screen, &rows[i]), TAG, "add settings row failed");
    }

    cyd_ui_add_button(screen,
                      "Touch Calib",
                      6,
                      22,
                      28,
                      3,
                      CYD_UI_COLOR_BLUE,
                      CYD_UI_COLOR_CYAN,
                      CYD_SETTINGS_APP_ACTION_TOUCH_CALIBRATE);

    return ESP_OK;
}

static esp_err_t cyd_settings_render_alarm_page(cyd_display_screen_t *screen, cyd_alarm_id_t alarm_id)
{
    char hour_value[CYD_DISPLAY_TEXT_MAX_LEN + 1] = { 0 };
    char minute_value[CYD_DISPLAY_TEXT_MAX_LEN + 1] = { 0 };
    cyd_ui_stepper_row_t rows[2] = { 0 };
    cyd_alarm_config_t alarm = { 0 };
    uint16_t hour_down_action = alarm_id == CYD_ALARM_ID_1 ? CYD_SETTINGS_APP_ACTION_ALARM1_HOUR_DOWN
                                                           : CYD_SETTINGS_APP_ACTION_ALARM2_HOUR_DOWN;
    uint16_t hour_up_action = alarm_id == CYD_ALARM_ID_1 ? CYD_SETTINGS_APP_ACTION_ALARM1_HOUR_UP
                                                         : CYD_SETTINGS_APP_ACTION_ALARM2_HOUR_UP;
    uint16_t minute_down_action = alarm_id == CYD_ALARM_ID_1 ? CYD_SETTINGS_APP_ACTION_ALARM1_MINUTE_DOWN
                                                             : CYD_SETTINGS_APP_ACTION_ALARM2_MINUTE_DOWN;
    uint16_t minute_up_action = alarm_id == CYD_ALARM_ID_1 ? CYD_SETTINGS_APP_ACTION_ALARM1_MINUTE_UP
                                                           : CYD_SETTINGS_APP_ACTION_ALARM2_MINUTE_UP;

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
            { "SUN", CYD_ALARM_WEEKDAY_SUNDAY, 0, 5, CYD_SETTINGS_APP_ACTION_ALARM1_WEEKDAY_SUN },
            { "MON", CYD_ALARM_WEEKDAY_MONDAY, 5, 6, CYD_SETTINGS_APP_ACTION_ALARM1_WEEKDAY_MON },
            { "TUE", CYD_ALARM_WEEKDAY_TUESDAY, 11, 5, CYD_SETTINGS_APP_ACTION_ALARM1_WEEKDAY_TUE },
            { "WED", CYD_ALARM_WEEKDAY_WEDNESDAY, 16, 6, CYD_SETTINGS_APP_ACTION_ALARM1_WEEKDAY_WED },
            { "THU", CYD_ALARM_WEEKDAY_THURSDAY, 22, 6, CYD_SETTINGS_APP_ACTION_ALARM1_WEEKDAY_THU },
            { "FRI", CYD_ALARM_WEEKDAY_FRIDAY, 28, 5, CYD_SETTINGS_APP_ACTION_ALARM1_WEEKDAY_FRI },
            { "SAT", CYD_ALARM_WEEKDAY_SATURDAY, 33, 5, CYD_SETTINGS_APP_ACTION_ALARM1_WEEKDAY_SAT },
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
        .label_col = CYD_SETTINGS_ITEM_LABEL_COL,
        .label_span_cols = CYD_SETTINGS_ITEM_LABEL_SPAN_COLS,
        .label_scale = 1,
        .value_col = CYD_SETTINGS_ITEM_VALUE_COL,
        .value_span_cols = CYD_SETTINGS_ITEM_VALUE_SPAN_COLS,
        .value_scale = 2,
        .button_left_col = CYD_SETTINGS_ITEM_BUTTON_LEFT_COL,
        .button_right_col = CYD_SETTINGS_ITEM_BUTTON_RIGHT_COL,
        .button_span_cols = CYD_SETTINGS_ITEM_BUTTON_SPAN_COLS,
        .button_span_rows = CYD_SETTINGS_ITEM_BUTTON_SPAN_ROWS,
        .button_scale = CYD_SETTINGS_ITEM_BUTTON_SCALE,
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
        .label_col = CYD_SETTINGS_ITEM_LABEL_COL,
        .label_span_cols = CYD_SETTINGS_ITEM_LABEL_SPAN_COLS,
        .label_scale = 1,
        .value_col = CYD_SETTINGS_ITEM_VALUE_COL,
        .value_span_cols = CYD_SETTINGS_ITEM_VALUE_SPAN_COLS,
        .value_scale = 2,
        .button_left_col = CYD_SETTINGS_ITEM_BUTTON_LEFT_COL,
        .button_right_col = CYD_SETTINGS_ITEM_BUTTON_RIGHT_COL,
        .button_span_cols = CYD_SETTINGS_ITEM_BUTTON_SPAN_COLS,
        .button_span_rows = CYD_SETTINGS_ITEM_BUTTON_SPAN_ROWS,
        .button_scale = CYD_SETTINGS_ITEM_BUTTON_SCALE,
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

static esp_err_t cyd_settings_render_network_page(cyd_display_screen_t *screen)
{
    char wifi_line[CYD_DISPLAY_TEXT_MAX_LEN + 1] = { 0 };

    cyd_system_apps_format_wifi_status(wifi_line, sizeof(wifi_line));
    cyd_ui_add_text(screen, wifi_line, 2, 7, 36, 2, CYD_DISPLAY_ALIGN_LEFT, 1, CYD_UI_COLOR_WHITE);
    cyd_ui_add_button(screen,
                      "Stored SSIDs",
                      6,
                      12,
                      28,
                      3,
                      CYD_UI_COLOR_DIMGREY,
                      CYD_UI_COLOR_LIGHTGREY,
                      CYD_SETTINGS_APP_ACTION_STORED_SSIDS);
    cyd_ui_add_button(screen,
                      "Wi-Fi Setup",
                      6,
                      17,
                      28,
                      3,
                      CYD_UI_COLOR_BLUE,
                      CYD_UI_COLOR_CYAN,
                      CYD_SETTINGS_APP_ACTION_WIFI);
    return ESP_OK;
}

static esp_err_t cyd_settings_render_nvs_page(cyd_display_screen_t *screen)
{
    cyd_ui_add_text(screen,
                    "Maintenance actions",
                    2,
                    7,
                    36,
                    2,
                    CYD_DISPLAY_ALIGN_LEFT,
                    1,
                    CYD_UI_COLOR_WHITE);
    cyd_ui_add_button(screen,
                      "Clear Touch Calib",
                      4,
                      12,
                      32,
                      3,
                      CYD_UI_COLOR_DIMGREY,
                      CYD_UI_COLOR_LIGHTGREY,
                      CYD_SETTINGS_APP_ACTION_CLEAR_TOUCH_CALIB);
    cyd_ui_add_text(screen,
                    "Delete saved touch calibration only",
                    2,
                    17,
                    36,
                    2,
                    CYD_DISPLAY_ALIGN_LEFT,
                    1,
                    CYD_UI_COLOR_LIGHTGREY);
    return ESP_OK;
}

static esp_err_t cyd_settings_render_pages(cyd_display_screen_t *screen)
{
    if (s_settings_page == CYD_SETTINGS_PAGE_GENERAL) {
        return cyd_settings_render_general_page(screen);
    }
    if (s_settings_page == CYD_SETTINGS_PAGE_ALARM1) {
        return cyd_settings_render_alarm_page(screen, CYD_ALARM_ID_1);
    }
    if (s_settings_page == CYD_SETTINGS_PAGE_ALARM2) {
        return cyd_settings_render_alarm_page(screen, CYD_ALARM_ID_2);
    }
    if (s_settings_page == CYD_SETTINGS_PAGE_NETWORK) {
        return cyd_settings_render_network_page(screen);
    }
    if (s_settings_page == CYD_SETTINGS_PAGE_NVS) {
        return cyd_settings_render_nvs_page(screen);
    }
    return ESP_OK;
}

static esp_err_t cyd_settings_save_pending_values(void)
{
    ESP_RETURN_ON_ERROR(cyd_display_save_brightness(), TAG, "save brightness failed");
    ESP_RETURN_ON_ERROR(cyd_speaker_save_volume(), TAG, "save volume failed");
    ESP_RETURN_ON_ERROR(time_sync_save_interval_minutes(), TAG, "save time sync interval failed");
    return time_sync_save_timezone();
}

static esp_err_t cyd_settings_show_restart_message(const char *title, const char *detail)
{
    cyd_display_screen_t *screen = &s_settings_screen;

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
    cyd_ui_add_text(screen, title, 2, 10, 36, 2, CYD_DISPLAY_ALIGN_LEFT, 1, CYD_UI_COLOR_WHITE);
    cyd_ui_add_text(screen, detail, 2, 14, 36, 2, CYD_DISPLAY_ALIGN_LEFT, 1, CYD_UI_COLOR_YELLOW);
    return cyd_ui_submit(screen);
}

static esp_err_t cyd_settings_app_show(void)
{
    cyd_display_screen_t *screen = &s_settings_screen;

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

    if (s_settings_view == CYD_SETTINGS_VIEW_STORED_SSIDS) {
        ESP_RETURN_ON_ERROR(cyd_settings_render_stored_ssids(screen), TAG, "render stored SSIDs failed");
        return cyd_ui_submit(screen);
    }
    if (s_settings_view == CYD_SETTINGS_VIEW_STORED_SSIDS_DELETE_CONFIRM) {
        ESP_RETURN_ON_ERROR(cyd_settings_render_delete_confirm(screen), TAG, "render delete confirm failed");
        return cyd_ui_submit(screen);
    }
    if (s_settings_view == CYD_SETTINGS_VIEW_CLEAR_TOUCH_CALIB_CONFIRM) {
        ESP_RETURN_ON_ERROR(cyd_settings_render_clear_touch_calib_confirm(screen),
                            TAG,
                            "render clear touch calib confirm failed");
        return cyd_ui_submit(screen);
    }

    ESP_RETURN_ON_ERROR(cyd_settings_render_pages(screen), TAG, "render settings page failed");
    ESP_RETURN_ON_ERROR(cyd_settings_app_add_page_nav(screen), TAG, "settings page nav failed");
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
    if (from_app != NULL &&
        from_app != cyd_wifi_setup_get_app() &&
        from_app != cyd_touch_calibration_app_get_app()) {
        s_settings_return_app = from_app;
    }
    s_settings_page = CYD_SETTINGS_PAGE_GENERAL;
    s_settings_view = CYD_SETTINGS_VIEW_PAGES;
    s_settings_selected_profile = 0;
    ESP_RETURN_ON_ERROR(cyd_settings_load_profiles(), TAG, "load stored profiles failed");
    s_settings_touch_tracker = (cyd_system_apps_touch_tracker_t){ 0 };
    return cyd_settings_app_show();
}

static esp_err_t cyd_settings_refresh(void)
{
    return cyd_settings_app_show();
}

static esp_err_t cyd_settings_handle_stored_ssids_action(uint16_t action_id, bool *handled)
{
    ESP_RETURN_ON_FALSE(handled != NULL, ESP_ERR_INVALID_ARG, TAG, "handled is null");
    *handled = false;

    if (s_settings_view != CYD_SETTINGS_VIEW_STORED_SSIDS) {
        return ESP_OK;
    }

    if (action_id >= CYD_SETTINGS_APP_ACTION_STORED_SELECT_BASE &&
        action_id < (CYD_SETTINGS_APP_ACTION_STORED_SELECT_BASE + WIFI_PROFILE_STORE_MAX_ENTRIES)) {
        size_t selected = (size_t)(action_id - CYD_SETTINGS_APP_ACTION_STORED_SELECT_BASE);
        if (selected < s_settings_profile_count) {
            s_settings_selected_profile = selected;
            ESP_RETURN_ON_ERROR(cyd_settings_refresh(), TAG, "refresh settings failed");
        }
        *handled = true;
        return ESP_OK;
    }

    if (action_id == CYD_SETTINGS_APP_ACTION_STORED_PREFER && s_settings_profile_count > 0) {
        ESP_RETURN_ON_ERROR(wifi_profile_store_set_priority(s_settings_profiles[s_settings_selected_profile].ssid),
                            TAG,
                            "prioritize stored SSID failed");
        ESP_RETURN_ON_ERROR(cyd_settings_load_profiles(), TAG, "reload stored profiles failed");
        ESP_RETURN_ON_ERROR(cyd_settings_refresh(), TAG, "refresh settings failed");
        *handled = true;
        return ESP_OK;
    }

    if (action_id == CYD_SETTINGS_APP_ACTION_STORED_DELETE && s_settings_profile_count > 0) {
        s_settings_view = CYD_SETTINGS_VIEW_STORED_SSIDS_DELETE_CONFIRM;
        ESP_RETURN_ON_ERROR(cyd_settings_refresh(), TAG, "refresh settings failed");
        *handled = true;
        return ESP_OK;
    }

    if (action_id == CYD_SETTINGS_APP_ACTION_BACK) {
        s_settings_view = CYD_SETTINGS_VIEW_PAGES;
        ESP_RETURN_ON_ERROR(cyd_settings_refresh(), TAG, "refresh settings failed");
        *handled = true;
        return ESP_OK;
    }

    return ESP_OK;
}

static esp_err_t cyd_settings_handle_delete_confirm_action(uint16_t action_id, bool *handled)
{
    ESP_RETURN_ON_FALSE(handled != NULL, ESP_ERR_INVALID_ARG, TAG, "handled is null");
    *handled = false;

    if (s_settings_view != CYD_SETTINGS_VIEW_STORED_SSIDS_DELETE_CONFIRM) {
        return ESP_OK;
    }

    if (action_id == CYD_SETTINGS_APP_ACTION_STORED_CANCEL_DELETE) {
        s_settings_view = CYD_SETTINGS_VIEW_STORED_SSIDS;
        ESP_RETURN_ON_ERROR(cyd_settings_refresh(), TAG, "refresh settings failed");
        *handled = true;
        return ESP_OK;
    }

    if (action_id == CYD_SETTINGS_APP_ACTION_STORED_CONFIRM_DELETE && s_settings_profile_count > 0) {
        ESP_RETURN_ON_ERROR(wifi_profile_store_remove(s_settings_profiles[s_settings_selected_profile].ssid),
                            TAG,
                            "remove stored SSID failed");
        s_settings_view = CYD_SETTINGS_VIEW_STORED_SSIDS;
        ESP_RETURN_ON_ERROR(cyd_settings_load_profiles(), TAG, "reload stored profiles failed");
        ESP_RETURN_ON_ERROR(cyd_settings_refresh(), TAG, "refresh settings failed");
        *handled = true;
        return ESP_OK;
    }

    return ESP_OK;
}

static esp_err_t cyd_settings_handle_page_nav_action(uint16_t action_id, bool *handled)
{
    ESP_RETURN_ON_FALSE(handled != NULL, ESP_ERR_INVALID_ARG, TAG, "handled is null");
    *handled = false;

    if (action_id == CYD_SETTINGS_APP_ACTION_WIFI) {
        ESP_RETURN_ON_ERROR(app_shell_switch_to(cyd_wifi_setup_get_app()), TAG, "switch to wifi setup failed");
        *handled = true;
        return ESP_OK;
    }

    if (action_id == CYD_SETTINGS_APP_ACTION_STORED_SSIDS) {
        s_settings_view = CYD_SETTINGS_VIEW_STORED_SSIDS;
        ESP_RETURN_ON_ERROR(cyd_settings_load_profiles(), TAG, "load stored profiles failed");
        ESP_RETURN_ON_ERROR(cyd_settings_refresh(), TAG, "refresh settings failed");
        *handled = true;
        return ESP_OK;
    }

    if (action_id == CYD_SETTINGS_APP_ACTION_PREV_PAGE && s_settings_page > CYD_SETTINGS_PAGE_GENERAL) {
        --s_settings_page;
        ESP_RETURN_ON_ERROR(cyd_settings_refresh(), TAG, "refresh settings failed");
        *handled = true;
        return ESP_OK;
    }

    if (action_id == CYD_SETTINGS_APP_ACTION_NEXT_PAGE &&
        (size_t)s_settings_page + 1 < CYD_SETTINGS_PAGE_COUNT) {
        s_settings_page = (cyd_settings_page_t)((size_t)s_settings_page + 1);
        ESP_RETURN_ON_ERROR(cyd_settings_refresh(), TAG, "refresh settings failed");
        *handled = true;
        return ESP_OK;
    }

    return ESP_OK;
}

static esp_err_t cyd_settings_handle_general_page_action(uint16_t action_id, bool *handled)
{
    ESP_RETURN_ON_FALSE(handled != NULL, ESP_ERR_INVALID_ARG, TAG, "handled is null");
    *handled = false;

    if (s_settings_page != CYD_SETTINGS_PAGE_GENERAL || s_settings_view != CYD_SETTINGS_VIEW_PAGES) {
        return ESP_OK;
    }

    if (action_id == CYD_SETTINGS_APP_ACTION_BRIGHTNESS_DOWN ||
        action_id == CYD_SETTINGS_APP_ACTION_BRIGHTNESS_UP) {
        size_t brightness_index = cyd_settings_find_brightness_index(cyd_display_get_brightness());
        if (action_id == CYD_SETTINGS_APP_ACTION_BRIGHTNESS_DOWN) {
            if (brightness_index + 1 < sizeof(CYD_SETTINGS_BRIGHTNESS_LEVELS)) {
                ++brightness_index;
            }
        } else if (brightness_index > 0) {
            --brightness_index;
        }

        ESP_RETURN_ON_ERROR(cyd_display_set_brightness(CYD_SETTINGS_BRIGHTNESS_LEVELS[brightness_index]),
                            TAG,
                            "set brightness failed");
        ESP_RETURN_ON_ERROR(cyd_settings_refresh(), TAG, "refresh settings failed");
        *handled = true;
        return ESP_OK;
    }

    if (action_id == CYD_SETTINGS_APP_ACTION_VOLUME_DOWN ||
        action_id == CYD_SETTINGS_APP_ACTION_VOLUME_UP) {
        size_t volume_index = cyd_settings_find_volume_index(cyd_speaker_get_volume_percent());
        if (action_id == CYD_SETTINGS_APP_ACTION_VOLUME_DOWN) {
            if (volume_index + 1 < sizeof(CYD_SETTINGS_VOLUME_LEVELS)) {
                ++volume_index;
            }
        } else if (volume_index > 0) {
            --volume_index;
        }

        ESP_RETURN_ON_ERROR(cyd_speaker_set_volume_percent(CYD_SETTINGS_VOLUME_LEVELS[volume_index]),
                            TAG,
                            "set volume failed");
        ESP_RETURN_ON_ERROR(cyd_settings_refresh(), TAG, "refresh settings failed");
        *handled = true;
        return ESP_OK;
    }

    if (action_id == CYD_SETTINGS_APP_ACTION_TIME_SYNC_DOWN ||
        action_id == CYD_SETTINGS_APP_ACTION_TIME_SYNC_UP) {
        uint16_t interval_minutes = time_sync_get_interval_minutes();
        interval_minutes = (action_id == CYD_SETTINGS_APP_ACTION_TIME_SYNC_DOWN)
                               ? cyd_settings_time_sync_decrease(interval_minutes)
                               : cyd_settings_time_sync_increase(interval_minutes);

        ESP_RETURN_ON_ERROR(time_sync_set_interval_minutes(interval_minutes), TAG, "set time sync interval failed");
        ESP_RETURN_ON_ERROR(cyd_settings_refresh(), TAG, "refresh settings failed");
        *handled = true;
        return ESP_OK;
    }

    if (action_id == CYD_SETTINGS_APP_ACTION_TIMEZONE_DOWN ||
        action_id == CYD_SETTINGS_APP_ACTION_TIMEZONE_UP) {
        size_t timezone_index = cyd_settings_find_timezone_index(time_sync_get_timezone());

        if (action_id == CYD_SETTINGS_APP_ACTION_TIMEZONE_DOWN) {
            if (timezone_index > 0) {
                --timezone_index;
            }
        } else if (timezone_index + 1 < (sizeof(CYD_SETTINGS_TIMEZONE_OPTIONS) / sizeof(CYD_SETTINGS_TIMEZONE_OPTIONS[0]))) {
            ++timezone_index;
        }

        ESP_RETURN_ON_ERROR(time_sync_set_timezone(CYD_SETTINGS_TIMEZONE_OPTIONS[timezone_index].tz),
                            TAG,
                            "set timezone failed");
        ESP_RETURN_ON_ERROR(cyd_settings_refresh(), TAG, "refresh settings failed");
        *handled = true;
        return ESP_OK;
    }

    if (action_id == CYD_SETTINGS_APP_ACTION_TOUCH_CALIBRATE) {
        ESP_RETURN_ON_ERROR(app_shell_switch_to(cyd_touch_calibration_app_get_app()),
                            TAG,
                            "switch to touch calibration failed");
        *handled = true;
        return ESP_OK;
    }

    return ESP_OK;
}

static uint8_t cyd_settings_wrap_u8_down(uint8_t value, uint8_t min_value, uint8_t max_value)
{
    return (value <= min_value) ? max_value : (uint8_t)(value - 1U);
}

static uint8_t cyd_settings_wrap_u8_up(uint8_t value, uint8_t min_value, uint8_t max_value)
{
    return (value >= max_value) ? min_value : (uint8_t)(value + 1U);
}

static esp_err_t cyd_settings_handle_alarm_page_action(uint16_t action_id, bool *handled)
{
    cyd_alarm_config_t alarm1 = { 0 };
    cyd_alarm_config_t alarm2 = { 0 };

    ESP_RETURN_ON_FALSE(handled != NULL, ESP_ERR_INVALID_ARG, TAG, "handled is null");
    *handled = false;

    if ((s_settings_page != CYD_SETTINGS_PAGE_ALARM1 &&
         s_settings_page != CYD_SETTINGS_PAGE_ALARM2) ||
        s_settings_view != CYD_SETTINGS_VIEW_PAGES) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(cyd_alarm_get_config(CYD_ALARM_ID_1, &alarm1), TAG, "get alarm1 config failed");
    ESP_RETURN_ON_ERROR(cyd_alarm_get_config(CYD_ALARM_ID_2, &alarm2), TAG, "get alarm2 config failed");

    if (action_id == CYD_SETTINGS_APP_ACTION_ALARM1_HOUR_DOWN ||
        action_id == CYD_SETTINGS_APP_ACTION_ALARM1_HOUR_UP) {
        alarm1.hour = (action_id == CYD_SETTINGS_APP_ACTION_ALARM1_HOUR_DOWN)
                          ? cyd_settings_wrap_u8_down(alarm1.hour, 0, 23)
                          : cyd_settings_wrap_u8_up(alarm1.hour, 0, 23);
        ESP_RETURN_ON_ERROR(cyd_alarm_set_time(CYD_ALARM_ID_1, alarm1.hour, alarm1.minute),
                            TAG,
                            "set alarm1 hour failed");
        ESP_RETURN_ON_ERROR(cyd_settings_refresh(), TAG, "refresh settings failed");
        *handled = true;
        return ESP_OK;
    }

    if (action_id == CYD_SETTINGS_APP_ACTION_ALARM1_MINUTE_DOWN ||
        action_id == CYD_SETTINGS_APP_ACTION_ALARM1_MINUTE_UP) {
        alarm1.minute = (action_id == CYD_SETTINGS_APP_ACTION_ALARM1_MINUTE_DOWN)
                            ? cyd_settings_wrap_u8_down(alarm1.minute, 0, 59)
                            : cyd_settings_wrap_u8_up(alarm1.minute, 0, 59);
        ESP_RETURN_ON_ERROR(cyd_alarm_set_time(CYD_ALARM_ID_1, alarm1.hour, alarm1.minute),
                            TAG,
                            "set alarm1 minute failed");
        ESP_RETURN_ON_ERROR(cyd_settings_refresh(), TAG, "refresh settings failed");
        *handled = true;
        return ESP_OK;
    }

    if (action_id == CYD_SETTINGS_APP_ACTION_ALARM2_HOUR_DOWN ||
        action_id == CYD_SETTINGS_APP_ACTION_ALARM2_HOUR_UP) {
        alarm2.hour = (action_id == CYD_SETTINGS_APP_ACTION_ALARM2_HOUR_DOWN)
                          ? cyd_settings_wrap_u8_down(alarm2.hour, 0, 23)
                          : cyd_settings_wrap_u8_up(alarm2.hour, 0, 23);
        ESP_RETURN_ON_ERROR(cyd_alarm_set_time(CYD_ALARM_ID_2, alarm2.hour, alarm2.minute),
                            TAG,
                            "set alarm2 hour failed");
        ESP_RETURN_ON_ERROR(cyd_settings_refresh(), TAG, "refresh settings failed");
        *handled = true;
        return ESP_OK;
    }

    if (action_id == CYD_SETTINGS_APP_ACTION_ALARM2_MINUTE_DOWN ||
        action_id == CYD_SETTINGS_APP_ACTION_ALARM2_MINUTE_UP) {
        alarm2.minute = (action_id == CYD_SETTINGS_APP_ACTION_ALARM2_MINUTE_DOWN)
                            ? cyd_settings_wrap_u8_down(alarm2.minute, 0, 59)
                            : cyd_settings_wrap_u8_up(alarm2.minute, 0, 59);
        ESP_RETURN_ON_ERROR(cyd_alarm_set_time(CYD_ALARM_ID_2, alarm2.hour, alarm2.minute),
                            TAG,
                            "set alarm2 minute failed");
        ESP_RETURN_ON_ERROR(cyd_settings_refresh(), TAG, "refresh settings failed");
        *handled = true;
        return ESP_OK;
    }

    if (s_settings_page == CYD_SETTINGS_PAGE_ALARM1) {
        uint8_t toggle_mask = 0;

        switch (action_id) {
        case CYD_SETTINGS_APP_ACTION_ALARM1_WEEKDAY_SUN:
            toggle_mask = CYD_ALARM_WEEKDAY_SUNDAY;
            break;
        case CYD_SETTINGS_APP_ACTION_ALARM1_WEEKDAY_MON:
            toggle_mask = CYD_ALARM_WEEKDAY_MONDAY;
            break;
        case CYD_SETTINGS_APP_ACTION_ALARM1_WEEKDAY_TUE:
            toggle_mask = CYD_ALARM_WEEKDAY_TUESDAY;
            break;
        case CYD_SETTINGS_APP_ACTION_ALARM1_WEEKDAY_WED:
            toggle_mask = CYD_ALARM_WEEKDAY_WEDNESDAY;
            break;
        case CYD_SETTINGS_APP_ACTION_ALARM1_WEEKDAY_THU:
            toggle_mask = CYD_ALARM_WEEKDAY_THURSDAY;
            break;
        case CYD_SETTINGS_APP_ACTION_ALARM1_WEEKDAY_FRI:
            toggle_mask = CYD_ALARM_WEEKDAY_FRIDAY;
            break;
        case CYD_SETTINGS_APP_ACTION_ALARM1_WEEKDAY_SAT:
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
            ESP_RETURN_ON_ERROR(cyd_settings_refresh(), TAG, "refresh settings failed");
            *handled = true;
            return ESP_OK;
        }
    }

    return ESP_OK;
}

static esp_err_t cyd_settings_handle_nvs_page_action(uint16_t action_id, bool *handled)
{
    ESP_RETURN_ON_FALSE(handled != NULL, ESP_ERR_INVALID_ARG, TAG, "handled is null");
    *handled = false;

    if (s_settings_view == CYD_SETTINGS_VIEW_CLEAR_TOUCH_CALIB_CONFIRM) {
        if (action_id == CYD_SETTINGS_APP_ACTION_CLEAR_TOUCH_CALIB_CANCEL) {
            s_settings_view = CYD_SETTINGS_VIEW_PAGES;
            ESP_RETURN_ON_ERROR(cyd_settings_refresh(), TAG, "refresh settings failed");
            *handled = true;
            return ESP_OK;
        }

        if (action_id == CYD_SETTINGS_APP_ACTION_CLEAR_TOUCH_CALIB_CONFIRM) {
            ESP_RETURN_ON_ERROR(cyd_input_clear_touch_calibration(), TAG, "clear touch calibration failed");
            ESP_RETURN_ON_ERROR(cyd_input_discard_pending_events(), TAG, "discard input events failed");
            ESP_RETURN_ON_ERROR(cyd_settings_save_pending_values(), TAG, "save settings before restart failed");
            ESP_RETURN_ON_ERROR(cyd_settings_show_restart_message("Touch calibration cleared", "Restarting..."),
                                TAG,
                                "show restart message failed");
            vTaskDelay(pdMS_TO_TICKS(2000));
            *handled = true;
            esp_restart();
            return ESP_OK;
        }

        return ESP_OK;
    }

    if (s_settings_page != CYD_SETTINGS_PAGE_NVS || s_settings_view != CYD_SETTINGS_VIEW_PAGES) {
        return ESP_OK;
    }

    if (action_id == CYD_SETTINGS_APP_ACTION_CLEAR_TOUCH_CALIB) {
        s_settings_view = CYD_SETTINGS_VIEW_CLEAR_TOUCH_CALIB_CONFIRM;
        ESP_RETURN_ON_ERROR(cyd_settings_refresh(), TAG, "refresh settings failed");
        *handled = true;
        return ESP_OK;
    }

    return ESP_OK;
}

static esp_err_t cyd_settings_app_step(void *ctx)
{
    (void)ctx;

    cyd_input_event_t event = { 0 };
    if (cyd_input_read_event(&event, pdMS_TO_TICKS(CYD_SYSTEM_APPS_INPUT_POLL_MS)) == ESP_OK) {
        uint16_t action_id = 0;
        bool handled = false;

        if (cyd_settings_touch_stepper_action(&event, &action_id)) {
            ESP_RETURN_ON_ERROR(cyd_settings_handle_general_page_action(action_id, &handled), TAG, "handle general page failed");
            if (handled) {
                return ESP_OK;
            }
            ESP_RETURN_ON_ERROR(cyd_settings_handle_alarm_page_action(action_id, &handled), TAG, "handle alarm page failed");
            return ESP_OK;
        }

        if (!cyd_system_apps_touch_confirmed_action(&event, &s_settings_touch_tracker, &action_id)) {
            return ESP_OK;
        }

        ESP_RETURN_ON_ERROR(cyd_settings_handle_stored_ssids_action(action_id, &handled), TAG, "handle stored SSIDs failed");
        if (handled) {
            return ESP_OK;
        }
        ESP_RETURN_ON_ERROR(cyd_settings_handle_delete_confirm_action(action_id, &handled), TAG, "handle delete confirm failed");
        if (handled) {
            return ESP_OK;
        }
        ESP_RETURN_ON_ERROR(cyd_settings_handle_page_nav_action(action_id, &handled), TAG, "handle page nav failed");
        if (handled) {
            return ESP_OK;
        }
        ESP_RETURN_ON_ERROR(cyd_settings_handle_general_page_action(action_id, &handled), TAG, "handle general page failed");
        if (handled) {
            return ESP_OK;
        }
        ESP_RETURN_ON_ERROR(cyd_settings_handle_alarm_page_action(action_id, &handled), TAG, "handle alarm page failed");
        if (handled) {
            return ESP_OK;
        }
        ESP_RETURN_ON_ERROR(cyd_settings_handle_nvs_page_action(action_id, &handled), TAG, "handle NVS page failed");
        if (handled) {
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
    return cyd_settings_save_pending_values();
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

static esp_err_t cyd_touch_calibration_app_enter(void *ctx, const app_shell_app_t *from_app)
{
    (void)ctx;

    ESP_RETURN_ON_FALSE(from_app != NULL, ESP_ERR_INVALID_STATE, TAG, "touch calibration return app not set");
    s_touch_calibration_return_app = from_app;
    ESP_RETURN_ON_ERROR(cyd_input_run_touch_calibration(), TAG, "touch calibration failed");
    ESP_RETURN_ON_ERROR(cyd_input_discard_pending_events(), TAG, "discard input events failed");
    return app_shell_switch_to(s_touch_calibration_return_app);
}

static const app_shell_app_t s_cyd_touch_calibration_shell_app = {
    .id = "touch_calibration",
    .ctx = NULL,
    .enter = cyd_touch_calibration_app_enter,
    .step = NULL,
    .leave = NULL,
};

const app_shell_app_t *cyd_info_app_get_app(void)
{
    return &s_cyd_info_shell_app;
}

const app_shell_app_t *cyd_settings_app_get_app(void)
{
    return &s_cyd_settings_shell_app;
}

const app_shell_app_t *cyd_touch_calibration_app_get_app(void)
{
    return &s_cyd_touch_calibration_shell_app;
}
