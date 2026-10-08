/*
 * System information screens: every page with the longest values a device
 * reports, measured with the real fonts.
 */

#include <stdio.h>
#include <string.h>
#include "cyd_display.h"
#include "system_info_view.h"
#include "ui_test_support.h"

static cyd_display_screen_t s_screen;

static void build_and_check(const char *name, const system_info_view_model_t *m)
{
    system_info_view_build(&s_screen, m);
    ui_test_check_screen(name, &s_screen);
}

static const cyd_display_widget_t *find_action(uint16_t action_id)
{
    for (size_t i = 0; i < s_screen.widget_count; ++i) {
        if (s_screen.widgets[i].type == CYD_DISPLAY_WIDGET_BUTTON && s_screen.widgets[i].action_id == action_id) {
            return &s_screen.widgets[i];
        }
    }
    return NULL;
}

int main(void)
{
    static int16_t samples[120];
    system_info_view_model_t m;

    ui_test_set_all_text_immutable(false); /* values are formatted in stack buffers */

    printf("overview\n");
    memset(&m, 0, sizeof(m));
    m.app_name = "esp32_project";
    m.app_version = "b4af5ea-dirty";
    m.idf_version = "v5.4.3-dirty";
    m.chip_revision = 301;
    m.chip_cores = 2;
    m.heap_free = 4294967295U;
    for (int w = SYSTEM_SETTINGS_VIEW_WIFI_UNAVAILABLE; w <= SYSTEM_SETTINGS_VIEW_WIFI_SETUP_RUNNING; ++w) {
        char name[32];
        m.wifi = (system_settings_view_wifi_t)w;
        snprintf(name, sizeof(name), "overview, Wi-Fi %d", w);
        build_and_check(name, &m);
    }
    ui_test_check(!find_action(CYD_INFO_APP_ACTION_PREV_PAGE)->enabled, "the first page cannot go back");

    printf("diagnostics\n");
    memset(&m, 0, sizeof(m));
    m.page = SYSTEM_INFO_VIEW_DIAG;
    /* The ESP32 has 320KB of SRAM, so heap figures never pass six digits. */
    m.heap_free = 327680;
    m.heap_min_free = 327680;
    m.heap_largest_block = 327680;
    m.sync_last = SYSTEM_SETTINGS_VIEW_SYNC_LAST_OK_AT;
    m.last_sync_at = (struct tm){ .tm_mon = 11, .tm_mday = 31, .tm_hour = 23, .tm_min = 59 };
    m.profiles_readable = true;
    m.profile_count = 5;
    for (int f = SYSTEM_INFO_VIEW_FAILURE_NONE; f <= SYSTEM_INFO_VIEW_FAILURE_CONNECT; ++f) {
        char name[32];
        m.wifi_failure = (system_info_view_failure_t)f;
        m.sync = (system_settings_view_sync_t)(f % 5);
        snprintf(name, sizeof(name), "diag, failure %d", f);
        build_and_check(name, &m);
    }

    printf("Wi-Fi\n");
    memset(&m, 0, sizeof(m));
    m.page = SYSTEM_INFO_VIEW_WIFI;
    m.wifi_users = 0xff;
    m.wifi_on_seconds = 4294967295U;
    m.wifi_max_on_seconds = 4294967295U;
    m.wifi_connected_too_long = true;
    m.wifi_failure = SYSTEM_INFO_VIEW_FAILURE_NO_SAVED_PROFILE;
    build_and_check("Wi-Fi, odd user mask", &m);

    printf("RSSI\n");
    memset(&m, 0, sizeof(m));
    m.page = SYSTEM_INFO_VIEW_RSSI;
    m.has_rssi = true;
    m.rssi_now = -100;
    m.rssi_samples = 65535;
    m.rssi_graph = (cyd_display_sparkline_t){ .samples = samples, .count = 120, .min_value = -100, .max_value = -30 };
    build_and_check("RSSI with history", &m);
    m.has_rssi = false;
    m.rssi_graph.samples = NULL;
    build_and_check("RSSI, Wi-Fi off", &m);

    printf("NVS\n");
    memset(&m, 0, sizeof(m));
    m.page = SYSTEM_INFO_VIEW_NVS;
    for (size_t i = 0; i < SYSTEM_INFO_VIEW_NVS_MAX; ++i) {
        m.nvs[i] = (system_info_view_nvs_row_t){ "nvs.net80211abc", "feature", 999 }; /* 15-char name */
    }
    m.nvs_count = SYSTEM_INFO_VIEW_NVS_MAX;
    m.nvs_total = 999;
    build_and_check("NVS, full list", &m);
    ui_test_check(!find_action(CYD_INFO_APP_ACTION_NEXT_PAGE)->enabled, "the last page cannot go further");
    m.nvs_scan_failed = true;
    m.nvs_error = "ESP_ERR_NVS_NOT_INITIALIZED";
    m.nvs_count = 0;
    build_and_check("NVS, scan failed", &m);

    return ui_test_finish();
}
