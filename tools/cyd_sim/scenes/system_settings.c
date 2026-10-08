/*
 * The system settings app (system_settings_view.c): every page, the states
 * that change its wording or colour, and every dialog.
 */
#include <string.h>
#include "sim_catalog.h"
#include "system_settings_view.h"

static system_settings_view_model_t page(system_settings_view_screen_t screen, const char *title, size_t index)
{
    system_settings_view_model_t m;

    memset(&m, 0, sizeof(m));
    m.screen = screen;
    m.page_title = title;
    m.page_index = index;
    m.page_count = 6;
    return m;
}

static void build_general(cyd_display_screen_t *screen, unsigned frame)
{
    (void)frame;
    system_settings_view_model_t m = page(SYSTEM_SETTINGS_VIEW_GENERAL, "一般", 0);
    m.brightness_percent = 75;
    m.can_dim = true;
    m.can_brighten = true;
    m.idle_return_seconds = 90;
    m.can_idle_return_down = true;
    m.can_idle_return_up = true;
    system_settings_view_build(screen, &m);
}

static void build_general_limits(cyd_display_screen_t *screen, unsigned frame)
{
    (void)frame;
    system_settings_view_model_t m = page(SYSTEM_SETTINGS_VIEW_GENERAL, "一般", 0);
    m.brightness_percent = 100;
    m.can_dim = true;
    m.idle_return_seconds = 0;
    m.can_idle_return_up = true;
    system_settings_view_build(screen, &m);
}

static void build_time(cyd_display_screen_t *screen, unsigned frame)
{
    system_settings_view_model_t m = page(SYSTEM_SETTINGS_VIEW_TIME, "時刻", 1);
    /* 2026-10-08 (Thu) 09:41:xx, ticking like the device. */
    m.local_time = (struct tm){ .tm_year = 126, .tm_mon = 9, .tm_mday = 8, .tm_wday = 4,
                                .tm_hour = 9, .tm_min = 41, .tm_sec = (int)((frame / 60U) % 60U) };
    m.clock_set = true;
    m.timezone_label = system_settings_view_timezone_label(1);
    m.can_timezone_down = true;
    m.can_timezone_up = true;
    system_settings_view_build(screen, &m);
}

static void build_time_unset(cyd_display_screen_t *screen, unsigned frame)
{
    (void)frame;
    system_settings_view_model_t m = page(SYSTEM_SETTINGS_VIEW_TIME, "時刻", 1);
    m.local_time = (struct tm){ .tm_year = 70, .tm_mon = 0, .tm_mday = 1, .tm_wday = 4 };
    m.timezone_label = system_settings_view_timezone_label(system_settings_view_timezone_count() - 1U);
    m.can_timezone_down = true;
    system_settings_view_build(screen, &m);
}

static void build_network1(cyd_display_screen_t *screen, system_settings_view_wifi_t wifi)
{
    system_settings_view_model_t m = page(SYSTEM_SETTINGS_VIEW_NETWORK1, "ネットワーク1", 2);
    m.wifi = wifi;
    system_settings_view_build(screen, &m);
}

static void build_network1_connected(cyd_display_screen_t *screen, unsigned frame)
{
    (void)frame;
    build_network1(screen, SYSTEM_SETTINGS_VIEW_WIFI_CONNECTED);
}

static void build_network1_failed(cyd_display_screen_t *screen, unsigned frame)
{
    (void)frame;
    build_network1(screen, SYSTEM_SETTINGS_VIEW_WIFI_FAILED);
}

static void build_network1_setup(cyd_display_screen_t *screen, unsigned frame)
{
    (void)frame;
    build_network1(screen, SYSTEM_SETTINGS_VIEW_WIFI_SETUP_REQUIRED);
}

static void build_network2(cyd_display_screen_t *screen, unsigned frame)
{
    (void)frame;
    system_settings_view_model_t m = page(SYSTEM_SETTINGS_VIEW_NETWORK2, "ネットワーク2", 3);
    m.sync_interval_minutes = 90;
    m.can_sync_interval_down = true;
    m.can_sync_interval_up = true;
    m.wifi_idle_seconds = 300;
    m.can_wifi_idle_down = true;
    m.can_wifi_idle_up = true;
    m.sync_now_enabled = true;
    m.sync = SYSTEM_SETTINGS_VIEW_SYNC_IDLE;
    m.sync_last = SYSTEM_SETTINGS_VIEW_SYNC_LAST_OK_AT;
    m.last_sync_at = (struct tm){ .tm_mon = 9, .tm_mday = 8, .tm_hour = 9, .tm_min = 41 };
    system_settings_view_build(screen, &m);
}

static void build_network2_failed(cyd_display_screen_t *screen, unsigned frame)
{
    (void)frame;
    system_settings_view_model_t m = page(SYSTEM_SETTINGS_VIEW_NETWORK2, "ネットワーク2", 3);
    m.sync_interval_minutes = 1440;
    m.can_sync_interval_down = true;
    m.wifi_idle_seconds = 0;
    m.can_wifi_idle_up = true;
    m.sync = SYSTEM_SETTINGS_VIEW_SYNC_SYNCING;
    m.sync_last = SYSTEM_SETTINGS_VIEW_SYNC_LAST_FAILED;
    system_settings_view_build(screen, &m);
}

static void build_nvs(cyd_display_screen_t *screen, unsigned frame)
{
    (void)frame;
    system_settings_view_model_t m = page(SYSTEM_SETTINGS_VIEW_NVS, "初期化", 4);
    system_settings_view_build(screen, &m);
}

static void build_apps(cyd_display_screen_t *screen, unsigned frame)
{
    (void)frame;
    system_settings_view_model_t m = page(SYSTEM_SETTINGS_VIEW_APPS, "アプリ", 5);
    m.apps[0] = "Clock";
    m.app_count = 1;
    system_settings_view_build(screen, &m);
}

static system_settings_view_model_t ssids(system_settings_view_screen_t screen)
{
    system_settings_view_model_t m = page(screen, NULL, 0);
    m.page_count = 0;
    m.ssids[0] = "MyHome-5G";
    m.ssids[1] = "MyHome-2.4G";
    m.ssids[2] = "Office_Guest_Network_Long_Name";
    m.ssid_count = 3;
    m.selected_ssid = 1;
    return m;
}

static void build_stored(cyd_display_screen_t *screen, unsigned frame)
{
    (void)frame;
    system_settings_view_model_t m = ssids(SYSTEM_SETTINGS_VIEW_STORED_SSIDS);
    system_settings_view_build(screen, &m);
}

static void build_stored_empty(cyd_display_screen_t *screen, unsigned frame)
{
    (void)frame;
    system_settings_view_model_t m = ssids(SYSTEM_SETTINGS_VIEW_STORED_SSIDS);
    m.ssid_count = 0;
    system_settings_view_build(screen, &m);
}

static void build_delete_ssid(cyd_display_screen_t *screen, unsigned frame)
{
    (void)frame;
    system_settings_view_model_t m = ssids(SYSTEM_SETTINGS_VIEW_DELETE_SSID_CONFIRM);
    system_settings_view_build(screen, &m);
}

static void build_simple(cyd_display_screen_t *screen, system_settings_view_screen_t which)
{
    system_settings_view_model_t m = page(which, NULL, 0);
    m.page_count = 0;
    system_settings_view_build(screen, &m);
}

static void build_clear_touch(cyd_display_screen_t *screen, unsigned frame)
{
    (void)frame;
    build_simple(screen, SYSTEM_SETTINGS_VIEW_CLEAR_TOUCH_CALIB_CONFIRM);
}

static void build_clear_app_data(cyd_display_screen_t *screen, unsigned frame)
{
    (void)frame;
    build_simple(screen, SYSTEM_SETTINGS_VIEW_CLEAR_APP_DATA_CONFIRM);
}

static void build_clear_nvs(cyd_display_screen_t *screen, unsigned frame)
{
    (void)frame;
    build_simple(screen, SYSTEM_SETTINGS_VIEW_CLEAR_NVS_CONFIRM);
}

static void build_clear_nvs_forced(cyd_display_screen_t *screen, unsigned frame)
{
    (void)frame;
    system_settings_view_model_t m = page(SYSTEM_SETTINGS_VIEW_CLEAR_NVS_CONFIRM, NULL, 0);
    m.page_count = 0;
    m.nvs_force_initialize = true;
    m.nvs_problem = "sys/disp_bright: bad size";
    system_settings_view_build(screen, &m);
}

static void build_restart(cyd_display_screen_t *screen, unsigned frame)
{
    (void)frame;
    build_simple(screen, SYSTEM_SETTINGS_VIEW_RESTART_CONFIRM);
}

static void build_message(cyd_display_screen_t *screen, unsigned frame)
{
    (void)frame;
    system_settings_view_model_t m = page(SYSTEM_SETTINGS_VIEW_MESSAGE, NULL, 0);
    m.page_count = 0;
    m.message_title = "タッチ補正を消しました";
    m.message_detail = "再起動しています…";
    system_settings_view_build(screen, &m);
}

static const cyd_sim_scene_t k_scenes[] = {
    { "sys_general", "設定: 一般", NULL, build_general },
    { "sys_general_limits", "設定: 一般 (上限・しない)", "明るさ 100%、無操作で戻る しない", build_general_limits },
    { "sys_time", "設定: 時刻", "秒が進む", build_time },
    { "sys_time_unset", "設定: 時刻 (未設定)", "時計が合っていない、最後のタイムゾーン", build_time_unset },
    { "sys_network1", "設定: ネットワーク1 (接続済み)", NULL, build_network1_connected },
    { "sys_network1_failed", "設定: ネットワーク1 (接続できない)", NULL, build_network1_failed },
    { "sys_network1_setup", "設定: ネットワーク1 (未設定)", NULL, build_network1_setup },
    { "sys_network2", "設定: ネットワーク2", "前回成功", build_network2 },
    { "sys_network2_failed", "設定: ネットワーク2 (同期中・前回失敗)", "今すぐ合わせるは無効", build_network2_failed },
    { "sys_nvs", "設定: 初期化", NULL, build_nvs },
    { "sys_apps", "設定: アプリ", NULL, build_apps },
    { "sys_stored", "保存済みのネットワーク", "2 番目を選択中、長い SSID", build_stored },
    { "sys_stored_empty", "保存済みのネットワーク (なし)", NULL, build_stored_empty },
    { "sys_delete_ssid", "確認: ネットワークの削除", NULL, build_delete_ssid },
    { "sys_clear_touch", "確認: タッチ補正の消去", NULL, build_clear_touch },
    { "sys_clear_app_data", "確認: アプリデータの消去", NULL, build_clear_app_data },
    { "sys_clear_nvs", "確認: すべて初期化", NULL, build_clear_nvs },
    { "sys_clear_nvs_forced", "確認: 保存データが読めない", "やめるが無い", build_clear_nvs_forced },
    { "sys_restart", "確認: 再起動", NULL, build_restart },
    { "sys_message", "お知らせ: 再起動中", NULL, build_message },
};

CYD_SIM_REGISTER_SCENES(system_settings, CYD_SIM_ORDER_SETTINGS, k_scenes);
