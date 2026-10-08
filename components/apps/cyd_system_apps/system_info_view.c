#include <stdio.h>
#include "cyd_ui.h"
#include "system_info_view.h"

/*
 * Layout on the 8px grid. Item pages list up to seven "name  value" rows of
 * 24px between the header (rows 0-3) and the page navigation (rows 27-29):
 *   cols 1-13 name (six 16px kanji) | cols 14-38 value
 */
#define VIEW_APP_TITLE "システム情報"
#define VIEW_ITEM_FIRST_ROW 4
#define VIEW_ITEM_ROWS 3
#define VIEW_NAME_COL 1
#define VIEW_NAME_COLS 13
#define VIEW_VALUE_COL 14
#define VIEW_VALUE_COLS 25

static const char *const k_page_titles[SYSTEM_INFO_VIEW_PAGE_COUNT] = {
    "概要",
    "診断",
    "Wi-Fi 診断",
    "電波 (RSSI)",
    "NVS",
};

static const char *view_text(const char *text)
{
    return text != NULL ? text : "";
}

static const char *view_failure_text(system_info_view_failure_t failure)
{
    switch (failure) {
    case SYSTEM_INFO_VIEW_FAILURE_NO_SAVED_PROFILE: return "設定がありません";
    case SYSTEM_INFO_VIEW_FAILURE_NO_AP_IN_RANGE: return "AP が見つかりません";
    case SYSTEM_INFO_VIEW_FAILURE_AUTH: return "認証失敗";
    case SYSTEM_INFO_VIEW_FAILURE_TIMEOUT: return "タイムアウト";
    case SYSTEM_INFO_VIEW_FAILURE_CONNECT: return "接続失敗";
    default: return "なし";
    }
}

static const char *view_touch_calib_text(system_info_view_touch_calib_t calib)
{
    switch (calib) {
    case SYSTEM_INFO_VIEW_TOUCH_CALIB_SAVED: return "保存済み";
    case SYSTEM_INFO_VIEW_TOUCH_CALIB_DEFAULT: return "既定値";
    default: return "未保存";
    }
}

/* One "name  value" row; `index` counts from the first item row. */
static void view_item(cyd_display_screen_t *screen, size_t index, const char *name, const char *value,
                      uint16_t value_color)
{
    uint8_t row = (uint8_t)(VIEW_ITEM_FIRST_ROW + index * VIEW_ITEM_ROWS);

    cyd_ui_add_label(screen, name, VIEW_NAME_COL, row, VIEW_NAME_COLS, VIEW_ITEM_ROWS,
                     CYD_DISPLAY_ALIGN_LEFT, CYD_DISPLAY_FONT_BODY_BOLD, CYD_UI_THEME_SUBTEXT);
    cyd_ui_add_label(screen, value, VIEW_VALUE_COL, row, VIEW_VALUE_COLS, VIEW_ITEM_ROWS,
                     CYD_DISPLAY_ALIGN_LEFT, CYD_DISPLAY_FONT_BODY, value_color);
}

static void view_overview(cyd_display_screen_t *screen, const system_info_view_model_t *m)
{
    char chip[32];
    char heap[24];

    snprintf(chip, sizeof(chip), "rev%u / %u cores", m->chip_revision, m->chip_cores);
    snprintf(heap, sizeof(heap), "%u bytes", m->heap_free);

    view_item(screen, 0, "アプリ", view_text(m->app_name), CYD_UI_THEME_TEXT);
    view_item(screen, 1, "バージョン", view_text(m->app_version), CYD_UI_THEME_TEXT);
    view_item(screen, 2, "ESP-IDF", view_text(m->idf_version), CYD_UI_THEME_TEXT);
    view_item(screen, 3, "チップ", chip, CYD_UI_THEME_TEXT);
    view_item(screen, 4, "空きヒープ", heap, CYD_UI_THEME_TEXT);
    view_item(screen, 5, "Wi-Fi", system_settings_view_wifi_text(m->wifi), system_settings_view_wifi_color(m->wifi));
}

static void view_diag(cyd_display_screen_t *screen, const system_info_view_model_t *m)
{
    char heap[24];
    char heap_min[32];
    char sync_last[40];
    char profiles[24];

    snprintf(heap, sizeof(heap), "%u bytes", m->heap_free);
    snprintf(heap_min, sizeof(heap_min), "%u / %u", m->heap_min_free, m->heap_largest_block);
    /* Without the "前回: " prefix: the item name already says it. */
    system_settings_view_format_sync_last(sync_last, sizeof(sync_last), m->sync_last, &m->last_sync_at, false);
    if (m->profiles_readable) {
        snprintf(profiles, sizeof(profiles), "%u 件", m->profile_count);
    } else {
        snprintf(profiles, sizeof(profiles), "読めません");
    }

    view_item(screen, 0, "空きヒープ", heap, CYD_UI_THEME_TEXT);
    view_item(screen, 1, "最小/最大塊", heap_min, CYD_UI_THEME_TEXT);
    view_item(screen, 2, "Wi-Fi 失敗", view_failure_text(m->wifi_failure),
              m->wifi_failure == SYSTEM_INFO_VIEW_FAILURE_NONE ? CYD_UI_THEME_TEXT : CYD_UI_THEME_WARNING);
    view_item(screen, 3, "時刻同期", system_settings_view_sync_text(m->sync), CYD_UI_THEME_TEXT);
    view_item(screen, 4, "前回の同期", sync_last,
              m->sync_last == SYSTEM_SETTINGS_VIEW_SYNC_LAST_FAILED ? CYD_UI_THEME_DANGER_SOFT : CYD_UI_THEME_TEXT);
    view_item(screen, 5, "保存SSID", profiles, CYD_UI_THEME_TEXT);
    view_item(screen, 6, "タッチ補正", view_touch_calib_text(m->touch_calib), CYD_UI_THEME_TEXT);
}

static void view_wifi(cyd_display_screen_t *screen, const system_info_view_model_t *m)
{
    char users[24];
    char on[24];
    char max_on[24];

    if (m->wifi_users == 0) {
        snprintf(users, sizeof(users), "なし");
    } else if (m->wifi_users_is_radio_manager) {
        snprintf(users, sizeof(users), "radio_manager");
    } else {
        snprintf(users, sizeof(users), "0x%02lx", (unsigned long)m->wifi_users);
    }
    snprintf(on, sizeof(on), "%lu 秒", (unsigned long)m->wifi_on_seconds);
    snprintf(max_on, sizeof(max_on), "%lu 秒", (unsigned long)m->wifi_max_on_seconds);

    view_item(screen, 0, "状態", system_settings_view_wifi_text(m->wifi), system_settings_view_wifi_color(m->wifi));
    view_item(screen, 1, "利用中", users, CYD_UI_THEME_TEXT);
    view_item(screen, 2, "最後の利用", m->wifi_last_user_is_radio_manager ? "radio_manager" : "なし",
              CYD_UI_THEME_TEXT);
    view_item(screen, 3, "接続時間", on, CYD_UI_THEME_TEXT);
    view_item(screen, 4, "最長の接続", max_on, CYD_UI_THEME_TEXT);
    view_item(screen, 5, "警告", m->wifi_connected_too_long ? "接続が長すぎます" : "なし",
              m->wifi_connected_too_long ? CYD_UI_THEME_WARNING : CYD_UI_THEME_TEXT);
    view_item(screen, 6, "失敗の理由", view_failure_text(m->wifi_failure),
              m->wifi_failure == SYSTEM_INFO_VIEW_FAILURE_NONE ? CYD_UI_THEME_TEXT : CYD_UI_THEME_WARNING);
}

static void view_rssi(cyd_display_screen_t *screen, const system_info_view_model_t *m)
{
    char line[40];

    if (m->has_rssi) {
        snprintf(line, sizeof(line), "現在 %d dBm (%u 件)", m->rssi_now, m->rssi_samples);
    } else {
        /* This page never powers the radio up, so say plainly that Wi-Fi is
           off instead of implying the graph is about to start. */
        snprintf(line, sizeof(line), "Wi-Fi はオフです");
    }
    cyd_ui_add_label(screen, line, VIEW_NAME_COL, 4, 38, 3, CYD_DISPLAY_ALIGN_LEFT, CYD_DISPLAY_FONT_BODY_BOLD,
                     m->has_rssi ? CYD_UI_THEME_TEXT : CYD_UI_THEME_SUBTEXT);

    if (m->rssi_graph.samples != NULL) {
        cyd_ui_add_sparkline(screen, 7, 8, 32, 17, &m->rssi_graph, CYD_UI_THEME_SUCCESS, CYD_UI_THEME_BG,
                             CYD_UI_THEME_LINE);
        cyd_ui_add_label(screen, "-30", VIEW_NAME_COL, 8, 6, 2, CYD_DISPLAY_ALIGN_LEFT, CYD_DISPLAY_FONT_BODY,
                         CYD_UI_THEME_SUBTEXT);
        cyd_ui_add_label(screen, "-100", VIEW_NAME_COL, 23, 6, 2, CYD_DISPLAY_ALIGN_LEFT, CYD_DISPLAY_FONT_BODY,
                         CYD_UI_THEME_SUBTEXT);
    }
}

static void view_nvs(cyd_display_screen_t *screen, const system_info_view_model_t *m)
{
    char summary[48];
    size_t count = m->nvs_count < SYSTEM_INFO_VIEW_NVS_MAX ? m->nvs_count : SYSTEM_INFO_VIEW_NVS_MAX;

    if (m->nvs_scan_failed) {
        /* The error name is long and technical; it gets a line of its own. */
        snprintf(summary, sizeof(summary), "読み取りに失敗しました");
        cyd_ui_add_label(screen, view_text(m->nvs_error), VIEW_NAME_COL, 7, 38, 3, CYD_DISPLAY_ALIGN_LEFT,
                         CYD_DISPLAY_FONT_BODY, CYD_UI_THEME_SUBTEXT);
    } else if (m->nvs_total > count) {
        snprintf(summary, sizeof(summary), "namespace %u 個 (%u 個を表示)", m->nvs_total, (unsigned)count);
    } else {
        snprintf(summary, sizeof(summary), "namespace %u 個", m->nvs_total);
    }
    cyd_ui_add_label(screen, summary, VIEW_NAME_COL, 4, 38, 2, CYD_DISPLAY_ALIGN_LEFT, CYD_DISPLAY_FONT_BODY_BOLD,
                     m->nvs_scan_failed ? CYD_UI_THEME_DANGER_SOFT : CYD_UI_THEME_TEXT);

    /* Ten 16px rows: namespace | scope | entries. */
    for (size_t i = 0; i < count; ++i) {
        char entries[12];
        uint8_t row = (uint8_t)(6 + i * 2);
        snprintf(entries, sizeof(entries), "%u", m->nvs[i].entries);
        cyd_ui_add_label(screen, view_text(m->nvs[i].name), VIEW_NAME_COL, row, 20, 2, CYD_DISPLAY_ALIGN_LEFT,
                         CYD_DISPLAY_FONT_BODY, CYD_UI_THEME_TEXT);
        cyd_ui_add_label(screen, view_text(m->nvs[i].scope), 22, row, 10, 2, CYD_DISPLAY_ALIGN_LEFT,
                         CYD_DISPLAY_FONT_BODY, CYD_UI_THEME_SUBTEXT);
        cyd_ui_add_label(screen, entries, 32, row, 6, 2, CYD_DISPLAY_ALIGN_RIGHT, CYD_DISPLAY_FONT_BODY,
                         CYD_UI_THEME_TEXT);
    }
}

void system_info_view_build(cyd_display_screen_t *screen, const system_info_view_model_t *m)
{
    if (screen == NULL || m == NULL) {
        return;
    }
    const size_t page = m->page < SYSTEM_INFO_VIEW_PAGE_COUNT ? (size_t)m->page : 0U;

    cyd_ui_screen_clear(screen);
    cyd_ui_add_panel(screen, 0, 0, CYD_DISPLAY_GRID_COLS, CYD_DISPLAY_GRID_ROWS, CYD_UI_THEME_BG, 0, 0, 0);
    cyd_ui_add_settings_title(screen, VIEW_APP_TITLE);

    switch ((system_info_view_page_t)page) {
    case SYSTEM_INFO_VIEW_DIAG:
        view_diag(screen, m);
        break;
    case SYSTEM_INFO_VIEW_WIFI:
        view_wifi(screen, m);
        break;
    case SYSTEM_INFO_VIEW_RSSI:
        view_rssi(screen, m);
        break;
    case SYSTEM_INFO_VIEW_NVS:
        view_nvs(screen, m);
        break;
    default:
        view_overview(screen, m);
        break;
    }

    (void)cyd_ui_add_settings_page_nav(screen, k_page_titles[page], page, SYSTEM_INFO_VIEW_PAGE_COUNT,
                                       CYD_INFO_APP_ACTION_PREV_PAGE, CYD_INFO_APP_ACTION_NEXT_PAGE);
    cyd_ui_add_settings_back(screen, CYD_INFO_APP_ACTION_BACK);
}
