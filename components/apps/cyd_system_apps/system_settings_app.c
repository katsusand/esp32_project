#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"
#include "esp_assert.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs_health.h"
#include "nvs_schema.h"
#include "app_registry.h"
#include "app_shell.h"
#include "cyd_display.h"
#include "cyd_input.h"
#include "cyd_system_apps.h"
#include "cyd_system_apps_internal.h"
#include "cyd_ui.h"
#include "cyd_wifi_setup.h"
#include "radio_manager.h"
#include "system_settings_view.h"
#include "time_sync.h"
#include "wifi_connection.h"
#include "wifi_profile_store.h"

#define TAG "cyd_system_apps"
#ifndef APP_WIFI_STA_ENABLED
#define APP_WIFI_STA_ENABLED 1
#endif
_Static_assert(SYSTEM_SETTINGS_VIEW_PROFILES_MAX == WIFI_PROFILE_STORE_MAX_ENTRIES,
               "the stored SSIDs screen must show every stored profile");

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

#define CYD_SETTINGS_TIME_SYNC_MINUTES_MIN 1U
#define CYD_SETTINGS_TIME_SYNC_MINUTES_MAX 1440U

/*
 * Wi-Fi idle-off steps in seconds, ascending. 0 means never: radio_manager
 * already treats a zero timeout as portMAX_DELAY, so the radio is simply never
 * released for being idle.
 */
static const uint16_t CYD_SETTINGS_WIFI_IDLE_SECONDS[] = {
    0, 30, 60, 180, 300, 600, 900, 1200, 1800, 2700, 3600,
};

typedef enum {
    CYD_SETTINGS_PAGE_GENERAL = 0,
    CYD_SETTINGS_PAGE_TIME,
    CYD_SETTINGS_PAGE_NETWORK1,
    CYD_SETTINGS_PAGE_NETWORK2,
    CYD_SETTINGS_PAGE_NVS,
    CYD_SETTINGS_PAGE_APPS,
    CYD_SETTINGS_PAGE_COUNT,
} cyd_settings_page_t;

typedef enum {
    CYD_SETTINGS_PAGE_GROUP_GENERAL = 0,
    CYD_SETTINGS_PAGE_GROUP_TIME,
    CYD_SETTINGS_PAGE_GROUP_NETWORK,
    CYD_SETTINGS_PAGE_GROUP_NVS,
    CYD_SETTINGS_PAGE_GROUP_APPS,
} cyd_settings_page_group_t;

typedef enum {
    CYD_SETTINGS_VIEW_PAGES = 0,
    CYD_SETTINGS_VIEW_STORED_SSIDS,
    CYD_SETTINGS_VIEW_STORED_SSIDS_DELETE_CONFIRM,
    CYD_SETTINGS_VIEW_CLEAR_TOUCH_CALIB_CONFIRM,
    CYD_SETTINGS_VIEW_CLEAR_NVS_CONFIRM,
    CYD_SETTINGS_VIEW_CLEAR_APP_DATA_CONFIRM,
    CYD_SETTINGS_VIEW_RESTART_CONFIRM,
} cyd_settings_view_t;

typedef enum {
    CYD_SETTINGS_DIRECT_VIEW_NONE = 0,
    CYD_SETTINGS_DIRECT_VIEW_STORED_SSIDS,
    CYD_SETTINGS_DIRECT_VIEW_CLEAR_TOUCH_CALIB_CONFIRM,
    CYD_SETTINGS_DIRECT_VIEW_CLEAR_NVS_CONFIRM,
} cyd_settings_direct_view_t;


typedef struct {
    bool valid;
    time_t now;
    wifi_connection_state_t wifi_state;
    time_sync_state_t time_sync_state;
    bool has_last_attempt;
    esp_err_t last_attempt_status;
    bool has_last_success;
    time_t last_success_at;
    bool sync_now_pending;
} cyd_settings_status_snapshot_t;

static cyd_display_screen_t s_settings_screen;
static const app_shell_app_t *s_settings_return_app;
static cyd_system_apps_touch_tracker_t s_settings_touch_tracker;
static cyd_settings_page_t s_settings_page = CYD_SETTINGS_PAGE_GENERAL;
static cyd_settings_view_t s_settings_view = CYD_SETTINGS_VIEW_PAGES;
static cyd_settings_direct_view_t s_settings_pending_direct_view;
static cyd_settings_direct_view_t s_settings_direct_view;
static portMUX_TYPE s_settings_direct_view_lock = portMUX_INITIALIZER_UNLOCKED;
static wifi_profile_store_entry_t s_settings_profiles[WIFI_PROFILE_STORE_MAX_ENTRIES];
static size_t s_settings_profile_count = 0;
static size_t s_settings_selected_profile = 0;
static bool s_settings_sync_now_pending = false;
static cyd_settings_status_snapshot_t s_settings_status_snapshot = { 0 };
static bool s_settings_force_initialize = false;
static cyd_settings_page_t s_settings_active_pages[CYD_SETTINGS_PAGE_COUNT];
static size_t s_settings_active_page_count = 0;
static size_t s_settings_active_page_index = 0;

/*
 * Everything the shell needs to know about a settings page.
 *
 * English contract: this is the single source for page order, shell metadata,
 * and render/action binding. Page IDs remain in the enum and action semantics
 * remain in the page handlers. `handle_action` runs only for the active page.
 */
typedef struct {
    cyd_settings_page_t id;
    const char *title;
    cyd_settings_page_group_t group;
    bool uses_live_status;
    bool has_steppers;          /* page carries -/+ controls that auto-repeat */
    bool (*is_enabled)(void);   /* NULL means always present */
    /* Fills the page's part of the view model from the services. */
    void (*fill)(system_settings_view_model_t *model);
    system_settings_view_screen_t screen;
    esp_err_t (*handle_action)(uint16_t action_id, bool *handled);
} cyd_settings_page_def_t;

static esp_err_t cyd_settings_refresh(void);

static bool cyd_settings_page_is_enabled(cyd_settings_page_t page);
static bool cyd_settings_page_has_steppers(cyd_settings_page_t page);
static const cyd_settings_page_def_t *cyd_settings_page_def(cyd_settings_page_t page);

/*
 * True for screens that settings itself launches, so coming back from one does
 * not overwrite the real return target.
 *
 * Only settings screens qualify. A registered app is a legitimate place to have
 * arrived from -- matching those here is what broke `<<`, because entering
 * settings from the clock stopped recording the clock as the return app.
 */
static bool cyd_settings_is_app_settings_screen(const app_shell_app_t *app)
{
    if (app == NULL) {
        return false;
    }
    for (size_t i = 0; i < app_registry_count(); ++i) {
        const app_registry_entry_t *entry = app_registry_at(i);
        if (entry != NULL && entry->settings_app == app) {
            return true;
        }
    }
    return false;
}
static bool cyd_settings_page_uses_live_status(cyd_settings_page_t page);

/*
 * True when settings is being re-entered from a screen it opened itself:
 * Wi-Fi setup, touch calibration, or an app's own settings screen.
 *
 * Two things follow. The return target must not be overwritten, otherwise `<<`
 * would bounce back into the sub-screen. And the page the user was on must be
 * preserved -- resetting to GENERAL means pressing back lands them somewhere
 * they were never at. Sub-views such as Stored SSIDs never hit this path
 * because they stay inside this app and never re-run enter().
 */
static bool cyd_settings_is_own_subscreen(const app_shell_app_t *app)
{
    return app != NULL &&
           (app == cyd_wifi_setup_get_app() ||
            app == system_touch_calibration_app_get_app() ||
            cyd_settings_is_app_settings_screen(app));
}

static bool cyd_settings_is_stepper_action(uint16_t action_id)
{
    switch (action_id) {
    case CYD_SETTINGS_APP_ACTION_BRIGHTNESS_DOWN:
    case CYD_SETTINGS_APP_ACTION_BRIGHTNESS_UP:
    case CYD_SETTINGS_APP_ACTION_TIME_SYNC_DOWN:
    case CYD_SETTINGS_APP_ACTION_TIME_SYNC_UP:
    case CYD_SETTINGS_APP_ACTION_TIMEZONE_DOWN:
    case CYD_SETTINGS_APP_ACTION_TIMEZONE_UP:
    case CYD_SETTINGS_APP_ACTION_WIFI_IDLE_DOWN:
    case CYD_SETTINGS_APP_ACTION_WIFI_IDLE_UP:
    case CYD_SETTINGS_APP_ACTION_IDLE_RETURN_DOWN:
    case CYD_SETTINGS_APP_ACTION_IDLE_RETURN_UP:
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

    if (!cyd_settings_page_has_steppers(s_settings_page)) {
        return false;
    }

    if (event->data.touch.action != CYD_INPUT_TOUCH_ACTION_PRESS &&
        event->data.touch.action != CYD_INPUT_TOUCH_ACTION_REPEAT) {
        return false;
    }

    if (!cyd_display_screen_hit_test(&s_settings_screen,
                                     event->data.touch.x,
                                     event->data.touch.y,
                                     &pressed_action_id)) {
        return false;
    }

    if (!cyd_settings_is_stepper_action(pressed_action_id)) {
        return false;
    }

    *action_id = pressed_action_id;
    return true;
}


static void cyd_settings_capture_status_snapshot(cyd_settings_status_snapshot_t *snapshot)
{
    wifi_connection_state_t wifi_state = WIFI_CONNECTION_STATE_STOPPED;
    esp_err_t last_attempt_status = ESP_OK;
    time_t last_success_at = 0;
    time_t now = 0;

    if (snapshot == NULL) {
        return;
    }

    if (s_settings_view == CYD_SETTINGS_VIEW_PAGES && s_settings_page == CYD_SETTINGS_PAGE_TIME) {
        time(&now);
    }

    *snapshot = (cyd_settings_status_snapshot_t){
        .valid = true,
        .now = now,
        .wifi_state = WIFI_CONNECTION_STATE_STOPPED,
        .time_sync_state = time_sync_get_state(),
        .has_last_attempt = time_sync_get_last_attempt_status(&last_attempt_status),
        .last_attempt_status = last_attempt_status,
        .has_last_success = time_sync_get_last_success_at(&last_success_at),
        .last_success_at = last_success_at,
        .sync_now_pending = s_settings_sync_now_pending,
    };

    if (wifi_connection_get_state(&wifi_state) == ESP_OK) {
        snapshot->wifi_state = wifi_state;
    }
}

static bool cyd_settings_status_snapshot_equal(const cyd_settings_status_snapshot_t *left,
                                               const cyd_settings_status_snapshot_t *right)
{
    if (left == NULL || right == NULL) {
        return false;
    }
    if (!left->valid || !right->valid) {
        return false;
    }

    return left->wifi_state == right->wifi_state &&
           left->now == right->now &&
           left->time_sync_state == right->time_sync_state &&
           left->has_last_attempt == right->has_last_attempt &&
           left->last_attempt_status == right->last_attempt_status &&
           left->has_last_success == right->has_last_success &&
           left->last_success_at == right->last_success_at &&
           left->sync_now_pending == right->sync_now_pending;
}

static bool cyd_settings_network_page_enabled(void)
{
    return APP_WIFI_STA_ENABLED != 0;
}

static bool cyd_settings_apps_page_enabled(void)
{
    /* Only present once an installed app actually owns a settings screen; an
       empty page would be a dead end in the page rotation. */
    return app_registry_settings_count() > 0;
}

static const cyd_settings_page_def_t *cyd_settings_page_defs(size_t *count);

static bool cyd_settings_page_is_enabled(cyd_settings_page_t page)
{
    const cyd_settings_page_def_t *def = cyd_settings_page_def(page);

    if (def == NULL) {
        return false;
    }
    return def->is_enabled == NULL || def->is_enabled();
}

static bool cyd_settings_page_has_steppers(cyd_settings_page_t page)
{
    const cyd_settings_page_def_t *def = cyd_settings_page_def(page);

    return def != NULL && def->has_steppers;
}

static cyd_settings_page_group_t cyd_settings_page_group(cyd_settings_page_t page)
{
    const cyd_settings_page_def_t *def = cyd_settings_page_def(page);

    return (def != NULL) ? def->group : CYD_SETTINGS_PAGE_GROUP_GENERAL;
}

static size_t cyd_settings_network_page_index(cyd_settings_page_t page)
{
    size_t table_count = 0;
    const cyd_settings_page_def_t *defs = cyd_settings_page_defs(&table_count);
    size_t index = 0;

    for (size_t i = 0; i < table_count; ++i) {
        if (defs[i].group != CYD_SETTINGS_PAGE_GROUP_NETWORK ||
            !cyd_settings_page_is_enabled(defs[i].id)) {
            continue;
        }

        ++index;
        if (defs[i].id == page) {
            return index;
        }
    }

    return 0;
}

static void cyd_settings_rebuild_active_pages(void)
{
    size_t count = 0;
    size_t active_index = 0;
    bool found_current = false;

    size_t table_count = 0;
    const cyd_settings_page_def_t *defs = cyd_settings_page_defs(&table_count);

    for (size_t i = 0; i < table_count; ++i) {
        cyd_settings_page_t page = defs[i].id;
        if (!cyd_settings_page_is_enabled(page)) {
            continue;
        }

        s_settings_active_pages[count] = page;
        if (page == s_settings_page) {
            active_index = count;
            found_current = true;
        }
        ++count;
    }

    s_settings_active_page_count = count;
    if (count == 0) {
        s_settings_page = CYD_SETTINGS_PAGE_GENERAL;
        s_settings_active_page_index = 0;
        return;
    }

    if (!found_current) {
        s_settings_page = s_settings_active_pages[0];
        active_index = 0;
    }
    s_settings_active_page_index = active_index;
}

static bool cyd_settings_page_uses_live_status(cyd_settings_page_t page)
{
    const cyd_settings_page_def_t *def = cyd_settings_page_def(page);

    return def != NULL && def->uses_live_status;
}

static bool cyd_settings_current_page_uses_live_status(void)
{
    return s_settings_view == CYD_SETTINGS_VIEW_PAGES && cyd_settings_page_uses_live_status(s_settings_page);
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
    uint16_t step_minutes = 1U;

    /* Use the step that the increase operation used to reach this range. */
    if (interval_minutes > 360U) {
        step_minutes = 180U;
    } else if (interval_minutes > 120U) {
        step_minutes = 60U;
    } else if (interval_minutes > 60U) {
        step_minutes = 30U;
    } else if (interval_minutes > 10U) {
        step_minutes = 5U;
    }

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

    for (size_t i = 0; i < system_settings_view_timezone_count(); ++i) {
        if (strcmp(timezone, system_settings_view_timezone_tz(i)) == 0) {
            return i;
        }
    }

    return 0;
}

static const char *cyd_settings_page_title(cyd_settings_page_t page)
{
    const cyd_settings_page_def_t *def = cyd_settings_page_def(page);

    return (def != NULL) ? def->title : "";
}

/* Idle-return is a plain arithmetic range, so it needs no lookup table the way
   the Wi-Fi idle steps do. 0 means never; app_shell already treats a zero
   timeout as "do not auto-return". */
#define CYD_SETTINGS_IDLE_RETURN_STEP 10U
#define CYD_SETTINGS_IDLE_RETURN_MAX 1800U

/* Snaps a stored value onto the step grid so a value written by another build
   (or a Kconfig default outside the range) still lands somewhere usable. */
static uint16_t cyd_settings_snap_idle_return(uint16_t seconds)
{
    if (seconds > CYD_SETTINGS_IDLE_RETURN_MAX) {
        return CYD_SETTINGS_IDLE_RETURN_MAX;
    }
    return (uint16_t)(seconds - (seconds % CYD_SETTINGS_IDLE_RETURN_STEP));
}

#define CYD_SETTINGS_WIFI_IDLE_COUNT \
    (sizeof(CYD_SETTINGS_WIFI_IDLE_SECONDS) / sizeof(CYD_SETTINGS_WIFI_IDLE_SECONDS[0]))

/* Nearest step at or below the stored value, so a value written by another
   build still lands on a usable position instead of snapping to zero. */
static size_t cyd_settings_find_wifi_idle_index(uint16_t seconds)
{
    size_t index = 0;

    for (size_t i = 0; i < CYD_SETTINGS_WIFI_IDLE_COUNT; ++i) {
        if (CYD_SETTINGS_WIFI_IDLE_SECONDS[i] <= seconds) {
            index = i;
        }
    }
    return index;
}

static bool cyd_settings_sync_now_enabled(void)
{
    wifi_connection_state_t state = WIFI_CONNECTION_STATE_STOPPED;

    if (time_sync_is_busy()) {
        return false;
    }

    if (wifi_connection_get_state(&state) != ESP_OK) {
        return false;
    }

    return state == WIFI_CONNECTION_STATE_CONNECTED ||
           state == WIFI_CONNECTION_STATE_FAILED ||
           state == WIFI_CONNECTION_STATE_OFF ||
           state == WIFI_CONNECTION_STATE_SETUP_REQUIRED;
}

static esp_err_t cyd_settings_request_sync_now(void)
{
    wifi_connection_state_t wifi_state = WIFI_CONNECTION_STATE_STOPPED;

    ESP_RETURN_ON_ERROR(wifi_connection_get_state(&wifi_state), TAG, "get wifi state failed");

    if (wifi_state == WIFI_CONNECTION_STATE_CONNECTED) {
        s_settings_sync_now_pending = false;
        time_sync_request_soon();
    } else {
        s_settings_sync_now_pending = true;
        bool retry_requested = false;
        if (wifi_state == WIFI_CONNECTION_STATE_FAILED ||
            wifi_state == WIFI_CONNECTION_STATE_OFF ||
            wifi_state == WIFI_CONNECTION_STATE_SETUP_REQUIRED) {
            if (wifi_connection_retry_connection_without_setup_async() != ESP_OK) {
                ESP_LOGW(TAG, "SYNC NOW failed to start Wi-Fi retry");
                s_settings_sync_now_pending = false;
            } else {
                retry_requested = true;
            }
        }
        if (retry_requested ||
            wifi_state == WIFI_CONNECTION_STATE_CONNECTING ||
            wifi_state == WIFI_CONNECTION_STATE_RECONNECTING) {
            time_sync_request_soon_and_release_wifi();
        }
    }

    ESP_LOGI(TAG, "SYNC NOW requested from settings");
    return ESP_OK;
}

static void cyd_settings_service_sync_now_pending(void)
{
    wifi_connection_state_t wifi_state = WIFI_CONNECTION_STATE_STOPPED;

    if (!s_settings_sync_now_pending) {
        return;
    }
    if (wifi_connection_get_state(&wifi_state) != ESP_OK) {
        return;
    }
    if (wifi_state == WIFI_CONNECTION_STATE_CONNECTED) {
        ESP_LOGI(TAG, "SYNC NOW resumed after Wi-Fi reconnect");
        s_settings_sync_now_pending = false;
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

/* Network pages carry their index in the title itself: "ネットワーク2". */
static void cyd_settings_fill_page_nav(system_settings_view_model_t *m)
{
    static char page_title[CYD_DISPLAY_TEXT_MAX_LEN + 1];
    const char *title = cyd_settings_page_title(s_settings_page);

    if (cyd_settings_page_group(s_settings_page) == CYD_SETTINGS_PAGE_GROUP_NETWORK) {
        snprintf(page_title, sizeof(page_title), "%s%u",
                 title, (unsigned)cyd_settings_network_page_index(s_settings_page));
    } else {
        snprintf(page_title, sizeof(page_title), "%s", title);
    }
    m->page_title = page_title;
    m->page_index = s_settings_active_page_index;
    m->page_count = s_settings_active_page_count;
}

static void cyd_settings_fill_general_page(system_settings_view_model_t *m)
{
    size_t brightness_index = cyd_settings_find_brightness_index(cyd_display_get_brightness());
    uint16_t idle_return_seconds = cyd_settings_snap_idle_return(app_shell_get_idle_return_timeout_seconds());

    m->brightness_percent = CYD_SETTINGS_BRIGHTNESS_PERCENTS[brightness_index];
    m->can_dim = brightness_index + 1 < sizeof(CYD_SETTINGS_BRIGHTNESS_LEVELS);
    m->can_brighten = brightness_index > 0;
    m->idle_return_seconds = idle_return_seconds;
    m->can_idle_return_down = idle_return_seconds > 0;
    m->can_idle_return_up = idle_return_seconds < CYD_SETTINGS_IDLE_RETURN_MAX;
}

static void cyd_settings_fill_time_page(system_settings_view_model_t *m)
{
    size_t timezone_index = cyd_settings_find_timezone_index(time_sync_get_timezone());
    time_t now = 0;

    time(&now);
    localtime_r(&now, &m->local_time);
    m->clock_set = m->local_time.tm_year + 1900 >= 2024;
    m->timezone_label = system_settings_view_timezone_label(timezone_index);
    m->can_timezone_down = timezone_index > 0;
    m->can_timezone_up = timezone_index + 1 < system_settings_view_timezone_count();
}

static void cyd_settings_fill_network1_page(system_settings_view_model_t *m)
{
    m->wifi = cyd_system_apps_view_wifi();
}

static void cyd_settings_fill_network2_page(system_settings_view_model_t *m)
{
    uint16_t wifi_idle_seconds = radio_manager_get_idle_timeout_seconds();
    size_t wifi_idle_index = cyd_settings_find_wifi_idle_index(wifi_idle_seconds);
    uint16_t time_sync_minutes = time_sync_get_interval_minutes();

    m->sync_interval_minutes = time_sync_minutes;
    m->can_sync_interval_down = time_sync_minutes > CYD_SETTINGS_TIME_SYNC_MINUTES_MIN;
    m->can_sync_interval_up = time_sync_minutes < CYD_SETTINGS_TIME_SYNC_MINUTES_MAX;
    m->wifi_idle_seconds = wifi_idle_seconds;
    m->can_wifi_idle_down = wifi_idle_index > 0;
    m->can_wifi_idle_up = wifi_idle_index + 1 < CYD_SETTINGS_WIFI_IDLE_COUNT;
    m->sync_now_enabled = cyd_settings_sync_now_enabled();
    m->sync = cyd_system_apps_view_sync(time_sync_get_state());
    m->sync_last = cyd_system_apps_view_sync_last(&m->last_sync_at);
}

static void cyd_settings_fill_nvs_page(system_settings_view_model_t *m)
{
    (void)m;
}

/*
 * Lists the settings screens owned by installed apps, not the apps themselves.
 * An app without a settings screen is absent here, and an app that is not
 * registered takes its settings screen with it.
 */
static void cyd_settings_fill_apps_page(system_settings_view_model_t *m)
{
    size_t count = app_registry_settings_count();

    if (count > SYSTEM_SETTINGS_VIEW_APPS_MAX) {
        count = SYSTEM_SETTINGS_VIEW_APPS_MAX;
    }
    for (size_t i = 0; i < count; ++i) {
        const app_registry_entry_t *entry = app_registry_settings_at(i);
        m->apps[i] = entry != NULL ? entry->title : NULL;
    }
    m->app_count = count;
}

static void cyd_settings_fill_profiles(system_settings_view_model_t *m)
{
    m->ssid_count = s_settings_profile_count;
    for (size_t i = 0; i < s_settings_profile_count && i < SYSTEM_SETTINGS_VIEW_PROFILES_MAX; ++i) {
        m->ssids[i] = s_settings_profiles[i].ssid;
    }
    m->selected_ssid = s_settings_selected_profile;
}

static esp_err_t cyd_settings_save_pending_values(void)
{
    ESP_RETURN_ON_ERROR(cyd_display_save_brightness(), TAG, "save brightness failed");
    ESP_RETURN_ON_ERROR(app_shell_save_idle_return_timeout_seconds(), TAG, "save idle return timeout failed");
    ESP_RETURN_ON_ERROR(time_sync_save_interval_minutes(), TAG, "save time sync interval failed");
    ESP_RETURN_ON_ERROR(time_sync_save_timezone(), TAG, "save timezone failed");
    ESP_RETURN_ON_ERROR(radio_manager_save_idle_timeout_seconds(), TAG, "save wifi idle timeout failed");
    return ESP_OK;
}

/* Model storage: big enough (two struct tm, string tables) to keep off the stack. */
static system_settings_view_model_t s_settings_model;

static esp_err_t cyd_settings_show_restart_message(const char *title, const char *detail)
{
    s_settings_model = (system_settings_view_model_t){
        .screen = SYSTEM_SETTINGS_VIEW_MESSAGE,
        .message_title = title,
        .message_detail = detail,
    };
    system_settings_view_build(&s_settings_screen, &s_settings_model);
    return cyd_ui_submit(&s_settings_screen);
}

/*
 * Saves what it can, then restarts regardless.
 *
 * English contract: the enclosure has no reachable reset button, so a restart
 * the user confirmed must happen even when a save fails. Aborting on the first
 * failed save left the device on the confirm screen with no way to reboot it
 * from the UI. The failure is logged and shown on the restart message.
 */
static void cyd_settings_save_and_restart(const char *title, uint32_t delay_ms)
{
    (void)cyd_input_discard_pending_events();
    esp_err_t save_err = cyd_settings_save_pending_values();
    if (save_err != ESP_OK) {
        ESP_LOGW(TAG, "saving settings before restart failed: %s", esp_err_to_name(save_err));
    }
    esp_err_t show_err = cyd_settings_show_restart_message(title,
                                                           save_err == ESP_OK ? "再起動しています…"
                                                                              : "設定を保存できませんでした。再起動します…");
    if (show_err != ESP_OK) {
        ESP_LOGW(TAG, "show restart message failed: %s", esp_err_to_name(show_err));
    }
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
    esp_restart();
}

static esp_err_t cyd_settings_app_show(void)
{
    system_settings_view_model_t *m = &s_settings_model;

    *m = (system_settings_view_model_t){ 0 };
    switch (s_settings_view) {
    case CYD_SETTINGS_VIEW_STORED_SSIDS:
        m->screen = SYSTEM_SETTINGS_VIEW_STORED_SSIDS;
        cyd_settings_fill_profiles(m);
        break;
    case CYD_SETTINGS_VIEW_STORED_SSIDS_DELETE_CONFIRM:
        m->screen = SYSTEM_SETTINGS_VIEW_DELETE_SSID_CONFIRM;
        cyd_settings_fill_profiles(m);
        break;
    case CYD_SETTINGS_VIEW_CLEAR_TOUCH_CALIB_CONFIRM:
        m->screen = SYSTEM_SETTINGS_VIEW_CLEAR_TOUCH_CALIB_CONFIRM;
        break;
    case CYD_SETTINGS_VIEW_CLEAR_APP_DATA_CONFIRM:
        m->screen = SYSTEM_SETTINGS_VIEW_CLEAR_APP_DATA_CONFIRM;
        break;
    case CYD_SETTINGS_VIEW_CLEAR_NVS_CONFIRM:
        m->screen = SYSTEM_SETTINGS_VIEW_CLEAR_NVS_CONFIRM;
        m->nvs_force_initialize = s_settings_force_initialize;
        m->nvs_problem = nvs_health_get_summary();
        break;
    case CYD_SETTINGS_VIEW_RESTART_CONFIRM:
        m->screen = SYSTEM_SETTINGS_VIEW_RESTART_CONFIRM;
        break;
    default: {
        const cyd_settings_page_def_t *def = cyd_settings_page_def(s_settings_page);

        ESP_RETURN_ON_FALSE(def != NULL && def->fill != NULL,
                            ESP_ERR_INVALID_STATE,
                            TAG,
                            "settings page definition missing");
        m->screen = def->screen;
        def->fill(m);
        cyd_settings_fill_page_nav(m);
        break;
    }
    }

    system_settings_view_build(&s_settings_screen, m);
    return cyd_ui_submit(&s_settings_screen);
}


static esp_err_t cyd_settings_app_enter(void *ctx, const app_shell_app_t *from_app)
{
    (void)ctx;
    const bool returning_from_subscreen = cyd_settings_is_own_subscreen(from_app);

    s_settings_force_initialize = nvs_health_requires_initialize();
    if (from_app != NULL && !returning_from_subscreen) {
        s_settings_return_app = from_app;
    }
    portENTER_CRITICAL(&s_settings_direct_view_lock);
    s_settings_direct_view = s_settings_pending_direct_view;
    s_settings_pending_direct_view = CYD_SETTINGS_DIRECT_VIEW_NONE;
    portEXIT_CRITICAL(&s_settings_direct_view_lock);

    /* Coming back from a sub-screen resumes the page the user left from. A
       fresh entry from another app starts at the first page. */
    if (!returning_from_subscreen) {
        s_settings_page = CYD_SETTINGS_PAGE_GENERAL;
    }
    s_settings_view = CYD_SETTINGS_VIEW_PAGES;
    if (s_settings_direct_view == CYD_SETTINGS_DIRECT_VIEW_STORED_SSIDS) {
        s_settings_page = CYD_SETTINGS_PAGE_NETWORK1;
        s_settings_view = CYD_SETTINGS_VIEW_STORED_SSIDS;
    } else if (s_settings_direct_view == CYD_SETTINGS_DIRECT_VIEW_CLEAR_TOUCH_CALIB_CONFIRM) {
        s_settings_page = CYD_SETTINGS_PAGE_NVS;
        s_settings_view = CYD_SETTINGS_VIEW_CLEAR_TOUCH_CALIB_CONFIRM;
    } else if (s_settings_direct_view == CYD_SETTINGS_DIRECT_VIEW_CLEAR_NVS_CONFIRM) {
        s_settings_page = CYD_SETTINGS_PAGE_NVS;
        s_settings_view = CYD_SETTINGS_VIEW_CLEAR_NVS_CONFIRM;
    }
    s_settings_selected_profile = 0;
    s_settings_sync_now_pending = false;
    s_settings_status_snapshot = (cyd_settings_status_snapshot_t){ 0 };
    if (!cyd_settings_page_is_enabled(s_settings_page)) {
        s_settings_direct_view = CYD_SETTINGS_DIRECT_VIEW_NONE;
        s_settings_page = CYD_SETTINGS_PAGE_GENERAL;
        s_settings_view = CYD_SETTINGS_VIEW_PAGES;
    }
    cyd_settings_rebuild_active_pages();
    ESP_RETURN_ON_ERROR(cyd_settings_load_profiles(), TAG, "load stored profiles failed");
    s_settings_touch_tracker = (cyd_system_apps_touch_tracker_t){ 0 };
    return cyd_settings_refresh();
}

static esp_err_t cyd_settings_refresh(void)
{
    cyd_settings_rebuild_active_pages();
    ESP_RETURN_ON_ERROR(cyd_settings_app_show(), TAG, "show settings failed");
    cyd_settings_capture_status_snapshot(&s_settings_status_snapshot);
    return ESP_OK;
}

static esp_err_t cyd_settings_refresh_if_live_status_changed(void)
{
    cyd_settings_status_snapshot_t snapshot = { 0 };

    if (!cyd_settings_current_page_uses_live_status()) {
        return ESP_OK;
    }

    cyd_settings_capture_status_snapshot(&snapshot);
    if (cyd_settings_status_snapshot_equal(&snapshot, &s_settings_status_snapshot)) {
        return ESP_OK;
    }

    return cyd_settings_refresh();
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
        if (s_settings_direct_view == CYD_SETTINGS_DIRECT_VIEW_STORED_SSIDS) {
            *handled = true;
            return app_shell_return_to(s_settings_return_app);
        }
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

    if (action_id == CYD_SETTINGS_APP_ACTION_PREV_PAGE && s_settings_active_page_index > 0) {
        --s_settings_active_page_index;
        s_settings_page = s_settings_active_pages[s_settings_active_page_index];
        ESP_RETURN_ON_ERROR(cyd_settings_refresh(), TAG, "refresh settings failed");
        *handled = true;
        return ESP_OK;
    }

    if (action_id == CYD_SETTINGS_APP_ACTION_NEXT_PAGE &&
        s_settings_active_page_index + 1 < s_settings_active_page_count) {
        ++s_settings_active_page_index;
        s_settings_page = s_settings_active_pages[s_settings_active_page_index];
        ESP_RETURN_ON_ERROR(cyd_settings_refresh(), TAG, "refresh settings failed");
        *handled = true;
        return ESP_OK;
    }

    return ESP_OK;
}

static esp_err_t cyd_settings_handle_network1_page_action(uint16_t action_id, bool *handled)
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

    return ESP_OK;
}

static esp_err_t cyd_settings_handle_general_page_action(uint16_t action_id, bool *handled)
{
    ESP_RETURN_ON_FALSE(handled != NULL, ESP_ERR_INVALID_ARG, TAG, "handled is null");
    *handled = false;

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

    if (action_id == CYD_SETTINGS_APP_ACTION_IDLE_RETURN_DOWN ||
        action_id == CYD_SETTINGS_APP_ACTION_IDLE_RETURN_UP) {
        uint16_t seconds = cyd_settings_snap_idle_return(app_shell_get_idle_return_timeout_seconds());

        if (action_id == CYD_SETTINGS_APP_ACTION_IDLE_RETURN_DOWN) {
            seconds = (seconds >= CYD_SETTINGS_IDLE_RETURN_STEP)
                          ? (uint16_t)(seconds - CYD_SETTINGS_IDLE_RETURN_STEP)
                          : 0U;
        } else if (seconds < CYD_SETTINGS_IDLE_RETURN_MAX) {
            seconds = (uint16_t)(seconds + CYD_SETTINGS_IDLE_RETURN_STEP);
        }

        ESP_RETURN_ON_ERROR(app_shell_set_idle_return_timeout_seconds(seconds),
                            TAG,
                            "set idle return timeout failed");
        ESP_RETURN_ON_ERROR(cyd_settings_refresh(), TAG, "refresh settings failed");
        *handled = true;
        return ESP_OK;
    }

    if (action_id == CYD_SETTINGS_APP_ACTION_TOUCH_CALIBRATE) {
        ESP_RETURN_ON_ERROR(app_shell_switch_to(system_touch_calibration_app_get_app()),
                            TAG,
                            "switch to touch calibration failed");
        *handled = true;
        return ESP_OK;
    }

    return ESP_OK;
}

static esp_err_t cyd_settings_handle_apps_page_action(uint16_t action_id, bool *handled)
{
    ESP_RETURN_ON_FALSE(handled != NULL, ESP_ERR_INVALID_ARG, TAG, "handled is null");
    *handled = false;

    if (action_id >= CYD_SETTINGS_APP_ACTION_APP_BASE &&
        action_id < (CYD_SETTINGS_APP_ACTION_APP_BASE + SYSTEM_SETTINGS_VIEW_APPS_MAX)) {
        const app_registry_entry_t *entry =
            app_registry_settings_at((size_t)(action_id - CYD_SETTINGS_APP_ACTION_APP_BASE));
        if (entry != NULL && entry->settings_app != NULL) {
            ESP_RETURN_ON_ERROR(app_shell_switch_to(entry->settings_app),
                                TAG,
                                "switch to app settings failed");
            *handled = true;
        }
        return ESP_OK;
    }

    return ESP_OK;
}

static esp_err_t cyd_settings_handle_time_page_action(uint16_t action_id, bool *handled)
{
    ESP_RETURN_ON_FALSE(handled != NULL, ESP_ERR_INVALID_ARG, TAG, "handled is null");
    *handled = false;

    if (action_id == CYD_SETTINGS_APP_ACTION_TIMEZONE_DOWN ||
        action_id == CYD_SETTINGS_APP_ACTION_TIMEZONE_UP) {
        size_t timezone_index = cyd_settings_find_timezone_index(time_sync_get_timezone());

        if (action_id == CYD_SETTINGS_APP_ACTION_TIMEZONE_DOWN) {
            if (timezone_index > 0) {
                --timezone_index;
            }
        } else if (timezone_index + 1 < system_settings_view_timezone_count()) {
            ++timezone_index;
        }

        ESP_RETURN_ON_ERROR(time_sync_set_timezone(system_settings_view_timezone_tz(timezone_index)),
                            TAG,
                            "set timezone failed");
        ESP_RETURN_ON_ERROR(cyd_settings_refresh(), TAG, "refresh settings failed");
        *handled = true;
        return ESP_OK;
    }

    return ESP_OK;
}

static esp_err_t cyd_settings_handle_network2_page_action(uint16_t action_id, bool *handled)
{
    ESP_RETURN_ON_FALSE(handled != NULL, ESP_ERR_INVALID_ARG, TAG, "handled is null");
    *handled = false;

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

    if (action_id == CYD_SETTINGS_APP_ACTION_WIFI_IDLE_DOWN ||
        action_id == CYD_SETTINGS_APP_ACTION_WIFI_IDLE_UP) {
        size_t index = cyd_settings_find_wifi_idle_index(radio_manager_get_idle_timeout_seconds());

        if (action_id == CYD_SETTINGS_APP_ACTION_WIFI_IDLE_DOWN) {
            if (index > 0) {
                --index;
            }
        } else if (index + 1 < CYD_SETTINGS_WIFI_IDLE_COUNT) {
            ++index;
        }

        ESP_RETURN_ON_ERROR(radio_manager_set_idle_timeout_seconds(CYD_SETTINGS_WIFI_IDLE_SECONDS[index]),
                            TAG,
                            "set wifi idle timeout failed");
        ESP_RETURN_ON_ERROR(cyd_settings_refresh(), TAG, "refresh settings failed");
        *handled = true;
        return ESP_OK;
    }

    if (action_id == CYD_SETTINGS_APP_ACTION_SYNC_NOW) {
        ESP_RETURN_ON_ERROR(cyd_settings_request_sync_now(), TAG, "request sync now failed");
        ESP_RETURN_ON_ERROR(cyd_settings_refresh(), TAG, "refresh settings failed");
        *handled = true;
        return ESP_OK;
    }

    return ESP_OK;
}

/*
 * Confirm screens are dispatched by view, not by page. A confirmation view
 * replaces the page screen and owns its input whether it was opened from NVS
 * or selected directly by another app.
 */
static esp_err_t cyd_settings_handle_clear_touch_calib_confirm_action(uint16_t action_id, bool *handled)
{
    ESP_RETURN_ON_FALSE(handled != NULL, ESP_ERR_INVALID_ARG, TAG, "handled is null");
    *handled = false;

    if (action_id == CYD_SETTINGS_APP_ACTION_CLEAR_TOUCH_CALIB_CANCEL) {
        if (s_settings_direct_view == CYD_SETTINGS_DIRECT_VIEW_CLEAR_TOUCH_CALIB_CONFIRM) {
            *handled = true;
            return app_shell_return_to(s_settings_return_app);
        }
        s_settings_view = CYD_SETTINGS_VIEW_PAGES;
        ESP_RETURN_ON_ERROR(cyd_settings_refresh(), TAG, "refresh settings failed");
        *handled = true;
        return ESP_OK;
    }

    if (action_id == CYD_SETTINGS_APP_ACTION_CLEAR_TOUCH_CALIB_CONFIRM) {
        ESP_RETURN_ON_ERROR(cyd_input_clear_touch_calibration(), TAG, "clear touch calibration failed");
        *handled = true;
        cyd_settings_save_and_restart("タッチ補正を消しました", 2000);
        return ESP_OK;
    }

    return ESP_OK;
}

static esp_err_t cyd_settings_handle_clear_app_data_confirm_action(uint16_t action_id, bool *handled)
{
    ESP_RETURN_ON_FALSE(handled != NULL, ESP_ERR_INVALID_ARG, TAG, "handled is null");
    *handled = false;

    if (action_id == CYD_SETTINGS_APP_ACTION_CLEAR_APP_DATA_CANCEL) {
        s_settings_view = CYD_SETTINGS_VIEW_PAGES;
        ESP_RETURN_ON_ERROR(cyd_settings_refresh(), TAG, "refresh settings failed");
        *handled = true;
        return ESP_OK;
    }

    if (action_id == CYD_SETTINGS_APP_ACTION_CLEAR_APP_DATA_CONFIRM) {
        size_t erased = 0;
        char detail[CYD_DISPLAY_TEXT_MAX_LEN + 1] = { 0 };

        esp_err_t err = nvs_schema_erase_scope(NVS_SCHEMA_SCOPE_APP, &erased);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "clear app data failed: %s", esp_err_to_name(err));
            /* The error name is technical and stays in English. */
            snprintf(detail, sizeof(detail), "%s", esp_err_to_name(err));
            ESP_RETURN_ON_ERROR(cyd_settings_show_restart_message("消去できませんでした", detail),
                                TAG,
                                "show failure message failed");
            vTaskDelay(pdMS_TO_TICKS(2000));
            s_settings_view = CYD_SETTINGS_VIEW_PAGES;
            *handled = true;
            return cyd_settings_refresh();
        }

        /* Restarting is the point, not a formality: apps read their NVS data at
           startup, so anything already running would keep stale state. */
        snprintf(detail, sizeof(detail), "%u件を消去しました", (unsigned)erased);
        *handled = true;
        cyd_settings_save_and_restart(detail, 2000);
        return ESP_OK;
    }

    return ESP_OK;
}

static esp_err_t cyd_settings_handle_clear_nvs_confirm_action(uint16_t action_id, bool *handled)
{
    ESP_RETURN_ON_FALSE(handled != NULL, ESP_ERR_INVALID_ARG, TAG, "handled is null");
    *handled = false;

    if (s_settings_force_initialize && action_id == CYD_SETTINGS_APP_ACTION_CLEAR_NVS_CANCEL) {
        /* Forced initialize has no escape: the stored data is unusable. */
        *handled = true;
        return ESP_OK;
    }

    if (action_id == CYD_SETTINGS_APP_ACTION_CLEAR_NVS_CANCEL) {
        if (s_settings_direct_view == CYD_SETTINGS_DIRECT_VIEW_CLEAR_NVS_CONFIRM) {
            *handled = true;
            return app_shell_return_to(s_settings_return_app);
        }
        s_settings_view = CYD_SETTINGS_VIEW_PAGES;
        ESP_RETURN_ON_ERROR(cyd_settings_refresh(), TAG, "refresh settings failed");
        *handled = true;
        return ESP_OK;
    }

    if (action_id == CYD_SETTINGS_APP_ACTION_CLEAR_NVS_CONFIRM) {
        ESP_RETURN_ON_ERROR(cyd_input_discard_pending_events(), TAG, "discard input events failed");
        ESP_RETURN_ON_ERROR(cyd_settings_show_restart_message("初期化しました", "再起動しています…"),
                            TAG,
                            "show restart message failed");
        vTaskDelay(pdMS_TO_TICKS(500));
        nvs_flash_deinit();
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "erase NVS failed");
        vTaskDelay(pdMS_TO_TICKS(1500));
        *handled = true;
        esp_restart();
        return ESP_OK;
    }

    return ESP_OK;
}

/*
 * True when settings is where the device booted to, rather than somewhere the
 * user navigated into.
 *
 * app_shell_start() enters the first app with from_app == NULL, so a boot entry
 * records no return app. That is the same condition as "there is nowhere to go
 * back to", which is the honest reason to offer something other than leaving:
 * app_shell_return_to() would fall back to the home app, and on a unit that was
 * booted into settings the home app is usually the one that cannot work yet -
 * it is missing the Wi-Fi or backend configuration that lives behind this very
 * screen.
 */
static bool cyd_settings_entered_at_boot(void)
{
    return s_settings_return_app == NULL;
}

/*
 * Root `<<` asks before restarting instead of leaving settings, but only when
 * the device booted straight here. The enclosure has no reachable reset button,
 * so this is the only way to reboot from the UI - and after configuring Wi-Fi
 * or the backend at boot, restarting is what actually puts the settings to use.
 *
 * Entered the ordinary way, `<<` still just goes back: the user came from a
 * working screen and asked to return to it, and rebooting instead would be a
 * surprising answer to that.
 */
static esp_err_t cyd_settings_handle_restart_confirm_action(uint16_t action_id, bool *handled)
{
    ESP_RETURN_ON_FALSE(handled != NULL, ESP_ERR_INVALID_ARG, TAG, "handled is null");
    *handled = false;

    if (action_id == CYD_SETTINGS_APP_ACTION_RESTART_CANCEL) {
        s_settings_view = CYD_SETTINGS_VIEW_PAGES;
        ESP_RETURN_ON_ERROR(cyd_settings_refresh(), TAG, "refresh settings failed");
        *handled = true;
        return ESP_OK;
    }

    if (action_id == CYD_SETTINGS_APP_ACTION_RESTART_CONFIRM) {
        *handled = true;
        cyd_settings_save_and_restart("再起動します", 1000);
        return ESP_OK;
    }

    return ESP_OK;
}

static esp_err_t cyd_settings_handle_nvs_page_action(uint16_t action_id, bool *handled)
{
    ESP_RETURN_ON_FALSE(handled != NULL, ESP_ERR_INVALID_ARG, TAG, "handled is null");
    *handled = false;

    if (action_id == CYD_SETTINGS_APP_ACTION_CLEAR_TOUCH_CALIB) {
        s_settings_view = CYD_SETTINGS_VIEW_CLEAR_TOUCH_CALIB_CONFIRM;
        ESP_RETURN_ON_ERROR(cyd_settings_refresh(), TAG, "refresh settings failed");
        *handled = true;
        return ESP_OK;
    }

    if (action_id == CYD_SETTINGS_APP_ACTION_CLEAR_APP_DATA) {
        s_settings_view = CYD_SETTINGS_VIEW_CLEAR_APP_DATA_CONFIRM;
        ESP_RETURN_ON_ERROR(cyd_settings_refresh(), TAG, "refresh settings failed");
        *handled = true;
        return ESP_OK;
    }

    if (action_id == CYD_SETTINGS_APP_ACTION_CLEAR_NVS) {
        s_settings_view = CYD_SETTINGS_VIEW_CLEAR_NVS_CONFIRM;
        ESP_RETURN_ON_ERROR(cyd_settings_refresh(), TAG, "refresh settings failed");
        *handled = true;
        return ESP_OK;
    }

    return ESP_OK;
}

static const cyd_settings_page_def_t CYD_SETTINGS_PAGES[] = {
    {
        .id = CYD_SETTINGS_PAGE_GENERAL,
        .has_steppers = true,
        .title = "一般",
        .group = CYD_SETTINGS_PAGE_GROUP_GENERAL,
        .uses_live_status = false,
        .is_enabled = NULL,
        .fill = cyd_settings_fill_general_page,
        .screen = SYSTEM_SETTINGS_VIEW_GENERAL,
        .handle_action = cyd_settings_handle_general_page_action,
    },
    {
        .id = CYD_SETTINGS_PAGE_TIME,
        .has_steppers = true,
        .title = "時刻",
        .group = CYD_SETTINGS_PAGE_GROUP_TIME,
        .uses_live_status = true,
        .is_enabled = NULL,
        .fill = cyd_settings_fill_time_page,
        .screen = SYSTEM_SETTINGS_VIEW_TIME,
        .handle_action = cyd_settings_handle_time_page_action,
    },
    {
        .id = CYD_SETTINGS_PAGE_NETWORK1,
        .has_steppers = false,
        .title = "ネットワーク",
        .group = CYD_SETTINGS_PAGE_GROUP_NETWORK,
        .uses_live_status = true,
        .is_enabled = cyd_settings_network_page_enabled,
        .fill = cyd_settings_fill_network1_page,
        .screen = SYSTEM_SETTINGS_VIEW_NETWORK1,
        .handle_action = cyd_settings_handle_network1_page_action,
    },
    {
        .id = CYD_SETTINGS_PAGE_NETWORK2,
        .has_steppers = true,
        .title = "ネットワーク",
        .group = CYD_SETTINGS_PAGE_GROUP_NETWORK,
        .uses_live_status = true,
        .is_enabled = cyd_settings_network_page_enabled,
        .fill = cyd_settings_fill_network2_page,
        .screen = SYSTEM_SETTINGS_VIEW_NETWORK2,
        .handle_action = cyd_settings_handle_network2_page_action,
    },
    {
        .id = CYD_SETTINGS_PAGE_NVS,
        .has_steppers = false,
        .title = "初期化",
        .group = CYD_SETTINGS_PAGE_GROUP_NVS,
        .uses_live_status = false,
        .is_enabled = NULL,
        .fill = cyd_settings_fill_nvs_page,
        .screen = SYSTEM_SETTINGS_VIEW_NVS,
        .handle_action = cyd_settings_handle_nvs_page_action,
    },
    {
        .id = CYD_SETTINGS_PAGE_APPS,
        .has_steppers = false,
        .title = "アプリ",
        .group = CYD_SETTINGS_PAGE_GROUP_APPS,
        .uses_live_status = false,
        .is_enabled = cyd_settings_apps_page_enabled,
        .fill = cyd_settings_fill_apps_page,
        .screen = SYSTEM_SETTINGS_VIEW_APPS,
        .handle_action = cyd_settings_handle_apps_page_action,
    },
};

#define CYD_SETTINGS_PAGE_DEF_COUNT (sizeof(CYD_SETTINGS_PAGES) / sizeof(CYD_SETTINGS_PAGES[0]))

/* Adding an enum value without a table row would silently drop the page from
   the rotation, which is the failure this table exists to prevent. */
ESP_STATIC_ASSERT(CYD_SETTINGS_PAGE_DEF_COUNT == CYD_SETTINGS_PAGE_COUNT,
                  "CYD_SETTINGS_PAGES must contain one entry per settings page");

static const cyd_settings_page_def_t *cyd_settings_page_defs(size_t *count)
{
    if (count != NULL) {
        *count = CYD_SETTINGS_PAGE_DEF_COUNT;
    }
    return CYD_SETTINGS_PAGES;
}

static const cyd_settings_page_def_t *cyd_settings_page_def(cyd_settings_page_t page)
{
    for (size_t i = 0; i < CYD_SETTINGS_PAGE_DEF_COUNT; ++i) {
        if (CYD_SETTINGS_PAGES[i].id == page) {
            return &CYD_SETTINGS_PAGES[i];
        }
    }
    return NULL;
}

/*
 * Routes an action to the screen that owns it.
 *
 * A sub-view owns the whole screen while it is up, so it consumes the action.
 * Otherwise only the active page's handler runs -- a page handler is never
 * reachable from a different page.
 */
static esp_err_t cyd_settings_handle_active_screen_action(uint16_t action_id, bool *handled)
{
    ESP_RETURN_ON_FALSE(handled != NULL, ESP_ERR_INVALID_ARG, TAG, "handled is null");
    *handled = false;

    switch (s_settings_view) {
    case CYD_SETTINGS_VIEW_STORED_SSIDS:
        return cyd_settings_handle_stored_ssids_action(action_id, handled);
    case CYD_SETTINGS_VIEW_STORED_SSIDS_DELETE_CONFIRM:
        return cyd_settings_handle_delete_confirm_action(action_id, handled);
    case CYD_SETTINGS_VIEW_CLEAR_TOUCH_CALIB_CONFIRM:
        return cyd_settings_handle_clear_touch_calib_confirm_action(action_id, handled);
    case CYD_SETTINGS_VIEW_CLEAR_NVS_CONFIRM:
        return cyd_settings_handle_clear_nvs_confirm_action(action_id, handled);
    case CYD_SETTINGS_VIEW_CLEAR_APP_DATA_CONFIRM:
        return cyd_settings_handle_clear_app_data_confirm_action(action_id, handled);
    case CYD_SETTINGS_VIEW_RESTART_CONFIRM:
        return cyd_settings_handle_restart_confirm_action(action_id, handled);
    case CYD_SETTINGS_VIEW_PAGES:
    default:
        break;
    }

    const cyd_settings_page_def_t *def = cyd_settings_page_def(s_settings_page);
    ESP_RETURN_ON_FALSE(def != NULL,
                        ESP_ERR_INVALID_STATE,
                        TAG,
                        "settings page definition missing");
    ESP_RETURN_ON_FALSE(def->handle_action != NULL,
                        ESP_ERR_INVALID_STATE,
                        TAG,
                        "settings page action callback missing");
    return def->handle_action(action_id, handled);
}

static esp_err_t cyd_settings_app_step(void *ctx)
{
    (void)ctx;

    cyd_settings_service_sync_now_pending();
    ESP_RETURN_ON_ERROR(cyd_settings_refresh_if_live_status_changed(),
                        TAG,
                        "refresh settings live status failed");

    cyd_input_event_t event = { 0 };
    if (cyd_input_read_event(&event, pdMS_TO_TICKS(CYD_SYSTEM_APPS_INPUT_POLL_MS)) != ESP_OK) {
        return ESP_OK;
    }

    uint16_t action_id = 0;
    bool handled = false;

    /* Steppers act on PRESS/REPEAT so a held button keeps changing the value;
       normal buttons wait for a confirmed RELEASE. The two are not
       interchangeable, which is why the paths stay separate. */
    if (cyd_settings_touch_stepper_action(&event, &action_id)) {
        return cyd_settings_handle_active_screen_action(action_id, &handled);
    }

    if (!cyd_system_apps_touch_confirmed_action(&s_settings_screen, &event, &s_settings_touch_tracker, &action_id)) {
        return ESP_OK;
    }

    /* Page navigation belongs to the shell chrome, not to any one page. */
    ESP_RETURN_ON_ERROR(cyd_settings_handle_page_nav_action(action_id, &handled), TAG, "handle page nav failed");
    if (handled) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(cyd_settings_handle_active_screen_action(action_id, &handled),
                        TAG,
                        "handle settings action failed");
    if (handled) {
        return ESP_OK;
    }

    if (action_id == CYD_SETTINGS_APP_ACTION_BACK) {
        if (cyd_settings_entered_at_boot()) {
            s_settings_view = CYD_SETTINGS_VIEW_RESTART_CONFIRM;
            ESP_RETURN_ON_ERROR(cyd_settings_refresh(), TAG, "refresh settings failed");
            return ESP_OK;
        }
        ESP_RETURN_ON_ERROR(app_shell_return_to(s_settings_return_app), TAG, "switch back from settings failed");
    }

    return ESP_OK;
}

static esp_err_t cyd_settings_app_leave(void *ctx)
{
    (void)ctx;
    return cyd_settings_save_pending_values();
}


static const app_shell_app_t s_cyd_settings_shell_app = {
    .id = "settings",
    .ctx = NULL,
    .enter = cyd_settings_app_enter,
    .step = cyd_settings_app_step,
    .leave = cyd_settings_app_leave,
};

const app_shell_app_t *system_settings_app_get_app(void)
{
    return &s_cyd_settings_shell_app;
}

static void system_settings_set_direct_view(cyd_settings_direct_view_t direct_view)
{
    portENTER_CRITICAL(&s_settings_direct_view_lock);
    s_settings_pending_direct_view = direct_view;
    portEXIT_CRITICAL(&s_settings_direct_view_lock);
}

void system_settings_open_stored_ssids(void)
{
    system_settings_set_direct_view(CYD_SETTINGS_DIRECT_VIEW_STORED_SSIDS);
}

void system_settings_open_clear_touch_calib_confirm(void)
{
    system_settings_set_direct_view(CYD_SETTINGS_DIRECT_VIEW_CLEAR_TOUCH_CALIB_CONFIRM);
}

void system_settings_open_clear_nvs_confirm(void)
{
    system_settings_set_direct_view(CYD_SETTINGS_DIRECT_VIEW_CLEAR_NVS_CONFIRM);
}
