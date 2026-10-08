/*
 * Wi-Fi setup (cyd_wifi_setup_view.c): the network list in each state, the
 * connecting and saved notices, and the failure dialog for each reason. The
 * password keyboard is in common.c ("keyboard_*").
 */
#include "cyd_wifi_setup_view.h"
#include "sim_catalog.h"

static const cyd_wifi_setup_view_ap_t k_aps[] = {
    { "MyHome-5G", -42 },
    { "MyHome-2.4G", -58 },
    { "Office_Guest_Network_Long_Name", -66 },
    { "", -70 },
    { "Cafe-Free-WiFi", -81 },
};

static void build_list(cyd_display_screen_t *screen, unsigned frame)
{
    (void)frame;
    cyd_wifi_setup_view_model_t m = { .screen = CYD_WIFI_SETUP_VIEW_SCAN, .ap_count = 5, .page_count = 3 };
    for (size_t i = 0; i < 5; ++i) {
        m.aps[i] = k_aps[i];
    }
    cyd_wifi_setup_view_build(screen, &m);
}

static void build_last_page(cyd_display_screen_t *screen, unsigned frame)
{
    (void)frame;
    cyd_wifi_setup_view_model_t m = { .screen = CYD_WIFI_SETUP_VIEW_SCAN, .ap_count = 2, .page_index = 2,
                                      .page_count = 3 };
    m.aps[0] = k_aps[3];
    m.aps[1] = k_aps[4];
    cyd_wifi_setup_view_build(screen, &m);
}

static void build_scanning(cyd_display_screen_t *screen, unsigned frame)
{
    (void)frame;
    const cyd_wifi_setup_view_model_t m = { .screen = CYD_WIFI_SETUP_VIEW_SCAN, .scanning = true };
    cyd_wifi_setup_view_build(screen, &m);
}

static void build_empty(cyd_display_screen_t *screen, unsigned frame)
{
    (void)frame;
    const cyd_wifi_setup_view_model_t m = { .screen = CYD_WIFI_SETUP_VIEW_SCAN, .page_count = 1 };
    cyd_wifi_setup_view_build(screen, &m);
}

static void build_scan_error(cyd_display_screen_t *screen, unsigned frame)
{
    (void)frame;
    const cyd_wifi_setup_view_model_t m = { .screen = CYD_WIFI_SETUP_VIEW_SCAN, .scan_failed = true,
                                            .scan_error = "ESP_ERR_TIMEOUT", .page_count = 1 };
    cyd_wifi_setup_view_build(screen, &m);
}

static void build_connecting(cyd_display_screen_t *screen, unsigned frame)
{
    (void)frame;
    const cyd_wifi_setup_view_model_t m = { .screen = CYD_WIFI_SETUP_VIEW_CONNECTING, .ssid = "MyHome-5G" };
    cyd_wifi_setup_view_build(screen, &m);
}

static void build_saved(cyd_display_screen_t *screen, unsigned frame)
{
    (void)frame;
    const cyd_wifi_setup_view_model_t m = { .screen = CYD_WIFI_SETUP_VIEW_SAVED, .ssid = "MyHome-5G" };
    cyd_wifi_setup_view_build(screen, &m);
}

static void build_failed(cyd_display_screen_t *screen, cyd_wifi_setup_view_failure_t failure)
{
    const cyd_wifi_setup_view_model_t m = { .screen = CYD_WIFI_SETUP_VIEW_FAILED, .ssid = "MyHome-5G",
                                            .failure = failure, .error = "ESP_FAIL" };
    cyd_wifi_setup_view_build(screen, &m);
}

static void build_failed_auth(cyd_display_screen_t *screen, unsigned frame)
{
    (void)frame;
    build_failed(screen, CYD_WIFI_SETUP_VIEW_FAILURE_AUTH);
}

static void build_failed_not_found(cyd_display_screen_t *screen, unsigned frame)
{
    (void)frame;
    build_failed(screen, CYD_WIFI_SETUP_VIEW_FAILURE_NOT_FOUND);
}

static void build_failed_timeout(cyd_display_screen_t *screen, unsigned frame)
{
    (void)frame;
    build_failed(screen, CYD_WIFI_SETUP_VIEW_FAILURE_TIMEOUT);
}

static const cyd_sim_scene_t k_scenes[] = {
    { "wifi_list", "Wi-Fi: 一覧", "長い SSID、名前なし、電波の強さ", build_list },
    { "wifi_list_last_page", "Wi-Fi: 一覧 (最後のページ)", NULL, build_last_page },
    { "wifi_scanning", "Wi-Fi: 検索中", "ボタンは無効", build_scanning },
    { "wifi_empty", "Wi-Fi: 見つからない", NULL, build_empty },
    { "wifi_scan_error", "Wi-Fi: 検索エラー", NULL, build_scan_error },
    { "wifi_connecting", "Wi-Fi: 接続中", NULL, build_connecting },
    { "wifi_saved", "Wi-Fi: 接続・保存した", NULL, build_saved },
    { "wifi_failed_auth", "Wi-Fi: 失敗 (パスワード)", NULL, build_failed_auth },
    { "wifi_failed_not_found", "Wi-Fi: 失敗 (見つからない)", NULL, build_failed_not_found },
    { "wifi_failed_timeout", "Wi-Fi: 失敗 (時間切れ)", NULL, build_failed_timeout },
};

CYD_SIM_REGISTER_SCENES(wifi_setup, CYD_SIM_ORDER_SETTINGS + 1, k_scenes);
