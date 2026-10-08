#include <stdio.h>
#include "cyd_ui.h"
#include "cyd_wifi_setup_view.h"

/*
 * Layout on the 8px grid:
 *
 *   0-3    [戻る] Wi-Fi の設定                   1/3
 *   4-23   five networks, 32px each: SSID ........ 強い
 *   25-29  [再検索]       [前へ]       [次へ]
 */
#define VIEW_TITLE "Wi-Fi の設定"
#define VIEW_HEADER_ROWS 4
#define VIEW_LIST_ROW 4
#define VIEW_AP_ROWS 4
#define VIEW_CONTROL_ROW 25
#define VIEW_CONTROL_ROWS 5

const char *cyd_wifi_setup_view_signal_text(int8_t rssi)
{
    if (rssi >= -60) {
        return "強い";
    }
    if (rssi >= -72) {
        return "ふつう";
    }
    return "弱い";
}

static const char *view_text(const char *text)
{
    return text != NULL ? text : "";
}

static void view_header(cyd_display_screen_t *screen, bool with_back)
{
    cyd_ui_screen_clear(screen);
    cyd_ui_add_panel(screen, 0, 0, CYD_DISPLAY_GRID_COLS, CYD_DISPLAY_GRID_ROWS, CYD_UI_THEME_BG, 0, 0, 0);
    cyd_ui_add_panel(screen, 0, 0, CYD_DISPLAY_GRID_COLS, VIEW_HEADER_ROWS, CYD_UI_THEME_SURFACE, 0, 0, 0);
    cyd_ui_add_label(screen, VIEW_TITLE, 9, 0, 22, VIEW_HEADER_ROWS,
                     CYD_DISPLAY_ALIGN_LEFT, CYD_DISPLAY_FONT_BODY_BOLD, CYD_UI_THEME_TEXT);
    if (with_back) {
        cyd_ui_add_styled_button(screen, "戻る", 0, 0, 8, VIEW_HEADER_ROWS, CYD_DISPLAY_FONT_BODY_BOLD,
                                 CYD_UI_THEME_PRIMARY_SOFT, CYD_UI_THEME_SURFACE, CYD_UI_THEME_PRIMARY,
                                 CYD_UI_THEME_BORDER_PX, CYD_WIFI_SETUP_VIEW_ACTION_BACK, true);
    }
}

/* A centred notice in the list area: what is happening, and an optional detail. */
static void view_notice(cyd_display_screen_t *screen, const char *title, uint16_t title_color,
                        const char *detail, const char *technical)
{
    cyd_ui_add_label(screen, title, 1, 9, 38, 3, CYD_DISPLAY_ALIGN_CENTER, CYD_DISPLAY_FONT_BODY_BOLD, title_color);
    if (detail != NULL && detail[0] != '\0') {
        cyd_ui_add_label(screen, detail, 1, 13, 38, 3, CYD_DISPLAY_ALIGN_CENTER, CYD_DISPLAY_FONT_BODY,
                         CYD_UI_THEME_TEXT);
    }
    if (technical != NULL && technical[0] != '\0') {
        cyd_ui_add_label(screen, technical, 1, 16, 38, 3, CYD_DISPLAY_ALIGN_CENTER, CYD_DISPLAY_FONT_BODY,
                         CYD_UI_THEME_SUBTEXT);
    }
}

static void view_scan(cyd_display_screen_t *screen, const cyd_wifi_setup_view_model_t *m)
{
    const bool idle = !m->scanning;
    const size_t pages = m->page_count > 0 ? m->page_count : 1;

    view_header(screen, true);
    if (idle && m->ap_count > 0) {
        char page[16];
        snprintf(page, sizeof(page), "%u/%u", (unsigned)(m->page_index + 1U), (unsigned)pages);
        cyd_ui_add_label(screen, page, 31, 0, 8, VIEW_HEADER_ROWS,
                         CYD_DISPLAY_ALIGN_RIGHT, CYD_DISPLAY_FONT_BODY, CYD_UI_THEME_SUBTEXT);
    }

    if (m->scanning) {
        view_notice(screen, "探しています…", CYD_UI_THEME_INFO, "近くの Wi-Fi を検索しています", NULL);
    } else if (m->scan_failed) {
        view_notice(screen, "検索できませんでした", CYD_UI_THEME_DANGER_SOFT, "再検索を押してください",
                    view_text(m->scan_error));
    } else if (m->ap_count == 0) {
        view_notice(screen, "見つかりません", CYD_UI_THEME_WARNING, "近くに Wi-Fi がありません", NULL);
    } else {
        size_t count = m->ap_count < CYD_WIFI_SETUP_VIEW_APS_PER_PAGE ? m->ap_count : CYD_WIFI_SETUP_VIEW_APS_PER_PAGE;
        for (size_t i = 0; i < count; ++i) {
            const char *ssid = view_text(m->aps[i].ssid);
            uint8_t row = (uint8_t)(VIEW_LIST_ROW + i * VIEW_AP_ROWS);
            /* The button is the hit area; the labels on top read like a list row. */
            cyd_ui_add_styled_button(screen, "", 0, row, CYD_DISPLAY_GRID_COLS, VIEW_AP_ROWS,
                                     CYD_DISPLAY_FONT_BODY_BOLD, CYD_UI_THEME_TEXT, CYD_UI_THEME_SURFACE,
                                     CYD_UI_THEME_LINE, 1, (uint16_t)(CYD_WIFI_SETUP_VIEW_ACTION_AP_BASE + i), true);
            /* About 22 characters fit; a longer SSID is cut with "…". */
            cyd_ui_add_label(screen, ssid[0] != '\0' ? ssid : "（名前なし）", 1, row, 30, VIEW_AP_ROWS,
                             CYD_DISPLAY_ALIGN_LEFT, CYD_DISPLAY_FONT_BODY_BOLD, CYD_UI_THEME_TEXT);
            cyd_ui_add_label(screen, cyd_wifi_setup_view_signal_text(m->aps[i].rssi), 32, row, 6, VIEW_AP_ROWS,
                             CYD_DISPLAY_ALIGN_RIGHT, CYD_DISPLAY_FONT_BODY, CYD_UI_THEME_SUBTEXT);
        }
    }

    cyd_ui_add_styled_button(screen, "再検索", 1, VIEW_CONTROL_ROW, 12, VIEW_CONTROL_ROWS,
                             CYD_DISPLAY_FONT_BODY_BOLD, CYD_UI_THEME_PRIMARY_SOFT, CYD_UI_THEME_SURFACE,
                             CYD_UI_THEME_PRIMARY, CYD_UI_THEME_BORDER_PX, CYD_WIFI_SETUP_VIEW_ACTION_RESCAN, idle);
    cyd_ui_add_styled_button(screen, "前へ", 14, VIEW_CONTROL_ROW, 12, VIEW_CONTROL_ROWS,
                             CYD_DISPLAY_FONT_BODY_BOLD, CYD_UI_THEME_PRIMARY_SOFT, CYD_UI_THEME_SURFACE,
                             CYD_UI_THEME_PRIMARY, CYD_UI_THEME_BORDER_PX, CYD_WIFI_SETUP_VIEW_ACTION_PREV,
                             idle && m->page_index > 0);
    cyd_ui_add_styled_button(screen, "次へ", 27, VIEW_CONTROL_ROW, 12, VIEW_CONTROL_ROWS,
                             CYD_DISPLAY_FONT_BODY_BOLD, CYD_UI_THEME_PRIMARY_SOFT, CYD_UI_THEME_SURFACE,
                             CYD_UI_THEME_PRIMARY, CYD_UI_THEME_BORDER_PX, CYD_WIFI_SETUP_VIEW_ACTION_NEXT,
                             idle && m->page_index + 1U < pages);
}

static const char *view_failure_text(cyd_wifi_setup_view_failure_t failure)
{
    switch (failure) {
    case CYD_WIFI_SETUP_VIEW_FAILURE_AUTH: return "パスワードが違うかもしれません";
    case CYD_WIFI_SETUP_VIEW_FAILURE_NOT_FOUND: return "ネットワークが見つかりません";
    case CYD_WIFI_SETUP_VIEW_FAILURE_TIMEOUT: return "時間内に接続できませんでした";
    default: return "接続に失敗しました";
    }
}

void cyd_wifi_setup_view_build(cyd_display_screen_t *screen, const cyd_wifi_setup_view_model_t *m)
{
    if (screen == NULL || m == NULL) {
        return;
    }

    switch (m->screen) {
    case CYD_WIFI_SETUP_VIEW_CONNECTING:
        view_header(screen, false);
        view_notice(screen, "接続しています…", CYD_UI_THEME_INFO, view_text(m->ssid), "少し時間がかかります");
        break;
    case CYD_WIFI_SETUP_VIEW_SAVED:
        view_header(screen, false);
        view_notice(screen, "接続しました", CYD_UI_THEME_SUCCESS_SOFT, view_text(m->ssid), "設定を保存しました");
        break;
    case CYD_WIFI_SETUP_VIEW_FAILED:
        view_header(screen, false);
        view_notice(screen, "接続できませんでした", CYD_UI_THEME_DANGER_SOFT, view_failure_text(m->failure),
                    view_text(m->error));
        cyd_ui_add_label(screen, view_text(m->ssid), 1, 4, 38, 3,
                         CYD_DISPLAY_ALIGN_CENTER, CYD_DISPLAY_FONT_BODY_BOLD, CYD_UI_THEME_SUBTEXT);
        cyd_ui_add_styled_button(screen, "OK", 10, 22, 20, 5, CYD_DISPLAY_FONT_BODY_BOLD, CYD_UI_THEME_ON_PRIMARY,
                                 CYD_UI_THEME_PRIMARY, CYD_UI_THEME_PRIMARY, 0, CYD_WIFI_SETUP_VIEW_ACTION_OK, true);
        break;
    default:
        view_scan(screen, m);
        break;
    }
}
