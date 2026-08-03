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
#include "time_sync.h"
#include "wifi_connection.h"
#include "wifi_profile_store.h"
#include "wifi_rssi_history.h"

#define TAG "cyd_system_apps"
#define CYD_INFO_APP_ACTION_BACK 0x2101
#define CYD_INFO_APP_ACTION_TOGGLE_PAGE 0x2102
/* Fixed RSSI scale. -30 dBm is effectively "next to the AP", -100 dBm is the
   practical noise floor, so a fixed scale keeps the graph comparable over time
   instead of auto-ranging on every sample. */
#define CYD_INFO_RSSI_MAX_DBM (-30)
#define CYD_INFO_RSSI_MIN_DBM (-100)
#define CYD_INFO_RSSI_WEAK_DBM (-75)

typedef enum {
    CYD_INFO_PAGE_INFO = 0,
    CYD_INFO_PAGE_DIAG,
    CYD_INFO_PAGE_DIAG2,
    CYD_INFO_PAGE_RSSI,
    CYD_INFO_PAGE_COUNT,
} cyd_info_page_t;

static cyd_display_screen_t s_info_screen;
static const app_shell_app_t *s_info_return_app;
static cyd_system_apps_touch_tracker_t s_info_touch_tracker;
static cyd_info_page_t s_info_page = CYD_INFO_PAGE_INFO;
static uint16_t s_info_rssi_shown_revision;
static bool s_info_rssi_has_history;

static const char *cyd_system_apps_wifi_warning_text(wifi_connection_warning_t warning)
{
    switch (warning) {
    case WIFI_CONNECTION_WARNING_CONNECTED_TOO_LONG:
        return "connected too long";
    case WIFI_CONNECTION_WARNING_NONE:
    default:
        return "none";
    }
}

static void cyd_system_apps_format_wifi_users(char *status_text, size_t status_size)
{
    uint32_t active_users = wifi_connection_get_active_users();

    if (status_text == NULL || status_size == 0) {
        return;
    }

    if (active_users == 0) {
        snprintf(status_text, status_size, "wifi users: none");
        return;
    }

    if (active_users == WIFI_CONNECTION_USER_RADIO_MANAGER) {
        snprintf(status_text, status_size, "wifi users: radio mgr");
        return;
    }

    snprintf(status_text, status_size, "wifi users: 0x%02lx", (unsigned long)active_users);
}

static const char *cyd_system_apps_wifi_user_text(wifi_connection_user_t user)
{
    switch (user) {
    case WIFI_CONNECTION_USER_RADIO_MANAGER:
        return "radio mgr";
    default:
        return "none";
    }
}

static void cyd_system_apps_format_wifi_last_user(char *status_text, size_t status_size)
{
    if (status_text == NULL || status_size == 0) {
        return;
    }

    snprintf(status_text,
             status_size,
             "wifi last user: %s",
             cyd_system_apps_wifi_user_text(wifi_connection_get_last_user()));
}

static void cyd_system_apps_format_wifi_duration(char *status_text, size_t status_size)
{
    uint32_t connected_seconds = wifi_connection_get_connected_duration_seconds();

    if (status_text == NULL || status_size == 0) {
        return;
    }

    snprintf(status_text,
             status_size,
             "wifi on: %lu sec",
             (unsigned long)connected_seconds);
}

static void cyd_system_apps_format_wifi_duration_high_water(char *status_text, size_t status_size)
{
    uint32_t high_water_seconds = wifi_connection_get_connected_duration_high_water_seconds();

    if (status_text == NULL || status_size == 0) {
        return;
    }

    snprintf(status_text,
             status_size,
             "wifi max on: %lu sec",
             (unsigned long)high_water_seconds);
}

static void cyd_system_apps_format_wifi_warning(char *status_text, size_t status_size)
{
    if (status_text == NULL || status_size == 0) {
        return;
    }

    snprintf(status_text,
             status_size,
             "wifi warn: %s",
             cyd_system_apps_wifi_warning_text(wifi_connection_get_warning()));
}

static const char *cyd_info_app_next_page_label(void)
{
    switch (s_info_page) {
    case CYD_INFO_PAGE_INFO:
        return "DIAG";
    case CYD_INFO_PAGE_DIAG:
        return "DIAG2";
    case CYD_INFO_PAGE_DIAG2:
        return "RSSI";
    case CYD_INFO_PAGE_RSSI:
    default:
        return "INFO";
    }
}

static const char *cyd_system_apps_wifi_failure_text(esp32_wifi_sta_failure_reason_t reason)
{
    switch (reason) {
    case ESP32_WIFI_STA_FAILURE_NO_SAVED_PROFILE:
        return "no saved profile";
    case ESP32_WIFI_STA_FAILURE_NO_AP_IN_RANGE:
        return "saved AP not found";
    case ESP32_WIFI_STA_FAILURE_AUTH:
        return "auth failed";
    case ESP32_WIFI_STA_FAILURE_TIMEOUT:
        return "connect timeout";
    case ESP32_WIFI_STA_FAILURE_CONNECT:
        return "connect failed";
    case ESP32_WIFI_STA_FAILURE_NONE:
    default:
        return "none";
    }
}

/*
 * The RSSI graph needs the radio powered, but radio_manager shuts it down once
 * no client holds a lease. Hold one only while that page is on screen.
 */
static void cyd_info_app_apply_rssi_monitoring(void)
{
    (void)wifi_rssi_history_set_monitoring(s_info_page == CYD_INFO_PAGE_RSSI);
}

static esp_err_t cyd_info_app_show_page_nav(cyd_display_screen_t *screen)
{
    const char *toggle_label = cyd_info_app_next_page_label();

    ESP_RETURN_ON_FALSE(screen != NULL, ESP_ERR_INVALID_ARG, TAG, "screen is null");

    cyd_ui_add_button(screen,
                      toggle_label,
                      26,
                      25,
                      12,
                      3,
                      CYD_UI_COLOR_DIMGREY,
                      CYD_UI_COLOR_LIGHTGREY,
                      CYD_INFO_APP_ACTION_TOGGLE_PAGE);
    return ESP_OK;
}

static esp_err_t cyd_info_app_show(void)
{
    if (s_info_page == CYD_INFO_PAGE_DIAG) {
        char heap_line[CYD_DISPLAY_TEXT_MAX_LEN + 1] = { 0 };
        char min_heap_line[CYD_DISPLAY_TEXT_MAX_LEN + 1] = { 0 };
        char wifi_fail_line[CYD_DISPLAY_TEXT_MAX_LEN + 1] = { 0 };
        char sync_state_line[CYD_DISPLAY_TEXT_MAX_LEN + 1] = { 0 };
        char sync_last_line[CYD_DISPLAY_TEXT_MAX_LEN + 1] = { 0 };
        char profiles_line[CYD_DISPLAY_TEXT_MAX_LEN + 1] = { 0 };
        char touch_line[CYD_DISPLAY_TEXT_MAX_LEN + 1] = { 0 };
        wifi_profile_store_entry_t profiles[WIFI_PROFILE_STORE_MAX_ENTRIES];
        size_t profile_count = 0;
        cyd_display_screen_t *screen = &s_info_screen;

        snprintf(heap_line,
                 sizeof(heap_line),
                 "heap 8bit: %u",
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT));
        snprintf(min_heap_line,
                 sizeof(min_heap_line),
                 "heap min/max: %u/%u",
                 (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
        snprintf(wifi_fail_line,
                 sizeof(wifi_fail_line),
                 "wifi fail: %s",
                 cyd_system_apps_wifi_failure_text(wifi_connection_get_last_failure_reason()));
        snprintf(sync_state_line,
                 sizeof(sync_state_line),
                 "sync state: %s",
                 cyd_system_apps_time_sync_state_text(time_sync_get_state()));
        cyd_system_apps_format_sync_attempt(sync_last_line, sizeof(sync_last_line));

        if (wifi_profile_store_load_entries(profiles,
                                            WIFI_PROFILE_STORE_MAX_ENTRIES,
                                            &profile_count) == ESP_OK) {
            snprintf(profiles_line, sizeof(profiles_line), "saved SSIDs: %u", (unsigned)profile_count);
        } else {
            snprintf(profiles_line, sizeof(profiles_line), "saved SSIDs: unavailable");
        }

        snprintf(touch_line,
                 sizeof(touch_line),
                 "touch calib: %s",
                 cyd_input_has_saved_touch_calibration() ? "saved" :
                 (cyd_input_has_touch_calibration() ? "default" : "not saved"));

        cyd_ui_screen_clear(screen);
        cyd_ui_add_text(screen,
                        "DIAG",
                        CYD_SYSTEM_APPS_TITLE_COL,
                        CYD_SYSTEM_APPS_TITLE_ROW,
                        CYD_SYSTEM_APPS_TITLE_SPAN_COLS,
                        CYD_SYSTEM_APPS_TITLE_SPAN_ROWS,
                        CYD_DISPLAY_ALIGN_RIGHT,
                        2,
                        CYD_UI_COLOR_CYAN);
        cyd_ui_add_text(screen, heap_line, 2, 5, 36, 2, CYD_DISPLAY_ALIGN_LEFT, 1, CYD_UI_COLOR_WHITE);
        cyd_ui_add_text(screen, min_heap_line, 2, 8, 36, 2, CYD_DISPLAY_ALIGN_LEFT, 1, CYD_UI_COLOR_WHITE);
        cyd_ui_add_text(screen, wifi_fail_line, 2, 11, 36, 2, CYD_DISPLAY_ALIGN_LEFT, 1, CYD_UI_COLOR_WHITE);
        cyd_ui_add_text(screen, sync_state_line, 2, 14, 36, 2, CYD_DISPLAY_ALIGN_LEFT, 1, CYD_UI_COLOR_WHITE);
        cyd_ui_add_text(screen, sync_last_line, 2, 17, 36, 2, CYD_DISPLAY_ALIGN_LEFT, 1, CYD_UI_COLOR_WHITE);
        cyd_ui_add_text(screen, profiles_line, 2, 20, 36, 2, CYD_DISPLAY_ALIGN_LEFT, 1, CYD_UI_COLOR_WHITE);
        cyd_ui_add_text(screen, touch_line, 2, 23, 36, 2, CYD_DISPLAY_ALIGN_LEFT, 1, CYD_UI_COLOR_WHITE);
        ESP_RETURN_ON_ERROR(cyd_info_app_show_page_nav(screen), TAG, "add info page nav failed");
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

    if (s_info_page == CYD_INFO_PAGE_DIAG2) {
        char wifi_state_line[CYD_DISPLAY_TEXT_MAX_LEN + 1] = { 0 };
        char wifi_users_line[CYD_DISPLAY_TEXT_MAX_LEN + 1] = { 0 };
        char wifi_last_user_line[CYD_DISPLAY_TEXT_MAX_LEN + 1] = { 0 };
        char wifi_on_line[CYD_DISPLAY_TEXT_MAX_LEN + 1] = { 0 };
        char wifi_max_on_line[CYD_DISPLAY_TEXT_MAX_LEN + 1] = { 0 };
        char wifi_warn_line[CYD_DISPLAY_TEXT_MAX_LEN + 1] = { 0 };
        char wifi_fail_line[CYD_DISPLAY_TEXT_MAX_LEN + 1] = { 0 };
        cyd_display_screen_t *screen = &s_info_screen;

        cyd_system_apps_format_wifi_status(wifi_state_line, sizeof(wifi_state_line));
        cyd_system_apps_format_wifi_users(wifi_users_line, sizeof(wifi_users_line));
        cyd_system_apps_format_wifi_last_user(wifi_last_user_line, sizeof(wifi_last_user_line));
        cyd_system_apps_format_wifi_duration(wifi_on_line, sizeof(wifi_on_line));
        cyd_system_apps_format_wifi_duration_high_water(wifi_max_on_line, sizeof(wifi_max_on_line));
        cyd_system_apps_format_wifi_warning(wifi_warn_line, sizeof(wifi_warn_line));
        snprintf(wifi_fail_line,
                 sizeof(wifi_fail_line),
                 "wifi fail: %s",
                 cyd_system_apps_wifi_failure_text(wifi_connection_get_last_failure_reason()));

        cyd_ui_screen_clear(screen);
        cyd_ui_add_text(screen,
                        "DIAG2",
                        CYD_SYSTEM_APPS_TITLE_COL,
                        CYD_SYSTEM_APPS_TITLE_ROW,
                        CYD_SYSTEM_APPS_TITLE_SPAN_COLS,
                        CYD_SYSTEM_APPS_TITLE_SPAN_ROWS,
                        CYD_DISPLAY_ALIGN_RIGHT,
                        2,
                        CYD_UI_COLOR_CYAN);
        cyd_ui_add_text(screen, wifi_state_line, 2, 5, 36, 2, CYD_DISPLAY_ALIGN_LEFT, 1, CYD_UI_COLOR_WHITE);
        cyd_ui_add_text(screen, wifi_users_line, 2, 8, 36, 2, CYD_DISPLAY_ALIGN_LEFT, 1, CYD_UI_COLOR_WHITE);
        cyd_ui_add_text(screen, wifi_last_user_line, 2, 11, 36, 2, CYD_DISPLAY_ALIGN_LEFT, 1, CYD_UI_COLOR_WHITE);
        cyd_ui_add_text(screen, wifi_on_line, 2, 14, 36, 2, CYD_DISPLAY_ALIGN_LEFT, 1, CYD_UI_COLOR_WHITE);
        cyd_ui_add_text(screen, wifi_max_on_line, 2, 17, 36, 2, CYD_DISPLAY_ALIGN_LEFT, 1, CYD_UI_COLOR_WHITE);
        cyd_ui_add_text(screen, wifi_warn_line, 2, 20, 36, 2, CYD_DISPLAY_ALIGN_LEFT, 1, CYD_UI_COLOR_WHITE);
        cyd_ui_add_text(screen, wifi_fail_line, 2, 23, 36, 2, CYD_DISPLAY_ALIGN_LEFT, 1, CYD_UI_COLOR_WHITE);
        ESP_RETURN_ON_ERROR(cyd_info_app_show_page_nav(screen), TAG, "add info page nav failed");
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

    if (s_info_page == CYD_INFO_PAGE_RSSI) {
        cyd_display_screen_t *screen = &s_info_screen;
        const int16_t *samples = NULL;
        uint16_t sample_count = 0;
        uint16_t revision = 0;
        int16_t latest_rssi = 0;
        char rssi_line[CYD_DISPLAY_TEXT_MAX_LEN + 1] = { 0 };
        bool has_history = wifi_rssi_history_get(&samples, &sample_count, &revision);

        if (wifi_rssi_history_get_latest(&latest_rssi)) {
            snprintf(rssi_line,
                     sizeof(rssi_line),
                     "now %d dBm  (%u samples)",
                     (int)latest_rssi,
                     (unsigned)sample_count);
        } else if (wifi_rssi_history_is_monitoring()) {
            /* Monitoring is on but nothing has associated yet. Say so, rather
               than "no data", so the radio bring-up wait looks intentional. */
            snprintf(rssi_line, sizeof(rssi_line), "waiting for Wi-Fi...");
        } else {
            snprintf(rssi_line, sizeof(rssi_line), "no data: Wi-Fi unavailable");
        }

        cyd_ui_screen_clear(screen);
        cyd_ui_add_text(screen,
                        "RSSI",
                        CYD_SYSTEM_APPS_TITLE_COL,
                        CYD_SYSTEM_APPS_TITLE_ROW,
                        CYD_SYSTEM_APPS_TITLE_SPAN_COLS,
                        CYD_SYSTEM_APPS_TITLE_SPAN_ROWS,
                        CYD_DISPLAY_ALIGN_RIGHT,
                        2,
                        CYD_UI_COLOR_CYAN);
        cyd_ui_add_text(screen, rssi_line, 2, 5, 36, 2, CYD_DISPLAY_ALIGN_LEFT, 1, CYD_UI_COLOR_WHITE);

        if (has_history) {
            cyd_display_sparkline_t graph = {
                .samples = samples,
                .count = sample_count,
                .revision = revision,
                .min_value = CYD_INFO_RSSI_MIN_DBM,
                .max_value = CYD_INFO_RSSI_MAX_DBM,
                .fill = true,
                .has_baseline = true,
                .baseline_value = CYD_INFO_RSSI_WEAK_DBM,
                .baseline_color = CYD_UI_COLOR_RED,
                .has_gap_value = true,
                .gap_value = WIFI_RSSI_HISTORY_GAP_DBM,
            };
            cyd_ui_add_sparkline(screen,
                                 2,
                                 9,
                                 36,
                                 13,
                                 &graph,
                                 CYD_UI_COLOR_GREEN,
                                 CYD_UI_COLOR_BLACK,
                                 CYD_UI_COLOR_DARKGREY);
            cyd_ui_add_text(screen, "-30", 2, 8, 6, 2, CYD_DISPLAY_ALIGN_LEFT, 1, CYD_UI_COLOR_DARKGREY);
            cyd_ui_add_text(screen, "-100", 2, 22, 6, 2, CYD_DISPLAY_ALIGN_LEFT, 1, CYD_UI_COLOR_DARKGREY);
            s_info_rssi_shown_revision = revision;
        }
        s_info_rssi_has_history = has_history;

        ESP_RETURN_ON_ERROR(cyd_info_app_show_page_nav(screen), TAG, "add info page nav failed");
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
    ESP_RETURN_ON_ERROR(cyd_info_app_show_page_nav(screen), TAG, "add info page nav failed");
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

static esp_err_t cyd_info_app_enter(void *ctx, const app_shell_app_t *from_app)
{
    (void)ctx;
    if (from_app != NULL) {
        s_info_return_app = from_app;
    }
    s_info_page = CYD_INFO_PAGE_INFO;
    s_info_touch_tracker = (cyd_system_apps_touch_tracker_t){ 0 };
    cyd_info_app_apply_rssi_monitoring();
    return cyd_info_app_show();
}

static esp_err_t cyd_info_app_step(void *ctx)
{
    (void)ctx;

    cyd_input_event_t event = { 0 };
    if (cyd_input_read_event(&event, pdMS_TO_TICKS(CYD_SYSTEM_APPS_INPUT_POLL_MS)) == ESP_OK) {
        uint16_t action_id = 0;
        if (cyd_system_apps_touch_confirmed_action(&event, &s_info_touch_tracker, &action_id)) {
            if (action_id == CYD_INFO_APP_ACTION_TOGGLE_PAGE) {
                s_info_page = (cyd_info_page_t)(((int)s_info_page + 1) % (int)CYD_INFO_PAGE_COUNT);
                cyd_info_app_apply_rssi_monitoring();
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
    if (s_info_page == CYD_INFO_PAGE_RSSI) {
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
    /* Releases the radio lease. Also covers the idle-return path, so the lease
       cannot outlive the view. */
    (void)wifi_rssi_history_set_monitoring(false);
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
