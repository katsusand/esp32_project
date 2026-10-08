#include <stdio.h>
#include "cyd_clock_view.h"
#include "cyd_ui.h"

/*
 * Clock face on the 8px grid:
 *
 *   3-5    2026年10月8日（木）
 *   6-8    午後                      (12-hour only)
 *   9-16   09:41:00                  (48px digits)
 *   18-20  時刻同期: 10/8 09:41 成功
 *   20-22  Wi-Fi: 接続済み
 *   24-28  [設定] [情報] [アラーム 1と2]
 *
 * The SD card icon, when there is one, sits in the top-right corner.
 */
#define VIEW_SD_ICON_COL 38
#define VIEW_SD_ICON_ROW 0
#define VIEW_BUTTON_ROW 24
#define VIEW_BUTTON_ROWS 5

static const char *view_text(const char *text)
{
    return text != NULL ? text : "";
}

static const char *view_wifi_text(cyd_clock_view_wifi_t wifi)
{
    switch (wifi) {
    case CYD_CLOCK_VIEW_WIFI_STOPPED: return "停止中";
    case CYD_CLOCK_VIEW_WIFI_INIT: return "準備中";
    case CYD_CLOCK_VIEW_WIFI_OFF: return "オフ";
    case CYD_CLOCK_VIEW_WIFI_CONNECTING: return "接続中…";
    case CYD_CLOCK_VIEW_WIFI_CONNECTED: return "接続済み";
    case CYD_CLOCK_VIEW_WIFI_RECONNECTING: return "再接続中…";
    case CYD_CLOCK_VIEW_WIFI_FAILED: return "接続できません";
    case CYD_CLOCK_VIEW_WIFI_SETUP_REQUIRED: return "未設定";
    case CYD_CLOCK_VIEW_WIFI_SETUP_RUNNING: return "設定中";
    default: return "状態不明";
    }
}

static const char *view_alarm_text(cyd_clock_view_alarm_t alarm)
{
    switch (alarm) {
    case CYD_CLOCK_VIEW_ALARM_1: return "アラーム 1";
    case CYD_CLOCK_VIEW_ALARM_2: return "アラーム 2";
    case CYD_CLOCK_VIEW_ALARM_1_2: return "アラーム 1と2";
    default: return "アラーム オフ";
    }
}

static const char *view_failure_text(cyd_clock_view_failure_t failure)
{
    switch (failure) {
    case CYD_CLOCK_VIEW_FAILURE_NO_SAVED_PROFILE: return "Wi-Fi が設定されていません";
    case CYD_CLOCK_VIEW_FAILURE_NO_AP_IN_RANGE: return "保存したネットワークが見つかりません";
    case CYD_CLOCK_VIEW_FAILURE_AUTH: return "パスワードが違うかもしれません";
    case CYD_CLOCK_VIEW_FAILURE_TIMEOUT: return "時間内に接続できませんでした";
    case CYD_CLOCK_VIEW_FAILURE_CONNECT: return "接続に失敗しました";
    default: return "Wi-Fi を使えません";
    }
}

static void view_background(cyd_display_screen_t *screen)
{
    cyd_ui_screen_clear(screen);
    cyd_ui_add_panel(screen, 0, 0, CYD_DISPLAY_GRID_COLS, CYD_DISPLAY_GRID_ROWS, CYD_UI_THEME_BG, 0, 0, 0);
}

static void view_centered(cyd_display_screen_t *screen, const char *text, uint8_t row, cyd_display_font_t font,
                          uint16_t color)
{
    cyd_ui_add_label(screen, text, 1, row, 38, 3, CYD_DISPLAY_ALIGN_CENTER, font, color);
}

static void view_button(cyd_display_screen_t *screen, const char *text, uint8_t col, uint8_t span_cols,
                        uint16_t fg, uint16_t bg, uint16_t border, uint16_t action_id)
{
    cyd_ui_add_styled_button(screen, text, col, VIEW_BUTTON_ROW, span_cols, VIEW_BUTTON_ROWS,
                             CYD_DISPLAY_FONT_BODY_BOLD, fg, bg, border,
                             bg == border ? 0 : CYD_UI_THEME_BORDER_PX, action_id, true);
}

static void view_face(cyd_display_screen_t *screen, const cyd_clock_view_model_t *m)
{
    static const char *const weekdays[] = { "日", "月", "火", "水", "木", "金", "土" };
    const struct tm *t = &m->local_time;
    char date[48];
    char clock[16];
    char sync[48];
    char wifi[40];

    if (m->time_known) {
        int hour = t->tm_hour;
        snprintf(date, sizeof(date), "%d年%d月%d日（%s）", t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
                 weekdays[(unsigned)t->tm_wday % 7U]);
        if (!m->use_24_hour) {
            hour = hour % 12 == 0 ? 12 : hour % 12;
        }
        snprintf(clock, sizeof(clock), "%02d:%02d:%02d", hour, t->tm_min, t->tm_sec);
    } else {
        snprintf(date, sizeof(date), "時刻を合わせています…");
        snprintf(clock, sizeof(clock), "--:--:--");
    }
    switch (m->sync) {
    case CYD_CLOCK_VIEW_SYNC_FAILED:
        snprintf(sync, sizeof(sync), "時刻同期: 失敗");
        break;
    case CYD_CLOCK_VIEW_SYNC_OK_AT:
        snprintf(sync, sizeof(sync), "時刻同期: %d/%d %02d:%02d 成功", m->last_sync_at.tm_mon + 1,
                 m->last_sync_at.tm_mday, m->last_sync_at.tm_hour, m->last_sync_at.tm_min);
        break;
    default:
        snprintf(sync, sizeof(sync), "時刻同期: まだ");
        break;
    }
    snprintf(wifi, sizeof(wifi), "Wi-Fi: %s", view_wifi_text(m->wifi));

    view_background(screen);
    view_centered(screen, date, 3, CYD_DISPLAY_FONT_BODY_BOLD, m->time_known ? CYD_UI_THEME_TEXT : CYD_UI_THEME_INFO);
    if (m->time_known && !m->use_24_hour) {
        view_centered(screen, m->local_time.tm_hour < 12 ? "午前" : "午後", 6, CYD_DISPLAY_FONT_BODY_BOLD,
                      CYD_UI_THEME_SUBTEXT);
    }
    /* 48px, not 64px: "HH:MM:SS" in 64px fills the whole width for some times
       and not others, so the renderer would shrink it from one second to the
       next. One fixed size never jumps. */
    cyd_ui_add_label(screen, clock, CYD_CLOCK_VIEW_TIME_COL, CYD_CLOCK_VIEW_TIME_ROW, CYD_CLOCK_VIEW_TIME_SPAN_COLS,
                     CYD_CLOCK_VIEW_TIME_SPAN_ROWS, CYD_DISPLAY_ALIGN_CENTER,
                     CYD_DISPLAY_FONT_CLOCK_MEDIUM, m->time_known ? CYD_UI_THEME_TEXT : CYD_UI_THEME_SUBTEXT);
    cyd_ui_add_label(screen, sync, 1, 18, 38, 2, CYD_DISPLAY_ALIGN_CENTER, CYD_DISPLAY_FONT_BODY,
                     m->sync == CYD_CLOCK_VIEW_SYNC_FAILED ? CYD_UI_THEME_DANGER : CYD_UI_THEME_SUBTEXT);
    cyd_ui_add_label(screen, wifi, 1, 20, 38, 2, CYD_DISPLAY_ALIGN_CENTER, CYD_DISPLAY_FONT_BODY,
                     CYD_UI_THEME_SUBTEXT);

    view_button(screen, "設定", 1, 9, CYD_UI_THEME_PRIMARY_SOFT, CYD_UI_THEME_SURFACE, CYD_UI_THEME_PRIMARY,
                CYD_CLOCK_APP_ACTION_SETTINGS);
    view_button(screen, "情報", 11, 9, CYD_UI_THEME_PRIMARY_SOFT, CYD_UI_THEME_SURFACE, CYD_UI_THEME_PRIMARY,
                CYD_CLOCK_APP_ACTION_INFO);
    if (m->alarm == CYD_CLOCK_VIEW_ALARM_OFF) {
        view_button(screen, view_alarm_text(m->alarm), 21, 18, CYD_UI_THEME_SUBTEXT, CYD_UI_THEME_SURFACE,
                    CYD_UI_THEME_LINE, CYD_CLOCK_APP_ACTION_ALARM);
    } else {
        view_button(screen, view_alarm_text(m->alarm), 21, 18, CYD_UI_THEME_WARNING, CYD_UI_THEME_WARNING_TINT,
                    CYD_UI_THEME_WARNING, CYD_CLOCK_APP_ACTION_ALARM);
    }

    /* Last, so the widgets before it keep their positions whether or not it is
       there. */
    if (m->sd_icon != NULL) {
        (void)cyd_ui_add_icon(screen, m->sd_icon, VIEW_SD_ICON_COL, VIEW_SD_ICON_ROW, 2, 2);
    }
}

static void view_wifi_failed(cyd_display_screen_t *screen, const cyd_clock_view_model_t *m)
{
    view_background(screen);
    view_centered(screen, "Wi-Fi に接続できません", 6, CYD_DISPLAY_FONT_BODY_BOLD, CYD_UI_THEME_DANGER);
    view_centered(screen, view_failure_text(m->failure), 10, CYD_DISPLAY_FONT_BODY, CYD_UI_THEME_TEXT);
    view_centered(screen, "もう一度試すか、設定してください", 14, CYD_DISPLAY_FONT_BODY,
                  CYD_UI_THEME_SUBTEXT);
    view_button(screen, "もう一度", 1, 18, CYD_UI_THEME_PRIMARY_SOFT, CYD_UI_THEME_SURFACE, CYD_UI_THEME_PRIMARY,
                CYD_CLOCK_APP_ACTION_WIFI_RETRY);
    view_button(screen, "Wi-Fi を設定", 21, 18, CYD_UI_THEME_ON_PRIMARY, CYD_UI_THEME_PRIMARY, CYD_UI_THEME_PRIMARY,
                CYD_CLOCK_APP_ACTION_WIFI_SETUP);
}

static void view_wifi_retrying(cyd_display_screen_t *screen, const cyd_clock_view_model_t *m)
{
    /* Literals and the SSID go to the label as they are: a literal is
       referenced whole, and the SSID (at most 32 bytes) fits the 40-byte copy. */
    const char *line = "Wi-Fi に接続します";

    if (m->retry == CYD_CLOCK_VIEW_RETRY_SEARCHING) {
        line = "保存したネットワークを探しています";
    } else if (m->retry == CYD_CLOCK_VIEW_RETRY_TRYING) {
        line = view_text(m->retry_ssid);
    }
    view_background(screen);
    view_centered(screen, "Wi-Fi に接続しています…", 8, CYD_DISPLAY_FONT_BODY_BOLD, CYD_UI_THEME_INFO);
    view_centered(screen, line, 12, CYD_DISPLAY_FONT_BODY, CYD_UI_THEME_TEXT);
    view_centered(screen, "しばらくお待ちください", 16, CYD_DISPLAY_FONT_BODY, CYD_UI_THEME_SUBTEXT);
}

void cyd_clock_view_build(cyd_display_screen_t *screen, const cyd_clock_view_model_t *m)
{
    if (screen == NULL || m == NULL) {
        return;
    }
    switch (m->screen) {
    case CYD_CLOCK_VIEW_SCREEN_WIFI_FAILED:
        view_wifi_failed(screen, m);
        break;
    case CYD_CLOCK_VIEW_SCREEN_WIFI_RETRYING:
        view_wifi_retrying(screen, m);
        break;
    default:
        view_face(screen, m);
        break;
    }
}
