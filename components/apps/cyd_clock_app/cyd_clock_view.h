#ifndef CYD_CLOCK_VIEW_H
#define CYD_CLOCK_VIEW_H

/*
 * The clock's screens, built from a model only: the clock face, "Wi-Fi
 * failed" and the saved-network retry.
 *
 * The clock face is the main screen and is read at a glance, so its time uses
 * the 48px digits; everything else is 16px.
 *
 * English contract: cyd_clock_view_build() calls no service, reads no clock
 * and touches no global state. The clock is a provisional main app; this view
 * belongs to it and leaves with it.
 */

#include <stdbool.h>
#include <time.h>
#include "cyd_display.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CYD_CLOCK_APP_ACTION_SETTINGS 0x1002
#define CYD_CLOCK_APP_ACTION_INFO 0x1003
#define CYD_CLOCK_APP_ACTION_ALARM 0x1004
#define CYD_CLOCK_APP_ACTION_WIFI_RETRY 0x1005
#define CYD_CLOCK_APP_ACTION_WIFI_SETUP 0x1006

/* Where the time is drawn; tapping there toggles 12/24-hour. */
#define CYD_CLOCK_VIEW_TIME_COL 0
#define CYD_CLOCK_VIEW_TIME_ROW 9
#define CYD_CLOCK_VIEW_TIME_SPAN_COLS CYD_DISPLAY_GRID_COLS
#define CYD_CLOCK_VIEW_TIME_SPAN_ROWS 8

typedef enum {
    CYD_CLOCK_VIEW_SCREEN_FACE = 0,
    CYD_CLOCK_VIEW_SCREEN_WIFI_FAILED,
    CYD_CLOCK_VIEW_SCREEN_WIFI_RETRYING,
} cyd_clock_view_screen_t;

typedef enum {
    CYD_CLOCK_VIEW_ALARM_OFF = 0,
    CYD_CLOCK_VIEW_ALARM_1,
    CYD_CLOCK_VIEW_ALARM_2,
    CYD_CLOCK_VIEW_ALARM_1_2,
} cyd_clock_view_alarm_t;

typedef enum {
    CYD_CLOCK_VIEW_SYNC_NONE = 0,
    CYD_CLOCK_VIEW_SYNC_FAILED,
    CYD_CLOCK_VIEW_SYNC_OK_AT,
} cyd_clock_view_sync_t;

/* wifi_connection_state_t, plus "the state could not be read". */
typedef enum {
    CYD_CLOCK_VIEW_WIFI_UNAVAILABLE = 0,
    CYD_CLOCK_VIEW_WIFI_STOPPED,
    CYD_CLOCK_VIEW_WIFI_INIT,
    CYD_CLOCK_VIEW_WIFI_OFF,
    CYD_CLOCK_VIEW_WIFI_CONNECTING,
    CYD_CLOCK_VIEW_WIFI_CONNECTED,
    CYD_CLOCK_VIEW_WIFI_RECONNECTING,
    CYD_CLOCK_VIEW_WIFI_FAILED,
    CYD_CLOCK_VIEW_WIFI_SETUP_REQUIRED,
    CYD_CLOCK_VIEW_WIFI_SETUP_RUNNING,
} cyd_clock_view_wifi_t;

/* esp32_wifi_sta_failure_reason_t */
typedef enum {
    CYD_CLOCK_VIEW_FAILURE_UNKNOWN = 0,
    CYD_CLOCK_VIEW_FAILURE_NO_SAVED_PROFILE,
    CYD_CLOCK_VIEW_FAILURE_NO_AP_IN_RANGE,
    CYD_CLOCK_VIEW_FAILURE_AUTH,
    CYD_CLOCK_VIEW_FAILURE_TIMEOUT,
    CYD_CLOCK_VIEW_FAILURE_CONNECT,
} cyd_clock_view_failure_t;

typedef enum {
    CYD_CLOCK_VIEW_RETRY_CONNECTING = 0, /* no network named yet */
    CYD_CLOCK_VIEW_RETRY_SEARCHING,
    CYD_CLOCK_VIEW_RETRY_TRYING,         /* trying `retry_ssid` */
} cyd_clock_view_retry_t;

typedef struct {
    cyd_clock_view_screen_t screen;

    /* FACE */
    bool time_known; /* the clock holds a real date */
    struct tm local_time;
    bool use_24_hour;
    cyd_clock_view_sync_t sync;
    struct tm last_sync_at;
    cyd_clock_view_wifi_t wifi;
    cyd_clock_view_alarm_t alarm;
    const cyd_display_bitmap_t *sd_icon; /* NULL: nothing to show */

    /* WIFI_FAILED */
    cyd_clock_view_failure_t failure;

    /* WIFI_RETRYING */
    cyd_clock_view_retry_t retry;
    const char *retry_ssid;
} cyd_clock_view_model_t;

/* Clears `screen` and builds the screen the model selects. */
void cyd_clock_view_build(cyd_display_screen_t *screen, const cyd_clock_view_model_t *model);

#ifdef __cplusplus
}
#endif

#endif
