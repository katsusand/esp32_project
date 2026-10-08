/*
 * The system information app (system_info_view.c): every page, with the values
 * a running device reports.
 */
#include <stdint.h>
#include <string.h>
#include "sim_catalog.h"
#include "system_info_view.h"

static system_info_view_model_t base(system_info_view_page_t page)
{
    system_info_view_model_t m;

    memset(&m, 0, sizeof(m));
    m.page = page;
    return m;
}

static void build_overview(cyd_display_screen_t *screen, unsigned frame)
{
    (void)frame;
    system_info_view_model_t m = base(SYSTEM_INFO_VIEW_OVERVIEW);
    m.app_name = "esp32_project";
    m.app_version = "b4af5ea-dirty";
    m.idf_version = "v5.4.3";
    m.chip_revision = 3;
    m.chip_cores = 2;
    m.heap_free = 61344;
    m.wifi = SYSTEM_SETTINGS_VIEW_WIFI_CONNECTED;
    system_info_view_build(screen, &m);
}

static void build_diag(cyd_display_screen_t *screen, unsigned frame)
{
    (void)frame;
    system_info_view_model_t m = base(SYSTEM_INFO_VIEW_DIAG);
    m.heap_free = 61344;
    m.heap_min_free = 49216;
    m.heap_largest_block = 51200;
    m.wifi_failure = SYSTEM_INFO_VIEW_FAILURE_AUTH;
    m.sync = SYSTEM_SETTINGS_VIEW_SYNC_IDLE;
    m.sync_last = SYSTEM_SETTINGS_VIEW_SYNC_LAST_OK_AT;
    m.last_sync_at = (struct tm){ .tm_mon = 9, .tm_mday = 8, .tm_hour = 9, .tm_min = 41 };
    m.profiles_readable = true;
    m.profile_count = 3;
    m.touch_calib = SYSTEM_INFO_VIEW_TOUCH_CALIB_SAVED;
    system_info_view_build(screen, &m);
}

static void build_wifi(cyd_display_screen_t *screen, unsigned frame)
{
    (void)frame;
    system_info_view_model_t m = base(SYSTEM_INFO_VIEW_WIFI);
    m.wifi = SYSTEM_SETTINGS_VIEW_WIFI_CONNECTED;
    m.wifi_users = 1;
    m.wifi_users_is_radio_manager = true;
    m.wifi_last_user_is_radio_manager = true;
    m.wifi_on_seconds = 42;
    m.wifi_max_on_seconds = 1803;
    m.wifi_connected_too_long = true;
    system_info_view_build(screen, &m);
}

static void build_rssi(cyd_display_screen_t *screen, unsigned frame)
{
    static int16_t samples[120];
    system_info_view_model_t m = base(SYSTEM_INFO_VIEW_RSSI);

    for (size_t i = 0; i < 120; ++i) {
        samples[i] = (int16_t)(-58 - (int)((i * 7 + frame / 30U) % 23U));
    }
    samples[40] = samples[41] = INT16_MIN; /* a gap: Wi-Fi was off (WIFI_RSSI_HISTORY_GAP_DBM) */
    m.has_rssi = true;
    m.rssi_now = samples[119];
    m.rssi_samples = 120;
    m.rssi_graph = (cyd_display_sparkline_t){
        .samples = samples, .count = 120, .revision = (uint16_t)(frame / 30U), .min_value = -100,
        .max_value = -30, .fill = true, .has_baseline = true, .baseline_value = -75,
        .baseline_color = 0xea28, .has_gap_value = true, .gap_value = INT16_MIN,
    };
    system_info_view_build(screen, &m);
}

static void build_rssi_off(cyd_display_screen_t *screen, unsigned frame)
{
    (void)frame;
    system_info_view_model_t m = base(SYSTEM_INFO_VIEW_RSSI);
    system_info_view_build(screen, &m);
}

static void build_nvs(cyd_display_screen_t *screen, unsigned frame)
{
    (void)frame;
    static const system_info_view_nvs_row_t rows[] = {
        { "nvs.net80211", "esp-idf", 14 }, { "phy", "esp-idf", 3 }, { "sys_display", "system", 1 },
        { "sys_input", "system", 1 },      { "sys_wifi", "system", 6 }, { "ftr_timesync", "feature", 2 },
        { "ftr_radio", "feature", 1 },    { "app_sched", "app", 2 },   { "app_clock", "app", 4 },
        { "misc", "unknown", 1 },
    };
    system_info_view_model_t m = base(SYSTEM_INFO_VIEW_NVS);
    memcpy(m.nvs, rows, sizeof(rows));
    m.nvs_count = 10;
    m.nvs_total = 12;
    system_info_view_build(screen, &m);
}

static const cyd_sim_scene_t k_scenes[] = {
    { "info_overview", "システム情報: 概要", NULL, build_overview },
    { "info_diag", "システム情報: 診断", "前回の Wi-Fi は認証失敗", build_diag },
    { "info_wifi", "システム情報: Wi-Fi 診断", "接続が長すぎる警告", build_wifi },
    { "info_rssi", "システム情報: 電波", "グラフが動く、途中に途切れ", build_rssi },
    { "info_rssi_off", "システム情報: 電波 (Wi-Fi オフ)", NULL, build_rssi_off },
    { "info_nvs", "システム情報: NVS", "12 個中 10 個を表示", build_nvs },
};

CYD_SIM_REGISTER_SCENES(system_info, CYD_SIM_ORDER_SETTINGS + 2, k_scenes);
