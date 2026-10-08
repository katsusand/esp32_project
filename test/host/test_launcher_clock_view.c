/*
 * The launcher, the clock and the clock settings, measured with the real
 * fonts. The clock face is built for every minute of the day in both 12- and
 * 24-hour form, because a time that does not fit would make the digits shrink
 * from one second to the next.
 */

#include <stdio.h>
#include <string.h>
#include "app_launcher_view.h"
#include "cyd_clock_settings_view.h"
#include "cyd_clock_view.h"
#include "cyd_display.h"
#include "ui_test_support.h"

static cyd_display_screen_t s_screen;

static void check_current(const char *name)
{
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

static void test_launcher(void)
{
    app_launcher_view_model_t m = {
        .titles = { "時計", "システム情報", "Wi-Fi の設定", "設定" }, .count = 4, .total = 9, .page_index = 1,
        .page_count = 3, .show_back = true,
    };

    app_launcher_view_build(&s_screen, &m);
    check_current("launcher, middle page");
    ui_test_check(find_action(APP_LAUNCHER_ACTION_APP_BASE + 3) != NULL, "four apps per page");
    ui_test_check(find_action(APP_LAUNCHER_ACTION_BACK) != NULL, "back shown when not home");

    m = (app_launcher_view_model_t){ .titles = { "時計" }, .count = 1, .total = 1, .page_count = 1 };
    app_launcher_view_build(&s_screen, &m);
    check_current("launcher as home");
    ui_test_check(find_action(APP_LAUNCHER_ACTION_BACK) == NULL, "no back button as home");
    ui_test_check(find_action(APP_LAUNCHER_ACTION_NEXT_PAGE) == NULL, "no page navigation for one page");

    m = (app_launcher_view_model_t){ .page_count = 1 };
    app_launcher_view_build(&s_screen, &m);
    check_current("launcher, no apps");
}

static void test_clock_face(void)
{
    cyd_clock_view_model_t m;
    int bad = 0;

    memset(&m, 0, sizeof(m));
    m.time_known = true;
    m.sync = CYD_CLOCK_VIEW_SYNC_OK_AT;
    m.last_sync_at = (struct tm){ .tm_mon = 11, .tm_mday = 31, .tm_hour = 23, .tm_min = 59 };
    m.wifi = CYD_CLOCK_VIEW_WIFI_RECONNECTING;
    m.alarm = CYD_CLOCK_VIEW_ALARM_1_2;
    for (int h24 = 0; h24 < 2; ++h24) {
        m.use_24_hour = h24 != 0;
        for (int minute = 0; minute < 24 * 60; ++minute) {
            /* Seconds vary like the minutes; "x8" is the widest digit pair. */
            m.local_time = (struct tm){ .tm_year = 126, .tm_mon = 11, .tm_mday = 28, .tm_wday = 3,
                                        .tm_hour = minute / 60, .tm_min = minute % 60, .tm_sec = 48 };
            cyd_clock_view_build(&s_screen, &m);
            if (!ui_test_screen_fits(&s_screen)) {
                char name[48];
                snprintf(name, sizeof(name), "clock %02d:%02d %s", minute / 60, minute % 60, h24 ? "24h" : "12h");
                check_current(name);
                ++bad;
            }
        }
    }
    ui_test_check(bad == 0, "the clock face fits at every minute, 12- and 24-hour");

    for (int a = CYD_CLOCK_VIEW_ALARM_OFF; a <= CYD_CLOCK_VIEW_ALARM_1_2; ++a) {
        for (int w = CYD_CLOCK_VIEW_WIFI_UNAVAILABLE; w <= CYD_CLOCK_VIEW_WIFI_SETUP_RUNNING; ++w) {
            m.alarm = (cyd_clock_view_alarm_t)a;
            m.wifi = (cyd_clock_view_wifi_t)w;
            cyd_clock_view_build(&s_screen, &m);
            if (!ui_test_screen_fits(&s_screen)) {
                check_current("clock face, alarm and Wi-Fi states");
                ++bad;
            }
        }
    }
    ui_test_check(bad == 0, "every alarm label and Wi-Fi state fits");

    m.time_known = false;
    m.sync = CYD_CLOCK_VIEW_SYNC_FAILED;
    cyd_clock_view_build(&s_screen, &m);
    check_current("clock face, not synced");
}

static void test_clock_wifi(void)
{
    cyd_clock_view_model_t m = { .screen = CYD_CLOCK_VIEW_SCREEN_WIFI_FAILED };

    for (int f = CYD_CLOCK_VIEW_FAILURE_UNKNOWN; f <= CYD_CLOCK_VIEW_FAILURE_CONNECT; ++f) {
        char name[32];
        m.failure = (cyd_clock_view_failure_t)f;
        cyd_clock_view_build(&s_screen, &m);
        snprintf(name, sizeof(name), "Wi-Fi failed, reason %d", f);
        check_current(name);
    }
    ui_test_check(find_action(CYD_CLOCK_APP_ACTION_WIFI_RETRY) != NULL &&
                      find_action(CYD_CLOCK_APP_ACTION_WIFI_SETUP) != NULL,
                  "the failure screen offers retry and setup");

    m = (cyd_clock_view_model_t){ .screen = CYD_CLOCK_VIEW_SCREEN_WIFI_RETRYING,
                                  .retry = CYD_CLOCK_VIEW_RETRY_TRYING, .retry_ssid = "Office-Guest-Network-5G" };
    cyd_clock_view_build(&s_screen, &m);
    check_current("retrying a long SSID");
    m.retry = CYD_CLOCK_VIEW_RETRY_SEARCHING;
    cyd_clock_view_build(&s_screen, &m);
    check_current("retry, searching");
    m.retry = CYD_CLOCK_VIEW_RETRY_CONNECTING;
    cyd_clock_view_build(&s_screen, &m);
    check_current("retry, connecting");
}

static void test_clock_settings(void)
{
    cyd_clock_settings_view_model_t m = { .page = CYD_CLOCK_SETTINGS_VIEW_ALARM1, .hour = 23, .minute = 59,
                                          .weekday_mask = 0x7f };

    cyd_clock_settings_view_build(&s_screen, &m);
    check_current("alarm 1, every day");
    ui_test_check(find_action(CYD_CLOCK_SETTINGS_ACTION_ALARM1_WEEKDAY_BASE + 6) != NULL, "all seven days can be toggled");
    m.page = CYD_CLOCK_SETTINGS_VIEW_ALARM2;
    cyd_clock_settings_view_build(&s_screen, &m);
    check_current("alarm 2");

    m = (cyd_clock_settings_view_model_t){ .page = CYD_CLOCK_SETTINGS_VIEW_SCHEDULER,
                                           .schedule_count = CYD_CLOCK_SETTINGS_VIEW_SCHEDULES_MAX,
                                           .schedule_capacity = CYD_CLOCK_SETTINGS_VIEW_SCHEDULES_MAX };
    for (size_t i = 0; i < CYD_CLOCK_SETTINGS_VIEW_SCHEDULES_MAX; ++i) {
        /* Owner and tag at their display caps (7 and 8 characters). */
        m.schedules[i] = (cyd_clock_settings_view_schedule_t){ (unsigned)i + 1U, "sampler", "rssi_win", true, true,
                                                               false, CYD_CLOCK_SETTINGS_VIEW_SCHED_WAITING,
                                                               23, 59, 23, 59 };
    }
    cyd_clock_settings_view_build(&s_screen, &m);
    check_current("scheduler, full");
    const cyd_display_widget_t *last = &s_screen.widgets[s_screen.widget_count - 1];
    ui_test_check(last->row + last->span_rows <= 27, "the last schedule ends above the page navigation");
    m.schedules_unavailable = true;
    cyd_clock_settings_view_build(&s_screen, &m);
    check_current("scheduler unavailable");
}

int main(void)
{
    ui_test_set_all_text_immutable(false); /* times are formatted in stack buffers */
    printf("launcher\n");
    test_launcher();
    printf("clock face\n");
    test_clock_face();
    printf("clock Wi-Fi screens\n");
    test_clock_wifi();
    printf("clock settings\n");
    test_clock_settings();
    return ui_test_finish();
}
