#include <stdbool.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_app_desc.h"
#include "esp_check.h"
#include "esp_chip_info.h"
#include "esp_heap_caps.h"
#include "app_shell.h"
#include "cyd_display.h"
#include "cyd_input.h"
#include "cyd_system_apps.h"
#include "cyd_system_apps_internal.h"
#include "cyd_ui.h"
#include "nvs_schema.h"
#include "time_sync.h"
#include "wifi_connection.h"
#include "wifi_profile_store.h"
#include "wifi_rssi_history.h"
#include "system_info_view.h"

#define TAG "cyd_system_apps"
/* Fixed RSSI scale. -30 dBm is effectively "next to the AP", -100 dBm is the
   practical noise floor, so a fixed scale keeps the graph comparable over time
   instead of auto-ranging on every sample. */
#define CYD_INFO_RSSI_MAX_DBM (-30)
#define CYD_INFO_RSSI_MIN_DBM (-100)
#define CYD_INFO_RSSI_WEAK_DBM (-75)

static cyd_display_screen_t s_info_screen;
static const app_shell_app_t *s_info_return_app;
static cyd_system_apps_touch_tracker_t s_info_touch_tracker;
static system_info_view_page_t s_info_page = SYSTEM_INFO_VIEW_OVERVIEW;
static uint16_t s_info_rssi_shown_revision;
static bool s_info_rssi_has_history;
/* The NVS page's namespace names are copied here; the model points into it. */
static char s_info_nvs_names[SYSTEM_INFO_VIEW_NVS_MAX][16];
static system_info_view_model_t s_info_model;

static system_info_view_failure_t cyd_info_view_failure(esp32_wifi_sta_failure_reason_t reason)
{
    switch (reason) {
    case ESP32_WIFI_STA_FAILURE_NO_SAVED_PROFILE: return SYSTEM_INFO_VIEW_FAILURE_NO_SAVED_PROFILE;
    case ESP32_WIFI_STA_FAILURE_NO_AP_IN_RANGE: return SYSTEM_INFO_VIEW_FAILURE_NO_AP_IN_RANGE;
    case ESP32_WIFI_STA_FAILURE_AUTH: return SYSTEM_INFO_VIEW_FAILURE_AUTH;
    case ESP32_WIFI_STA_FAILURE_TIMEOUT: return SYSTEM_INFO_VIEW_FAILURE_TIMEOUT;
    case ESP32_WIFI_STA_FAILURE_CONNECT: return SYSTEM_INFO_VIEW_FAILURE_CONNECT;
    default: return SYSTEM_INFO_VIEW_FAILURE_NONE;
    }
}

static void cyd_info_fill_overview(system_info_view_model_t *m)
{
    const esp_app_desc_t *app_desc = esp_app_get_description();
    esp_chip_info_t chip_info = { 0 };

    esp_chip_info(&chip_info);
    m->app_name = app_desc->project_name;
    m->app_version = app_desc->version;
    m->idf_version = app_desc->idf_ver;
    m->chip_revision = chip_info.revision;
    m->chip_cores = chip_info.cores;
    m->heap_free = (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT);
    m->wifi = cyd_system_apps_view_wifi();
}

static void cyd_info_fill_diag(system_info_view_model_t *m)
{
    wifi_profile_store_entry_t profiles[WIFI_PROFILE_STORE_MAX_ENTRIES];
    size_t profile_count = 0;

    m->heap_free = (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT);
    m->heap_min_free = (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
    m->heap_largest_block = (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    m->wifi_failure = cyd_info_view_failure(wifi_connection_get_last_failure_reason());
    m->sync = cyd_system_apps_view_sync(time_sync_get_state());
    m->sync_last = cyd_system_apps_view_sync_last(&m->last_sync_at);
    m->profiles_readable = wifi_profile_store_load_entries(profiles, WIFI_PROFILE_STORE_MAX_ENTRIES,
                                                           &profile_count) == ESP_OK;
    m->profile_count = (unsigned)profile_count;
    if (cyd_input_has_saved_touch_calibration()) {
        m->touch_calib = SYSTEM_INFO_VIEW_TOUCH_CALIB_SAVED;
    } else if (cyd_input_has_touch_calibration()) {
        m->touch_calib = SYSTEM_INFO_VIEW_TOUCH_CALIB_DEFAULT;
    } else {
        m->touch_calib = SYSTEM_INFO_VIEW_TOUCH_CALIB_NONE;
    }
}

static void cyd_info_fill_wifi(system_info_view_model_t *m)
{
    m->wifi = cyd_system_apps_view_wifi();
    m->wifi_users = wifi_connection_get_active_users();
    m->wifi_users_is_radio_manager = m->wifi_users == WIFI_CONNECTION_USER_RADIO_MANAGER;
    m->wifi_last_user_is_radio_manager = wifi_connection_get_last_user() == WIFI_CONNECTION_USER_RADIO_MANAGER;
    m->wifi_on_seconds = wifi_connection_get_connected_duration_seconds();
    m->wifi_max_on_seconds = wifi_connection_get_connected_duration_high_water_seconds();
    m->wifi_connected_too_long = wifi_connection_get_warning() == WIFI_CONNECTION_WARNING_CONNECTED_TOO_LONG;
    m->wifi_failure = cyd_info_view_failure(wifi_connection_get_last_failure_reason());
}

static void cyd_info_fill_rssi(system_info_view_model_t *m)
{
    const int16_t *samples = NULL;
    uint16_t sample_count = 0;
    uint16_t revision = 0;
    int16_t latest_rssi = 0;
    bool has_history = wifi_rssi_history_get(&samples, &sample_count, &revision);

    m->has_rssi = wifi_rssi_history_get_latest(&latest_rssi);
    m->rssi_now = latest_rssi;
    m->rssi_samples = sample_count;
    if (has_history) {
        m->rssi_graph = (cyd_display_sparkline_t){
            .samples = samples,
            .count = sample_count,
            .revision = revision,
            .min_value = CYD_INFO_RSSI_MIN_DBM,
            .max_value = CYD_INFO_RSSI_MAX_DBM,
            .fill = true,
            .has_baseline = true,
            .baseline_value = CYD_INFO_RSSI_WEAK_DBM,
            .baseline_color = CYD_UI_THEME_DANGER,
            .has_gap_value = true,
            .gap_value = WIFI_RSSI_HISTORY_GAP_DBM,
        };
        s_info_rssi_shown_revision = revision;
    }
    s_info_rssi_has_history = has_history;
}

/*
 * The walk reads flash, so a namespace no component opens any more still shows
 * up here -- which is the point: that is how leftovers from a replaced app are
 * spotted.
 */
static bool cyd_info_nvs_collect(const nvs_schema_namespace_info_t *info, void *ctx)
{
    system_info_view_model_t *m = (system_info_view_model_t *)ctx;

    ++m->nvs_total;
    if (m->nvs_count >= SYSTEM_INFO_VIEW_NVS_MAX) {
        return true; /* keep counting so the total stays honest */
    }
    snprintf(s_info_nvs_names[m->nvs_count], sizeof(s_info_nvs_names[0]), "%s", info->name);
    m->nvs[m->nvs_count] = (system_info_view_nvs_row_t){
        .name = s_info_nvs_names[m->nvs_count],
        .scope = nvs_schema_scope_name(info->scope),
        .entries = (unsigned)info->entry_count,
    };
    ++m->nvs_count;
    return true;
}

static void cyd_info_fill_nvs(system_info_view_model_t *m)
{
    esp_err_t err = nvs_schema_for_each_namespace(cyd_info_nvs_collect, m);

    m->nvs_scan_failed = err != ESP_OK;
    m->nvs_error = err != ESP_OK ? esp_err_to_name(err) : NULL;
}

static esp_err_t cyd_info_app_show(void)
{
    system_info_view_model_t *m = &s_info_model;

    *m = (system_info_view_model_t){ .page = s_info_page };
    switch (s_info_page) {
    case SYSTEM_INFO_VIEW_DIAG:
        cyd_info_fill_diag(m);
        break;
    case SYSTEM_INFO_VIEW_WIFI:
        cyd_info_fill_wifi(m);
        break;
    case SYSTEM_INFO_VIEW_RSSI:
        cyd_info_fill_rssi(m);
        break;
    case SYSTEM_INFO_VIEW_NVS:
        cyd_info_fill_nvs(m);
        break;
    default:
        cyd_info_fill_overview(m);
        break;
    }
    system_info_view_build(&s_info_screen, m);
    return cyd_ui_submit(&s_info_screen);
}

static esp_err_t cyd_info_app_enter(void *ctx, const app_shell_app_t *from_app)
{
    (void)ctx;
    if (from_app != NULL) {
        s_info_return_app = from_app;
    }
    s_info_page = SYSTEM_INFO_VIEW_OVERVIEW;
    s_info_touch_tracker = (cyd_system_apps_touch_tracker_t){ 0 };
    return cyd_info_app_show();
}

static esp_err_t cyd_info_app_step(void *ctx)
{
    (void)ctx;

    cyd_input_event_t event = { 0 };
    if (cyd_input_read_event(&event, pdMS_TO_TICKS(CYD_SYSTEM_APPS_INPUT_POLL_MS)) == ESP_OK) {
        uint16_t action_id = 0;
        if (cyd_system_apps_touch_confirmed_action(&s_info_screen, &event, &s_info_touch_tracker, &action_id)) {
            /* Like the settings pages, navigation stops at both ends. */
            if (action_id == CYD_INFO_APP_ACTION_PREV_PAGE && s_info_page > SYSTEM_INFO_VIEW_OVERVIEW) {
                s_info_page = (system_info_view_page_t)((int)s_info_page - 1);
                return cyd_info_app_show();
            }
            if (action_id == CYD_INFO_APP_ACTION_NEXT_PAGE && s_info_page + 1 < SYSTEM_INFO_VIEW_PAGE_COUNT) {
                s_info_page = (system_info_view_page_t)((int)s_info_page + 1);
                return cyd_info_app_show();
            }
            if (action_id == CYD_INFO_APP_ACTION_BACK) {
                ESP_RETURN_ON_ERROR(app_shell_return_to(s_info_return_app), TAG, "switch back from info failed");
            }
        }
    }

    /*
     * The other info pages only redraw on touch. The RSSI page is a live graph,
     * so it re-renders whenever the sampler reports a new revision. Comparing
     * revisions (rather than redrawing every poll) keeps the dirty-rect diff
     * doing the work it was built for.
     */
    if (s_info_page == SYSTEM_INFO_VIEW_RSSI) {
        uint16_t revision = 0;
        bool has_history = wifi_rssi_history_get(NULL, NULL, &revision);
        if (has_history != s_info_rssi_has_history ||
            (has_history && revision != s_info_rssi_shown_revision)) {
            return cyd_info_app_show();
        }
    }

    return ESP_OK;
}

static esp_err_t cyd_info_app_leave(void *ctx)
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

const app_shell_app_t *system_info_app_get_app(void)
{
    return &s_cyd_info_shell_app;
}
