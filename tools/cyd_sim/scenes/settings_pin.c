/*
 * The settings PIN: the security page of settings, the PIN screen in each of
 * its states (settings_pin_view.c), and turning the PIN off.
 */
#include <string.h>
#include "sim_catalog.h"
#include "settings_pin_view.h"
#include "system_settings_view.h"

static settings_pin_view_model_t pin(settings_pin_view_screen_t screen, uint8_t entered)
{
    settings_pin_view_model_t m;

    memset(&m, 0, sizeof(m));
    m.screen = screen;
    m.title = "設定";
    m.entered = entered;
    return m;
}

static void build_enter(cyd_display_screen_t *screen, unsigned frame)
{
    (void)frame;
    settings_pin_view_model_t m = pin(SETTINGS_PIN_VIEW_ENTER, 0);
    settings_pin_view_build(screen, &m);
}

static void build_enter_typing(cyd_display_screen_t *screen, unsigned frame)
{
    (void)frame;
    settings_pin_view_model_t m = pin(SETTINGS_PIN_VIEW_ENTER, 6);
    settings_pin_view_build(screen, &m);
}

static void build_wrong(cyd_display_screen_t *screen, unsigned frame)
{
    (void)frame;
    settings_pin_view_model_t m = pin(SETTINGS_PIN_VIEW_ENTER, 0);
    m.notice = SETTINGS_PIN_VIEW_NOTICE_WRONG;
    settings_pin_view_build(screen, &m);
}

static void build_wait(cyd_display_screen_t *screen, unsigned frame)
{
    settings_pin_view_model_t m = pin(SETTINGS_PIN_VIEW_ENTER, 0);
    /* Counts down from 30 s like the device, one step a second at 60 fps. */
    m.notice = SETTINGS_PIN_VIEW_NOTICE_WRONG;
    m.wait_seconds = 30U - (frame / 60U) % 30U;
    settings_pin_view_build(screen, &m);
}

static void build_wait_long(cyd_display_screen_t *screen, unsigned frame)
{
    (void)frame;
    settings_pin_view_model_t m = pin(SETTINGS_PIN_VIEW_ENTER, 0);
    m.wait_seconds = 15U * 60U + 30U;
    settings_pin_view_build(screen, &m);
}

static void build_new_first(cyd_display_screen_t *screen, unsigned frame)
{
    (void)frame;
    settings_pin_view_model_t m = pin(SETTINGS_PIN_VIEW_NEW_FIRST, 4);
    m.title = "PIN の設定";
    settings_pin_view_build(screen, &m);
}

static void build_new_second(cyd_display_screen_t *screen, unsigned frame)
{
    (void)frame;
    settings_pin_view_model_t m = pin(SETTINGS_PIN_VIEW_NEW_SECOND, 2);
    m.title = "PIN の設定";
    settings_pin_view_build(screen, &m);
}

static void build_mismatch(cyd_display_screen_t *screen, unsigned frame)
{
    (void)frame;
    settings_pin_view_model_t m = pin(SETTINGS_PIN_VIEW_NEW_FIRST, 0);
    m.title = "PIN の設定";
    m.notice = SETTINGS_PIN_VIEW_NOTICE_MISMATCH;
    settings_pin_view_build(screen, &m);
}

static void build_recovery(cyd_display_screen_t *screen, unsigned frame)
{
    (void)frame;
    settings_pin_view_model_t m = pin(SETTINGS_PIN_VIEW_ENTER, 0);
    m.recovery_label = "コードで開く";
    settings_pin_view_build(screen, &m);
}

static void build_info(cyd_display_screen_t *screen, unsigned frame)
{
    (void)frame;
    settings_pin_view_model_t m = pin(SETTINGS_PIN_VIEW_ENTER, 3);
    m.title = "システム情報";
    settings_pin_view_build(screen, &m);
}

static void build_restart(cyd_display_screen_t *screen, unsigned frame)
{
    (void)frame;
    settings_pin_view_model_t m = pin(SETTINGS_PIN_VIEW_MESSAGE, 0);
    m.message_title = "再起動します";
    m.message_detail = "設定には入りません";
    settings_pin_view_build(screen, &m);
}

static void build_security(cyd_display_screen_t *screen, bool enabled, bool info, bool recovery)
{
    system_settings_view_model_t m;

    memset(&m, 0, sizeof(m));
    m.screen = SYSTEM_SETTINGS_VIEW_SECURITY;
    m.page_title = "設定のPIN";
    m.page_index = 1;
    m.page_count = 7;
    m.pin_enabled = enabled;
    m.pin_info_lock = info;
    m.pin_has_recovery = recovery;
    system_settings_view_build(screen, &m);
}

static void build_security_off(cyd_display_screen_t *screen, unsigned frame)
{
    (void)frame;
    build_security(screen, false, false, false);
}

static void build_security_on(cyd_display_screen_t *screen, unsigned frame)
{
    (void)frame;
    build_security(screen, true, true, false);
}

static void build_security_recovery(cyd_display_screen_t *screen, unsigned frame)
{
    (void)frame;
    build_security(screen, true, false, true);
}

static void build_off_confirm(cyd_display_screen_t *screen, unsigned frame)
{
    (void)frame;
    system_settings_view_model_t m;

    memset(&m, 0, sizeof(m));
    m.screen = SYSTEM_SETTINGS_VIEW_PIN_OFF_CONFIRM;
    system_settings_view_build(screen, &m);
}

static const cyd_sim_scene_t k_scenes[] = {
    { "sys_security", "設定: 設定のPIN (オフ)", "情報も保護はグレー", build_security_off },
    { "sys_security_on", "設定: 設定のPIN (オン)", "情報も保護もオン", build_security_on },
    { "sys_security_recovery", "設定: 設定のPIN (解除の手段あり)", "派生側が解除の手段を登録", build_security_recovery },
    { "sys_pin_off", "確認: PIN をオフにする", NULL, build_off_confirm },
    { "pin_enter", "PIN: 入力", NULL, build_enter },
    { "pin_enter_typing", "PIN: 6 桁入力済み", "決定が押せる", build_enter_typing },
    { "pin_wrong", "PIN: 違う", NULL, build_wrong },
    { "pin_wait", "PIN: 待ち時間", "30 秒から数える。キーは無効", build_wait },
    { "pin_wait_long", "PIN: 待ち時間 (分)", "15 分 30 秒", build_wait_long },
    { "pin_new_first", "PIN: 新しい PIN (1 回目)", NULL, build_new_first },
    { "pin_new_second", "PIN: 新しい PIN (2 回目)", NULL, build_new_second },
    { "pin_mismatch", "PIN: 2 回が一致しない", "最初からやり直し", build_mismatch },
    { "pin_recovery", "PIN: 解除のボタンあり", "派生側の解除の手段", build_recovery },
    { "pin_info", "PIN: システム情報の前", NULL, build_info },
    { "pin_restart", "PIN: 起動時にやめる", "戻る先が無いので再起動", build_restart },
};

CYD_SIM_REGISTER_SCENES(settings_pin, CYD_SIM_ORDER_SETTINGS, k_scenes);
