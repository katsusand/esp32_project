/*
 * The launcher, the clock (a provisional main app) and its settings.
 */
#include <string.h>
#include "app_launcher_view.h"
#include "cyd_clock_settings_view.h"
#include "cyd_clock_view.h"
#include "sim_catalog.h"

static void build_launcher(cyd_display_screen_t *screen, unsigned frame)
{
    (void)frame;
    const app_launcher_view_model_t m = {
        .titles = { "時計", "設定", "システム情報" }, .count = 3, .total = 3, .page_count = 1,
    };
    app_launcher_view_build(screen, &m);
}

static void build_launcher_pages(cyd_display_screen_t *screen, unsigned frame)
{
    (void)frame;
    const app_launcher_view_model_t m = {
        .titles = { "時計", "設定", "システム情報", "Wi-Fi の設定" }, .count = 4, .total = 6, .page_count = 2,
        .show_back = true,
    };
    app_launcher_view_build(screen, &m);
}

static void build_launcher_empty(cyd_display_screen_t *screen, unsigned frame)
{
    (void)frame;
    const app_launcher_view_model_t m = { .page_count = 1 };
    app_launcher_view_build(screen, &m);
}

static cyd_clock_view_model_t face(unsigned frame)
{
    cyd_clock_view_model_t m;

    memset(&m, 0, sizeof(m));
    m.time_known = true;
    m.local_time = (struct tm){ .tm_year = 126, .tm_mon = 9, .tm_mday = 8, .tm_wday = 4, .tm_hour = 21,
                                .tm_min = 41, .tm_sec = (int)((frame / 60U) % 60U) };
    m.use_24_hour = true;
    m.sync = CYD_CLOCK_VIEW_SYNC_OK_AT;
    m.last_sync_at = (struct tm){ .tm_mon = 9, .tm_mday = 8, .tm_hour = 9, .tm_min = 41 };
    m.wifi = CYD_CLOCK_VIEW_WIFI_OFF;
    return m;
}

static void build_clock(cyd_display_screen_t *screen, unsigned frame)
{
    cyd_clock_view_model_t m = face(frame);
    cyd_clock_view_build(screen, &m);
}

static void build_clock_12h(cyd_display_screen_t *screen, unsigned frame)
{
    cyd_clock_view_model_t m = face(frame);
    m.use_24_hour = false;
    m.alarm = CYD_CLOCK_VIEW_ALARM_1_2;
    m.wifi = CYD_CLOCK_VIEW_WIFI_CONNECTED;
    cyd_clock_view_build(screen, &m);
}

static void build_clock_unsynced(cyd_display_screen_t *screen, unsigned frame)
{
    cyd_clock_view_model_t m = face(frame);
    m.time_known = false;
    m.sync = CYD_CLOCK_VIEW_SYNC_FAILED;
    m.wifi = CYD_CLOCK_VIEW_WIFI_CONNECTING;
    m.alarm = CYD_CLOCK_VIEW_ALARM_1;
    cyd_clock_view_build(screen, &m);
}

static void build_wifi_failed(cyd_display_screen_t *screen, unsigned frame)
{
    (void)frame;
    const cyd_clock_view_model_t m = { .screen = CYD_CLOCK_VIEW_SCREEN_WIFI_FAILED,
                                       .failure = CYD_CLOCK_VIEW_FAILURE_NO_AP_IN_RANGE };
    cyd_clock_view_build(screen, &m);
}

static void build_wifi_retrying(cyd_display_screen_t *screen, unsigned frame)
{
    (void)frame;
    const cyd_clock_view_model_t m = { .screen = CYD_CLOCK_VIEW_SCREEN_WIFI_RETRYING,
                                       .retry = CYD_CLOCK_VIEW_RETRY_TRYING, .retry_ssid = "MyHome-5G" };
    cyd_clock_view_build(screen, &m);
}

static void build_alarm1(cyd_display_screen_t *screen, unsigned frame)
{
    (void)frame;
    const cyd_clock_settings_view_model_t m = { .page = CYD_CLOCK_SETTINGS_VIEW_ALARM1, .hour = 7, .minute = 30,
                                                .weekday_mask = 0x3e };
    cyd_clock_settings_view_build(screen, &m);
}

static void build_alarm2(cyd_display_screen_t *screen, unsigned frame)
{
    (void)frame;
    const cyd_clock_settings_view_model_t m = { .page = CYD_CLOCK_SETTINGS_VIEW_ALARM2, .hour = 22, .minute = 0 };
    cyd_clock_settings_view_build(screen, &m);
}

static void build_scheduler(cyd_display_screen_t *screen, unsigned frame)
{
    (void)frame;
    cyd_clock_settings_view_model_t m = { .page = CYD_CLOCK_SETTINGS_VIEW_SCHEDULER, .schedule_count = 3,
                                          .schedule_capacity = 5 };
    m.schedules[0] = (cyd_clock_settings_view_schedule_t){ 1, "clock", "alarm1", false, false, true,
                                                           CYD_CLOCK_SETTINGS_VIEW_SCHED_WAITING, 7, 30, 0, 0 };
    m.schedules[1] = (cyd_clock_settings_view_schedule_t){ 2, "clock", "alarm2", false, false, true,
                                                           CYD_CLOCK_SETTINGS_VIEW_SCHED_DISABLED, 22, 0, 0, 0 };
    m.schedules[2] = (cyd_clock_settings_view_schedule_t){ 3, "sampler", "rssi_win", true, true, false,
                                                           CYD_CLOCK_SETTINGS_VIEW_SCHED_ACTIVE, 9, 0, 17, 30 };
    cyd_clock_settings_view_build(screen, &m);
}

static const cyd_sim_scene_t k_scenes[] = {
    { "launcher", "ランチャー (home)", "戻るが無い", build_launcher },
    { "launcher_pages", "ランチャー (2 ページ)", "戻るあり", build_launcher_pages },
    { "launcher_empty", "ランチャー (アプリなし)", NULL, build_launcher_empty },
    { "clock", "時計", "秒が進む、アラームはオフ", build_clock },
    { "clock_12h", "時計 (12 時間表示)", "午後、アラーム 1と2", build_clock_12h },
    { "clock_unsynced", "時計 (未同期)", NULL, build_clock_unsynced },
    { "clock_wifi_failed", "時計: Wi-Fi に接続できない", NULL, build_wifi_failed },
    { "clock_wifi_retrying", "時計: Wi-Fi 再接続中", NULL, build_wifi_retrying },
    { "clock_settings_alarm1", "時計の設定: アラーム1", "平日だけ", build_alarm1 },
    { "clock_settings_alarm2", "時計の設定: アラーム2", NULL, build_alarm2 },
    { "clock_settings_scheduler", "時計の設定: スケジュール", NULL, build_scheduler },
};

CYD_SIM_REGISTER_SCENES(launcher_clock, CYD_SIM_ORDER_MAIN_APP, k_scenes);
