/*
 * System settings screens: every page, dialog and value the app can produce
 * is built and measured against its box with the real fonts.
 *
 * The value ranges mirror system_settings_app.c: idle return 0-1800s in 10s
 * steps, the Wi-Fi idle table, time sync 1-1440 minutes along the app's step
 * rules, and every timezone.
 */

#include <stdio.h>
#include <string.h>
#include "cyd_display.h"
#include "system_settings_view.h"
#include "ui_test_support.h"

static cyd_display_screen_t s_screen;

static system_settings_view_model_t base(system_settings_view_screen_t screen)
{
    system_settings_view_model_t m;

    memset(&m, 0, sizeof(m));
    m.screen = screen;
    m.page_title = "ネットワーク2";
    m.page_index = 3;
    m.page_count = 6;
    return m;
}

static void build_and_check(const char *name, const system_settings_view_model_t *m)
{
    system_settings_view_build(&s_screen, m);
    ui_test_check_screen(name, &s_screen);
}

/* Builds silently and reports only failures, for long value sweeps. */
static void sweep(const char *what, const system_settings_view_model_t *m, unsigned value, int *bad)
{
    char name[96];

    system_settings_view_build(&s_screen, m);
    if (!ui_test_screen_fits(&s_screen)) {
        snprintf(name, sizeof(name), "%s %u", what, value);
        ui_test_check_screen(name, &s_screen);
        ++*bad;
    }
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

static bool has_text(const char *text)
{
    for (size_t i = 0; i < s_screen.widget_count; ++i) {
        if (strcmp(cyd_display_widget_text(&s_screen.widgets[i]), text) == 0) {
            return true;
        }
    }
    return false;
}

static void test_pages(void)
{
    system_settings_view_model_t m = base(SYSTEM_SETTINGS_VIEW_GENERAL);
    m.brightness_percent = 100;
    m.idle_return_seconds = 1790;
    build_and_check("general", &m);

    m = base(SYSTEM_SETTINGS_VIEW_TIME);
    m.local_time = (struct tm){ .tm_year = 126, .tm_mon = 11, .tm_mday = 31, .tm_wday = 3, .tm_hour = 23,
                                .tm_min = 59, .tm_sec = 59 };
    m.timezone_label = system_settings_view_timezone_label(0);
    build_and_check("time", &m);
    m.clock_set = true;
    build_and_check("time, clock set", &m);

    m = base(SYSTEM_SETTINGS_VIEW_NETWORK1);
    for (int w = SYSTEM_SETTINGS_VIEW_WIFI_UNAVAILABLE; w <= SYSTEM_SETTINGS_VIEW_WIFI_SETUP_RUNNING; ++w) {
        char name[48];
        m.wifi = (system_settings_view_wifi_t)w;
        snprintf(name, sizeof(name), "network1, Wi-Fi state %d", w);
        build_and_check(name, &m);
    }

    m = base(SYSTEM_SETTINGS_VIEW_NETWORK2);
    m.sync_last = SYSTEM_SETTINGS_VIEW_SYNC_LAST_OK_AT;
    m.last_sync_at = (struct tm){ .tm_mon = 11, .tm_mday = 31, .tm_hour = 23, .tm_min = 59 };
    for (int s = SYSTEM_SETTINGS_VIEW_SYNC_STOPPED; s <= SYSTEM_SETTINGS_VIEW_SYNC_RETRY_WAIT; ++s) {
        char name[48];
        m.sync = (system_settings_view_sync_t)s;
        snprintf(name, sizeof(name), "network2, sync state %d", s);
        build_and_check(name, &m);
    }
    m.sync_now_enabled = false;
    system_settings_view_build(&s_screen, &m);
    ui_test_check(find_action(CYD_SETTINGS_APP_ACTION_SYNC_NOW) != NULL &&
                      !find_action(CYD_SETTINGS_APP_ACTION_SYNC_NOW)->enabled,
                  "sync now renders disabled while it cannot run");

    build_and_check("nvs", &(system_settings_view_model_t){ .screen = SYSTEM_SETTINGS_VIEW_NVS,
                                                             .page_title = "初期化", .page_count = 6 });

    m = base(SYSTEM_SETTINGS_VIEW_APPS);
    m.page_title = "アプリ";
    for (size_t i = 0; i < SYSTEM_SETTINGS_VIEW_APPS_MAX; ++i) {
        m.apps[i] = "Clock";
    }
    m.app_count = SYSTEM_SETTINGS_VIEW_APPS_MAX + 2U;
    build_and_check("apps, more than fit", &m);
    ui_test_check(find_action(CYD_SETTINGS_APP_ACTION_APP_BASE + SYSTEM_SETTINGS_VIEW_APPS_MAX - 1U) != NULL &&
                      find_action(CYD_SETTINGS_APP_ACTION_APP_BASE + SYSTEM_SETTINGS_VIEW_APPS_MAX) == NULL,
                  "the apps page stops at SYSTEM_SETTINGS_VIEW_APPS_MAX");
    for (size_t i = 0; i < s_screen.widget_count; ++i) {
        const cyd_display_widget_t *w = &s_screen.widgets[i];
        if (w->type == CYD_DISPLAY_WIDGET_BUTTON && w->action_id >= CYD_SETTINGS_APP_ACTION_APP_BASE &&
            w->row + w->span_rows > 27) {
            ui_test_check(false, "an app button runs into the page navigation");
        }
    }
}

static void test_value_sweeps(void)
{
    static const uint16_t wifi_idle[] = { 0, 30, 60, 180, 300, 600, 900, 1200, 1800, 2700, 3600 };
    int bad = 0;
    system_settings_view_model_t m = base(SYSTEM_SETTINGS_VIEW_GENERAL);

    for (unsigned s = 0; s <= 1800; s += 10) {
        m.idle_return_seconds = (uint16_t)s;
        sweep("idle return", &m, s, &bad);
    }
    for (unsigned p = 5; p <= 100; p += 5) {
        m.brightness_percent = (uint8_t)p;
        sweep("brightness", &m, p, &bad);
    }
    m = base(SYSTEM_SETTINGS_VIEW_NETWORK2);
    for (size_t i = 0; i < sizeof(wifi_idle) / sizeof(wifi_idle[0]); ++i) {
        m.wifi_idle_seconds = wifi_idle[i];
        sweep("wifi idle", &m, wifi_idle[i], &bad);
    }
    for (unsigned minutes = 1; minutes <= 1440; ++minutes) {
        m.sync_interval_minutes = (uint16_t)minutes;
        sweep("sync interval", &m, minutes, &bad);
    }
    m = base(SYSTEM_SETTINGS_VIEW_TIME);
    for (size_t i = 0; i < system_settings_view_timezone_count(); ++i) {
        m.timezone_label = system_settings_view_timezone_label(i);
        sweep("timezone", &m, (unsigned)i, &bad);
    }
    ui_test_check(bad == 0, "every stepper value fits its box (idle return, brightness, Wi-Fi idle, "
                            "sync interval, timezone)");

    m = base(SYSTEM_SETTINGS_VIEW_NETWORK2);
    m.sync_interval_minutes = 90;
    system_settings_view_build(&s_screen, &m);
    ui_test_check(has_text("90分"), "90 minutes reads 90分");
    m.sync_interval_minutes = 540;
    system_settings_view_build(&s_screen, &m);
    ui_test_check(has_text("9時間"), "540 minutes reads 9時間");
    m.wifi_idle_seconds = 0;
    system_settings_view_build(&s_screen, &m);
    ui_test_check(has_text("しない"), "a zero Wi-Fi idle time reads しない");
}

static void test_dialogs(void)
{
    static const char *ssids[] = { "MyHome-5G", "MyHome-2.4G", "Office", "Cafe", "Library" };
    system_settings_view_model_t m = base(SYSTEM_SETTINGS_VIEW_STORED_SSIDS);

    m.page_count = 0;
    memcpy(m.ssids, ssids, sizeof(ssids));
    m.ssid_count = 5;
    m.selected_ssid = 4;
    build_and_check("stored SSIDs, full list", &m);
    ui_test_check(find_action(CYD_SETTINGS_APP_ACTION_STORED_SELECT_BASE + 4) != NULL,
                  "the fifth stored profile is reachable");
    m.ssid_count = 0;
    build_and_check("stored SSIDs, empty", &m);
    ui_test_check(!find_action(CYD_SETTINGS_APP_ACTION_STORED_DELETE)->enabled,
                  "delete is disabled with nothing stored");

    m.ssid_count = 5;
    m.screen = SYSTEM_SETTINGS_VIEW_DELETE_SSID_CONFIRM;
    build_and_check("delete SSID", &m);

    static const struct {
        system_settings_view_screen_t screen;
        const char *name;
        uint16_t cancel;
    } dialogs[] = {
        { SYSTEM_SETTINGS_VIEW_CLEAR_TOUCH_CALIB_CONFIRM, "clear touch calibration",
          CYD_SETTINGS_APP_ACTION_CLEAR_TOUCH_CALIB_CANCEL },
        { SYSTEM_SETTINGS_VIEW_CLEAR_APP_DATA_CONFIRM, "clear app data", CYD_SETTINGS_APP_ACTION_CLEAR_APP_DATA_CANCEL },
        { SYSTEM_SETTINGS_VIEW_CLEAR_NVS_CONFIRM, "initialize NVS", CYD_SETTINGS_APP_ACTION_CLEAR_NVS_CANCEL },
        { SYSTEM_SETTINGS_VIEW_RESTART_CONFIRM, "restart", CYD_SETTINGS_APP_ACTION_RESTART_CANCEL },
    };
    for (size_t i = 0; i < sizeof(dialogs) / sizeof(dialogs[0]); ++i) {
        m = base(dialogs[i].screen);
        m.page_count = 0;
        build_and_check(dialogs[i].name, &m);
        ui_test_check(find_action(dialogs[i].cancel) != NULL, "and it can be cancelled");
    }

    m = base(SYSTEM_SETTINGS_VIEW_CLEAR_NVS_CONFIRM);
    m.page_count = 0;
    m.nvs_force_initialize = true;
    m.nvs_problem = "sys/disp_bright: bad size";
    build_and_check("forced initialize", &m);
    ui_test_check(find_action(CYD_SETTINGS_APP_ACTION_CLEAR_NVS_CANCEL) == NULL,
                  "a forced initialize offers no cancel");

    static const char *const titles[] = { "タッチ補正を消しました", "18件を消去しました",
                                          "消去できませんでした", "初期化しました", "再起動します" };
    for (size_t i = 0; i < sizeof(titles) / sizeof(titles[0]); ++i) {
        m = base(SYSTEM_SETTINGS_VIEW_MESSAGE);
        m.message_title = titles[i];
        m.message_detail = "設定を保存できませんでした。再起動します…";
        build_and_check(titles[i], &m);
    }
}

int main(void)
{
    ui_test_set_all_text_immutable(false); /* the view formats values in stack buffers */
    printf("pages\n");
    test_pages();
    printf("stepper values\n");
    test_value_sweeps();
    printf("dialogs and messages\n");
    test_dialogs();
    return ui_test_finish();
}
