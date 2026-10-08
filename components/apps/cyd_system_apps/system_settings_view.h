#ifndef SYSTEM_SETTINGS_VIEW_H
#define SYSTEM_SETTINGS_VIEW_H

/*
 * The system settings screens, built from a model only.
 *
 * system_settings_app.c reads the services (display brightness, time sync,
 * Wi-Fi, the profile store, NVS health) into a system_settings_view_model_t;
 * this file turns the model into widgets and owns every word on these screens.
 * Keeping the two apart lets the simulator and the host tests build every page
 * and dialog by hand.
 *
 * English contract: system_settings_view_build() calls no service, reads no
 * clock and touches no global state. Service states arrive as the view's own
 * enums below, so this header depends on cyd_display only.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include "cyd_display.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CYD_SETTINGS_APP_ACTION_WIFI 0x2201
#define CYD_SETTINGS_APP_ACTION_BACK 0x2202
#define CYD_SETTINGS_APP_ACTION_BRIGHTNESS_DOWN 0x2203
#define CYD_SETTINGS_APP_ACTION_BRIGHTNESS_UP 0x2204
#define CYD_SETTINGS_APP_ACTION_PREV_PAGE 0x2205
#define CYD_SETTINGS_APP_ACTION_NEXT_PAGE 0x2206
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
#define CYD_SETTINGS_APP_ACTION_SYNC_NOW 0x2217
#define CYD_SETTINGS_APP_ACTION_WIFI_IDLE_DOWN 0x2218
#define CYD_SETTINGS_APP_ACTION_WIFI_IDLE_UP 0x2219
#define CYD_SETTINGS_APP_ACTION_CLEAR_NVS 0x221a
#define CYD_SETTINGS_APP_ACTION_CLEAR_NVS_CANCEL 0x221b
#define CYD_SETTINGS_APP_ACTION_CLEAR_NVS_CONFIRM 0x221c
#define CYD_SETTINGS_APP_ACTION_CLEAR_APP_DATA 0x221d
#define CYD_SETTINGS_APP_ACTION_CLEAR_APP_DATA_CANCEL 0x221e
#define CYD_SETTINGS_APP_ACTION_CLEAR_APP_DATA_CONFIRM 0x221f
#define CYD_SETTINGS_APP_ACTION_IDLE_RETURN_DOWN 0x2220
#define CYD_SETTINGS_APP_ACTION_IDLE_RETURN_UP 0x2221
#define CYD_SETTINGS_APP_ACTION_RESTART_CANCEL 0x2222
#define CYD_SETTINGS_APP_ACTION_RESTART_CONFIRM 0x2223
#define CYD_SETTINGS_APP_ACTION_THEME_DOWN 0x2224
#define CYD_SETTINGS_APP_ACTION_THEME_UP 0x2225
/* + index into the stored profile list */
#define CYD_SETTINGS_APP_ACTION_STORED_SELECT_BASE 0x2300
/* + index into the apps page list */
#define CYD_SETTINGS_APP_ACTION_APP_BASE 0x2400

/* Rows the apps page can show at once. The page has no scrolling, so this is
   also the cap on how many registered apps are reachable from settings. */
#define SYSTEM_SETTINGS_VIEW_APPS_MAX 4
/* Matches WIFI_PROFILE_STORE_MAX_ENTRIES; checked in system_settings_app.c. */
#define SYSTEM_SETTINGS_VIEW_PROFILES_MAX 5

typedef enum {
    SYSTEM_SETTINGS_VIEW_GENERAL = 0,
    SYSTEM_SETTINGS_VIEW_TIME,
    SYSTEM_SETTINGS_VIEW_NETWORK1,
    SYSTEM_SETTINGS_VIEW_NETWORK2,
    SYSTEM_SETTINGS_VIEW_NVS,
    SYSTEM_SETTINGS_VIEW_APPS,
    SYSTEM_SETTINGS_VIEW_STORED_SSIDS,
    SYSTEM_SETTINGS_VIEW_DELETE_SSID_CONFIRM,
    SYSTEM_SETTINGS_VIEW_CLEAR_TOUCH_CALIB_CONFIRM,
    SYSTEM_SETTINGS_VIEW_CLEAR_APP_DATA_CONFIRM,
    SYSTEM_SETTINGS_VIEW_CLEAR_NVS_CONFIRM,
    SYSTEM_SETTINGS_VIEW_RESTART_CONFIRM,
    SYSTEM_SETTINGS_VIEW_MESSAGE, /* full-screen notice, e.g. "restarting" */
} system_settings_view_screen_t;

/* wifi_connection_state_t, plus "the state could not be read". */
typedef enum {
    SYSTEM_SETTINGS_VIEW_WIFI_UNAVAILABLE = 0,
    SYSTEM_SETTINGS_VIEW_WIFI_STOPPED,
    SYSTEM_SETTINGS_VIEW_WIFI_INIT,
    SYSTEM_SETTINGS_VIEW_WIFI_OFF,
    SYSTEM_SETTINGS_VIEW_WIFI_CONNECTING,
    SYSTEM_SETTINGS_VIEW_WIFI_CONNECTED,
    SYSTEM_SETTINGS_VIEW_WIFI_RECONNECTING,
    SYSTEM_SETTINGS_VIEW_WIFI_FAILED,
    SYSTEM_SETTINGS_VIEW_WIFI_SETUP_REQUIRED,
    SYSTEM_SETTINGS_VIEW_WIFI_SETUP_RUNNING,
} system_settings_view_wifi_t;

/* time_sync_state_t */
typedef enum {
    SYSTEM_SETTINGS_VIEW_SYNC_STOPPED = 0,
    SYSTEM_SETTINGS_VIEW_SYNC_IDLE,
    SYSTEM_SETTINGS_VIEW_SYNC_WAITING_WIFI,
    SYSTEM_SETTINGS_VIEW_SYNC_SYNCING,
    SYSTEM_SETTINGS_VIEW_SYNC_RETRY_WAIT,
} system_settings_view_sync_t;

/* The outcome of the last time sync attempt. */
typedef enum {
    SYSTEM_SETTINGS_VIEW_SYNC_LAST_NONE = 0, /* no attempt yet */
    SYSTEM_SETTINGS_VIEW_SYNC_LAST_FAILED,
    SYSTEM_SETTINGS_VIEW_SYNC_LAST_OK,       /* succeeded, time unknown */
    SYSTEM_SETTINGS_VIEW_SYNC_LAST_OK_AT,    /* succeeded at last_sync_at */
} system_settings_view_sync_last_t;

typedef struct {
    system_settings_view_screen_t screen;

    /* Paged screens: the navigation row. */
    const char *page_title;
    size_t page_index;
    size_t page_count;

    /* GENERAL */
    uint8_t brightness_percent;
    bool can_dim;
    bool can_brighten;
    uint16_t idle_return_seconds; /* 0 = never */
    bool can_idle_return_down;
    bool can_idle_return_up;
    const char *theme_name; /* cyd_ui_theme_name() of the selected theme */
    bool can_theme_down;
    bool can_theme_up;

    /* TIME */
    struct tm local_time;
    bool clock_set;             /* the clock holds a real date */
    const char *timezone_label; /* e.g. "日本" */
    bool can_timezone_down;
    bool can_timezone_up;

    /* NETWORK1 / NETWORK2 */
    system_settings_view_wifi_t wifi;
    uint16_t sync_interval_minutes;
    bool can_sync_interval_down;
    bool can_sync_interval_up;
    uint16_t wifi_idle_seconds; /* 0 = never */
    bool can_wifi_idle_down;
    bool can_wifi_idle_up;
    bool sync_now_enabled;
    system_settings_view_sync_t sync;
    system_settings_view_sync_last_t sync_last;
    struct tm last_sync_at;

    /* APPS: titles of the registered apps' settings screens. */
    const char *apps[SYSTEM_SETTINGS_VIEW_APPS_MAX];
    size_t app_count;

    /* STORED_SSIDS / DELETE_SSID_CONFIRM */
    const char *ssids[SYSTEM_SETTINGS_VIEW_PROFILES_MAX];
    size_t ssid_count;
    size_t selected_ssid;

    /* CLEAR_NVS_CONFIRM: NVS holds data this firmware cannot read. */
    bool nvs_force_initialize;
    const char *nvs_problem; /* technical, English; may be empty */

    /* MESSAGE */
    const char *message_title;
    const char *message_detail;
} system_settings_view_model_t;

/* Words for the service states, shared with the system info screens. */
const char *system_settings_view_wifi_text(system_settings_view_wifi_t wifi);
uint16_t system_settings_view_wifi_color(system_settings_view_wifi_t wifi);
const char *system_settings_view_sync_text(system_settings_view_sync_t sync);
/* "前回: 10/8 09:41 成功" and the like ("10/8 09:41 成功" without the prefix);
   `at` is read only for LAST_OK_AT. */
void system_settings_view_format_sync_last(char *out, size_t out_size, system_settings_view_sync_last_t last,
                                           const struct tm *at, bool with_prefix);

/* "日本" for "JST-9" and so on; index into the app's timezone table. */
const char *system_settings_view_timezone_label(size_t index);
size_t system_settings_view_timezone_count(void);
/* The POSIX TZ string of a timezone option. */
const char *system_settings_view_timezone_tz(size_t index);

/* Clears `screen` and builds the screen the model selects. */
void system_settings_view_build(cyd_display_screen_t *screen, const system_settings_view_model_t *model);

#ifdef __cplusplus
}
#endif

#endif
