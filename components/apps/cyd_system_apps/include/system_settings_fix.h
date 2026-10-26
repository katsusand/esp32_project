#ifndef SYSTEM_SETTINGS_FIX_H
#define SYSTEM_SETTINGS_FIX_H

/*
 * Settings items a product fixes, so nobody on site sets them to a value the
 * product cannot work with. Example: a monitoring unit must not drop Wi-Fi
 * when idle, so "Wi-Fi切断" must stay "しない".
 *
 * The settings PIN decides who may enter settings; this decides what even
 * they may not change. See "Fixed Items" in docs/cyd_system_apps.md.
 *
 * Only the product composition fixes items, once at boot, in this order:
 *
 *   1. start the service                 radio_manager_start()
 *   2. give it the product's value       radio_manager_set_idle_timeout_seconds(0)
 *   3. fix the item                      system_settings_fix(SYSTEM_SETTINGS_ITEM_WIFI_IDLE, ...)
 *   4. start the shell                   app_shell_start()
 *
 * Step 2 is what fixes the value; step 3 only stops the settings screen from
 * changing it. The value set in step 2 wins over the one NVS held, and
 * settings saves it back when the user leaves settings.
 *
 * English contract: RAM only, nothing is saved. Call any number of times
 * before app_shell_start(); the last call for an item wins, and
 * SYSTEM_SETTINGS_FIX_NONE undoes it. After app_shell_start() it returns
 * ESP_ERR_INVALID_STATE: apps never fix items, so the one place to look for
 * why an item cannot be changed is the composition. A product that never calls
 * it gets the settings screens exactly as before.
 */

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The values are internal; products use the names. */
typedef enum {
    /* Items with a value: READONLY or HIDDEN. */
    SYSTEM_SETTINGS_ITEM_BRIGHTNESS = 0, /* 一般: 画面の明るさ */
    SYSTEM_SETTINGS_ITEM_THEME,          /* 一般: 配色 */
    SYSTEM_SETTINGS_ITEM_IDLE_RETURN,    /* 一般: 無操作で戻る */
    SYSTEM_SETTINGS_ITEM_TIMEZONE,       /* 時刻: タイムゾーン */
    SYSTEM_SETTINGS_ITEM_SYNC_INTERVAL,  /* ネットワーク2: 同期の間隔 */
    SYSTEM_SETTINGS_ITEM_WIFI_IDLE,      /* ネットワーク2: Wi-Fi切断 */
    /* Actions, with no value to show: HIDDEN only. */
    SYSTEM_SETTINGS_ITEM_TOUCH_CALIBRATE,   /* 一般: タッチ位置の補正 */
    SYSTEM_SETTINGS_ITEM_STORED_SSIDS,      /* ネットワーク1: 保存済みのネットワーク */
    SYSTEM_SETTINGS_ITEM_WIFI_SETUP,        /* ネットワーク1: Wi-Fi を設定する */
    SYSTEM_SETTINGS_ITEM_SYNC_NOW,          /* ネットワーク2: 今すぐ時刻を合わせる */
    SYSTEM_SETTINGS_ITEM_CLEAR_TOUCH_CALIB, /* 初期化: タッチ補正を消去 */
    SYSTEM_SETTINGS_ITEM_CLEAR_APP_DATA,    /* 初期化: アプリのデータを消去 */
    SYSTEM_SETTINGS_ITEM_CLEAR_NVS,         /* 初期化: すべて初期化 (the forced Initialize NVS flow still opens) */
    SYSTEM_SETTINGS_ITEM_COUNT,
} system_settings_item_t;

typedef enum {
    SYSTEM_SETTINGS_FIX_NONE = 0, /* changeable, as without a fix */
    /* Shown greyed out with its value, and the reason under the value. For a
       value worth seeing on site; otherwise prefer HIDDEN. */
    SYSTEM_SETTINGS_FIX_READONLY,
    /* Not drawn. The row stays empty so the other rows keep their places; a
       page whose items are all hidden is left out of the page rotation. */
    SYSTEM_SETTINGS_FIX_HIDDEN,
} system_settings_fix_mode_t;

/*
 * `reason` is drawn under the greyed value of a READONLY item and written to
 * the boot log for both modes; it may be NULL. It is not copied: pass a
 * literal. 16px in 200px: up to 12 full-width characters, e.g.
 * "常時接続（監視のため）". Written as a literal in the product's source, its
 * characters are added to the UI fonts at the next font build
 * (docs/cyd_ui_fonts.md); a character the fonts lack shows as a box.
 *
 * Errors: ESP_ERR_INVALID_ARG for an unknown item or mode,
 * ESP_ERR_NOT_SUPPORTED for READONLY on an action item,
 * ESP_ERR_INVALID_STATE after app_shell_start().
 */
esp_err_t system_settings_fix(system_settings_item_t item, system_settings_fix_mode_t mode, const char *reason);

system_settings_fix_mode_t system_settings_fix_mode(system_settings_item_t item);
/* NULL when the item is not fixed or no reason was given. */
const char *system_settings_fix_reason(system_settings_item_t item);
/* "wifi_idle" and so on, for logs; "" for an unknown item. */
const char *system_settings_item_name(system_settings_item_t item);

#ifdef __cplusplus
}
#endif

#endif
