#ifndef SYSTEM_INFO_VIEW_H
#define SYSTEM_INFO_VIEW_H

/*
 * The system information screens, built from a model only.
 *
 * Headings and item names are Japanese; values and technical terms (heap
 * bytes, RSSI, NVS namespace names, error names) stay as the firmware reports
 * them. system_info_app.c reads the services into the model.
 *
 * English contract: system_info_view_build() calls no service, reads no clock
 * and touches no global state. The sparkline samples it is given must stay
 * valid while the built screen is shown, as with any cyd_display sparkline.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include "cyd_display.h"
#include "system_settings_view.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CYD_INFO_APP_ACTION_BACK 0x2101
#define CYD_INFO_APP_ACTION_PREV_PAGE 0x2102
#define CYD_INFO_APP_ACTION_NEXT_PAGE 0x2103

/* Namespaces the NVS page lists; the rest are counted in the summary. */
#define SYSTEM_INFO_VIEW_NVS_MAX 10

typedef enum {
    SYSTEM_INFO_VIEW_OVERVIEW = 0,
    SYSTEM_INFO_VIEW_DIAG,
    SYSTEM_INFO_VIEW_WIFI,
    SYSTEM_INFO_VIEW_RSSI,
    SYSTEM_INFO_VIEW_NVS,
    SYSTEM_INFO_VIEW_PAGE_COUNT,
} system_info_view_page_t;

/* esp32_wifi_sta_failure_reason_t */
typedef enum {
    SYSTEM_INFO_VIEW_FAILURE_NONE = 0,
    SYSTEM_INFO_VIEW_FAILURE_NO_SAVED_PROFILE,
    SYSTEM_INFO_VIEW_FAILURE_NO_AP_IN_RANGE,
    SYSTEM_INFO_VIEW_FAILURE_AUTH,
    SYSTEM_INFO_VIEW_FAILURE_TIMEOUT,
    SYSTEM_INFO_VIEW_FAILURE_CONNECT,
} system_info_view_failure_t;

typedef enum {
    SYSTEM_INFO_VIEW_TOUCH_CALIB_NONE = 0, /* nothing usable */
    SYSTEM_INFO_VIEW_TOUCH_CALIB_DEFAULT,  /* Kconfig defaults */
    SYSTEM_INFO_VIEW_TOUCH_CALIB_SAVED,
} system_info_view_touch_calib_t;

typedef struct {
    const char *name;  /* namespace */
    const char *scope; /* nvs_schema_scope_name() */
    unsigned entries;
} system_info_view_nvs_row_t;

typedef struct {
    system_info_view_page_t page;

    /* OVERVIEW */
    const char *app_name;
    const char *app_version;
    const char *idf_version;
    unsigned chip_revision;
    unsigned chip_cores;
    unsigned heap_free;
    system_settings_view_wifi_t wifi;

    /* DIAG (heap_free and the sync fields are shared) */
    unsigned heap_min_free;
    unsigned heap_largest_block;
    system_info_view_failure_t wifi_failure;
    system_settings_view_sync_t sync;
    system_settings_view_sync_last_t sync_last;
    struct tm last_sync_at;
    bool profiles_readable;
    unsigned profile_count;
    system_info_view_touch_calib_t touch_calib;

    /* WIFI (wifi and wifi_failure are shared) */
    uint32_t wifi_users;     /* wifi_connection user bit mask */
    bool wifi_users_is_radio_manager;
    bool wifi_last_user_is_radio_manager;
    uint32_t wifi_on_seconds;
    uint32_t wifi_max_on_seconds;
    bool wifi_connected_too_long;

    /* RSSI */
    bool has_rssi;
    int rssi_now;
    unsigned rssi_samples;
    cyd_display_sparkline_t rssi_graph; /* .samples == NULL: no history */

    /* NVS */
    bool nvs_scan_failed;
    const char *nvs_error;
    unsigned nvs_total;
    system_info_view_nvs_row_t nvs[SYSTEM_INFO_VIEW_NVS_MAX];
    size_t nvs_count;
} system_info_view_model_t;

/* Clears `screen` and builds the page the model selects. */
void system_info_view_build(cyd_display_screen_t *screen, const system_info_view_model_t *model);

#ifdef __cplusplus
}
#endif

#endif
