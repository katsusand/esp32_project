/*
 * Settings items fixed by the product (system_settings_fix()): hidden rows
 * leave their place empty, read-only rows show the value greyed out with the
 * reason under it.
 */
#include <string.h>
#include "sim_catalog.h"
#include "system_settings_view.h"

static system_settings_view_model_t network2(void)
{
    system_settings_view_model_t m;

    memset(&m, 0, sizeof(m));
    m.screen = SYSTEM_SETTINGS_VIEW_NETWORK2;
    m.page_title = "ネットワーク2";
    m.page_index = 4;
    m.page_count = 7;
    m.sync_interval_minutes = 90;
    m.can_sync_interval_down = true;
    m.can_sync_interval_up = true;
    m.wifi_idle_seconds = 0;
    m.can_wifi_idle_up = true;
    m.sync_now_enabled = true;
    m.sync = SYSTEM_SETTINGS_VIEW_SYNC_IDLE;
    m.sync_last = SYSTEM_SETTINGS_VIEW_SYNC_LAST_OK_AT;
    m.last_sync_at = (struct tm){ .tm_mon = 9, .tm_mday = 26, .tm_hour = 9, .tm_min = 41 };
    return m;
}

static void build_wifi_idle_hidden(cyd_display_screen_t *screen, unsigned frame)
{
    (void)frame;
    system_settings_view_model_t m = network2();
    m.fix[SYSTEM_SETTINGS_ITEM_WIFI_IDLE] = SYSTEM_SETTINGS_FIX_HIDDEN;
    system_settings_view_build(screen, &m);
}

static void build_wifi_idle_readonly(cyd_display_screen_t *screen, unsigned frame)
{
    (void)frame;
    system_settings_view_model_t m = network2();
    m.fix[SYSTEM_SETTINGS_ITEM_WIFI_IDLE] = SYSTEM_SETTINGS_FIX_READONLY;
    m.fix_reason[SYSTEM_SETTINGS_ITEM_WIFI_IDLE] = "接続したままにするため";
    system_settings_view_build(screen, &m);
}

static void build_general(cyd_display_screen_t *screen, unsigned frame)
{
    (void)frame;
    system_settings_view_model_t m;

    memset(&m, 0, sizeof(m));
    m.screen = SYSTEM_SETTINGS_VIEW_GENERAL;
    m.page_title = "一般";
    m.page_count = 7;
    m.brightness_percent = 100;
    m.idle_return_seconds = 90;
    m.can_idle_return_down = true;
    m.can_idle_return_up = true;
    m.theme_name = "高コントラスト";
    m.can_theme_up = true;
    m.fix[SYSTEM_SETTINGS_ITEM_BRIGHTNESS] = SYSTEM_SETTINGS_FIX_READONLY;
    m.fix_reason[SYSTEM_SETTINGS_ITEM_BRIGHTNESS] = "見やすさのため";
    m.fix[SYSTEM_SETTINGS_ITEM_THEME] = SYSTEM_SETTINGS_FIX_READONLY;
    m.fix[SYSTEM_SETTINGS_ITEM_TOUCH_CALIBRATE] = SYSTEM_SETTINGS_FIX_HIDDEN;
    system_settings_view_build(screen, &m);
}

static void build_nvs(cyd_display_screen_t *screen, unsigned frame)
{
    (void)frame;
    system_settings_view_model_t m;

    memset(&m, 0, sizeof(m));
    m.screen = SYSTEM_SETTINGS_VIEW_NVS;
    m.page_title = "初期化";
    m.page_index = 5;
    m.page_count = 7;
    m.fix[SYSTEM_SETTINGS_ITEM_CLEAR_APP_DATA] = SYSTEM_SETTINGS_FIX_HIDDEN;
    m.fix[SYSTEM_SETTINGS_ITEM_CLEAR_NVS] = SYSTEM_SETTINGS_FIX_HIDDEN;
    system_settings_view_build(screen, &m);
}

static const cyd_sim_scene_t k_scenes[] = {
    { "sys_fixed_wifi_idle_hidden", "固定: Wi-Fi切断を隠す", "行の場所は空いたまま", build_wifi_idle_hidden },
    { "sys_fixed_wifi_idle_readonly", "固定: Wi-Fi切断を変えられない", "理由を値の下に", build_wifi_idle_readonly },
    { "sys_fixed_general", "固定: 一般", "明るさ・配色は変えられない、タッチ補正は隠す", build_general },
    { "sys_fixed_nvs", "固定: 初期化", "タッチ補正の消去だけ残す", build_nvs },
};

CYD_SIM_REGISTER_SCENES(settings_fixed, CYD_SIM_ORDER_SETTINGS, k_scenes);
