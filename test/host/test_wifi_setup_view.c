/*
 * Wi-Fi setup screens: the network list in every state, the notices and the
 * failure dialog, measured with the real fonts.
 */

#include <stdio.h>
#include <string.h>
#include "cyd_display.h"
#include "cyd_wifi_setup_view.h"
#include "ui_test_support.h"

static cyd_display_screen_t s_screen;

static const cyd_display_widget_t *find_action(uint16_t action_id)
{
    for (size_t i = 0; i < s_screen.widget_count; ++i) {
        if (s_screen.widgets[i].type == CYD_DISPLAY_WIDGET_BUTTON && s_screen.widgets[i].action_id == action_id) {
            return &s_screen.widgets[i];
        }
    }
    return NULL;
}

static void build_and_check(const char *name, const cyd_wifi_setup_view_model_t *m)
{
    cyd_wifi_setup_view_build(&s_screen, m);
    ui_test_check_screen(name, &s_screen);
}

static void test_list(void)
{
    cyd_wifi_setup_view_model_t m = { .screen = CYD_WIFI_SETUP_VIEW_SCAN, .page_index = 1, .page_count = 12 };

    for (size_t i = 0; i < CYD_WIFI_SETUP_VIEW_APS_PER_PAGE; ++i) {
        /* 22 characters fit; longer SSIDs are cut with "…", which is fine for a name. */
        m.aps[i] = (cyd_wifi_setup_view_ap_t){ "Office-Guest-Network-5", (int8_t)(-40 - (int)i * 12) };
    }
    m.ap_count = CYD_WIFI_SETUP_VIEW_APS_PER_PAGE;
    build_and_check("list, full page", &m);
    ui_test_check(find_action(CYD_WIFI_SETUP_VIEW_ACTION_AP_BASE + CYD_WIFI_SETUP_VIEW_APS_PER_PAGE - 1U) != NULL,
                  "every network on the page can be picked");
    ui_test_check(find_action(CYD_WIFI_SETUP_VIEW_ACTION_PREV)->enabled &&
                      find_action(CYD_WIFI_SETUP_VIEW_ACTION_NEXT)->enabled,
                  "a middle page can go both ways");
    const cyd_display_widget_t *last = find_action(CYD_WIFI_SETUP_VIEW_ACTION_AP_BASE + CYD_WIFI_SETUP_VIEW_APS_PER_PAGE - 1U);
    ui_test_check(last->row + last->span_rows <= find_action(CYD_WIFI_SETUP_VIEW_ACTION_RESCAN)->row,
                  "the list ends above the buttons");

    m.aps[0].ssid = "";
    build_and_check("list, hidden SSID", &m);

    m = (cyd_wifi_setup_view_model_t){ .screen = CYD_WIFI_SETUP_VIEW_SCAN, .scanning = true };
    build_and_check("scanning", &m);
    ui_test_check(!find_action(CYD_WIFI_SETUP_VIEW_ACTION_RESCAN)->enabled, "rescan is disabled while scanning");

    m = (cyd_wifi_setup_view_model_t){ .screen = CYD_WIFI_SETUP_VIEW_SCAN, .page_count = 1 };
    build_and_check("nothing found", &m);
    m.scan_failed = true;
    m.scan_error = "ESP_ERR_WIFI_NOT_STARTED";
    build_and_check("scan error", &m);

    ui_test_check(strcmp(cyd_wifi_setup_view_signal_text(-60), "強い") == 0 &&
                      strcmp(cyd_wifi_setup_view_signal_text(-61), "ふつう") == 0 &&
                      strcmp(cyd_wifi_setup_view_signal_text(-73), "弱い") == 0,
                  "signal words switch at -60 and -72 dBm");
}

static void test_notices(void)
{
    static const cyd_wifi_setup_view_failure_t failures[] = {
        CYD_WIFI_SETUP_VIEW_FAILURE_OTHER, CYD_WIFI_SETUP_VIEW_FAILURE_AUTH,
        CYD_WIFI_SETUP_VIEW_FAILURE_NOT_FOUND, CYD_WIFI_SETUP_VIEW_FAILURE_TIMEOUT,
    };
    cyd_wifi_setup_view_model_t m = { .screen = CYD_WIFI_SETUP_VIEW_CONNECTING, .ssid = "MyHome-5G" };

    build_and_check("connecting", &m);
    ui_test_check(find_action(CYD_WIFI_SETUP_VIEW_ACTION_BACK) == NULL, "connecting cannot be backed out of");
    m.screen = CYD_WIFI_SETUP_VIEW_SAVED;
    build_and_check("saved", &m);

    m.screen = CYD_WIFI_SETUP_VIEW_FAILED;
    m.error = "ESP_ERR_WIFI_NOT_CONNECT";
    for (size_t i = 0; i < sizeof(failures) / sizeof(failures[0]); ++i) {
        char name[32];
        m.failure = failures[i];
        snprintf(name, sizeof(name), "failed, reason %u", (unsigned)i);
        build_and_check(name, &m);
    }
    ui_test_check(find_action(CYD_WIFI_SETUP_VIEW_ACTION_OK) != NULL, "the failure dialog has an OK button");
}

int main(void)
{
    ui_test_set_all_text_immutable(false); /* page numbers are formatted in stack buffers */
    printf("network list\n");
    test_list();
    printf("notices\n");
    test_notices();
    return ui_test_finish();
}
