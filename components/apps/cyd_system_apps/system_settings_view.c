#include <stdio.h>
#include <string.h>
#include "cyd_ui.h"
#include "system_settings_view.h"

/*
 * Layout on the 8px grid. Pages fill CYD_UI_SETTINGS_CONTENT_FIRST_ROW (4) to
 * CYD_UI_SETTINGS_CONTENT_LAST_ROW (26); the chrome owns the rest.
 *
 * A stepper row is 4 rows (32px) high:
 *   cols 1-12 label | 13-17 [−] | 18-32 value | 33-37 [+]
 * The label column holds six 16px kanji, the value column "29分50秒" in 24px.
 * The buttons are 40x32px.
 */
#define VIEW_APP_TITLE "設定"
#define VIEW_ROW_ROWS 4
#define VIEW_LABEL_COL 1
#define VIEW_LABEL_COLS 12
#define VIEW_MINUS_COL 13
#define VIEW_BUTTON_COLS 5
#define VIEW_VALUE_COL 18
#define VIEW_VALUE_COLS 15
#define VIEW_PLUS_COL 33
/* Full-width action buttons and text lines. */
#define VIEW_WIDE_COL 2
#define VIEW_WIDE_COLS 36
#define VIEW_TEXT_ROWS 3
/* Confirm dialogs: [やめる] on the left, the action on the right, far apart. */
#define VIEW_CONFIRM_ROW 21
#define VIEW_CONFIRM_ROWS 5
#define VIEW_CONFIRM_CANCEL_COL 1
#define VIEW_CONFIRM_ACTION_COL 21
#define VIEW_CONFIRM_COLS 18

typedef struct {
    const char *label;
    const char *tz;
} view_timezone_t;

static const view_timezone_t k_timezones[] = {
    { "UTC", "UTC0" },
    { "日本", "JST-9" },
    { "韓国", "KST-9" },
    { "中国", "CHN-8" },
    { "台湾", "TWN-8" },
    { "インド", "IST-5:30" },
    { "タイ", "ICT-7" },
    { "シンガポール", "SGT-8" },
    { "UAE", "GST-4" },
    { "ドイツ", "DET-1DEST,M3.5.0/2,M10.5.0/3" },
    { "フランス", "FRT-1FRST,M3.5.0/2,M10.5.0/3" },
    { "イギリス", "GMT0BST,M3.5.0/1,M10.5.0/2" },
    { "ブラジル", "BRT3" },
    { "米国東部", "EST5EDT,M3.2.0/2,M11.1.0/2" },
    { "米国中部", "CST6CDT,M3.2.0/2,M11.1.0/2" },
    { "米国山岳部", "MST7MDT,M3.2.0/2,M11.1.0/2" },
    { "米国西部", "PST8PDT,M3.2.0/2,M11.1.0/2" },
    { "豪州東部", "AEST-10AEDT,M10.1.0/2,M4.1.0/3" },
    { "ニュージーランド", "NZST-12NZDT,M9.5.0/2,M4.1.0/3" },
};

#define VIEW_TIMEZONE_COUNT (sizeof(k_timezones) / sizeof(k_timezones[0]))

size_t system_settings_view_timezone_count(void)
{
    return VIEW_TIMEZONE_COUNT;
}

const char *system_settings_view_timezone_label(size_t index)
{
    return index < VIEW_TIMEZONE_COUNT ? k_timezones[index].label : "";
}

const char *system_settings_view_timezone_tz(size_t index)
{
    return index < VIEW_TIMEZONE_COUNT ? k_timezones[index].tz : "";
}

/* ---- wording --------------------------------------------------------------- */

static const char *view_text(const char *text)
{
    return text != NULL ? text : "";
}

/* 0 = "しない", else seconds as 秒 / 分 / 分秒. */
static void view_format_seconds(char *out, size_t out_size, uint16_t seconds)
{
    if (seconds == 0) {
        snprintf(out, out_size, "しない");
    } else if (seconds < 60U) {
        snprintf(out, out_size, "%u秒", (unsigned)seconds);
    } else if (seconds % 60U == 0U) {
        snprintf(out, out_size, "%u分", (unsigned)(seconds / 60U));
    } else {
        snprintf(out, out_size, "%u分%u秒", (unsigned)(seconds / 60U), (unsigned)(seconds % 60U));
    }
}

/* Whole hours from two hours up ("9時間"), otherwise minutes ("90分",
   "130分"): "2時間10分" is too wide for the value column in 24px. */
static void view_format_minutes(char *out, size_t out_size, uint16_t minutes)
{
    if (minutes >= 120U && minutes % 60U == 0U) {
        snprintf(out, out_size, "%u時間", (unsigned)(minutes / 60U));
    } else {
        snprintf(out, out_size, "%u分", (unsigned)minutes);
    }
}

const char *system_settings_view_wifi_text(system_settings_view_wifi_t wifi)
{
    switch (wifi) {
    case SYSTEM_SETTINGS_VIEW_WIFI_STOPPED: return "停止中";
    case SYSTEM_SETTINGS_VIEW_WIFI_INIT: return "準備中";
    case SYSTEM_SETTINGS_VIEW_WIFI_OFF: return "オフ";
    case SYSTEM_SETTINGS_VIEW_WIFI_CONNECTING: return "接続中…";
    case SYSTEM_SETTINGS_VIEW_WIFI_CONNECTED: return "接続済み";
    case SYSTEM_SETTINGS_VIEW_WIFI_RECONNECTING: return "再接続中…";
    case SYSTEM_SETTINGS_VIEW_WIFI_FAILED: return "接続できません";
    case SYSTEM_SETTINGS_VIEW_WIFI_SETUP_REQUIRED: return "未設定";
    case SYSTEM_SETTINGS_VIEW_WIFI_SETUP_RUNNING: return "設定中";
    default: return "状態不明";
    }
}

uint16_t system_settings_view_wifi_color(system_settings_view_wifi_t wifi)
{
    switch (wifi) {
    case SYSTEM_SETTINGS_VIEW_WIFI_CONNECTED: return CYD_UI_THEME_SUCCESS_SOFT;
    case SYSTEM_SETTINGS_VIEW_WIFI_FAILED: return CYD_UI_THEME_DANGER;
    case SYSTEM_SETTINGS_VIEW_WIFI_SETUP_REQUIRED: return CYD_UI_THEME_WARNING;
    default: return CYD_UI_THEME_TEXT;
    }
}

const char *system_settings_view_sync_text(system_settings_view_sync_t sync)
{
    switch (sync) {
    case SYSTEM_SETTINGS_VIEW_SYNC_IDLE: return "待機中";
    case SYSTEM_SETTINGS_VIEW_SYNC_WAITING_WIFI: return "Wi-Fi 待ち";
    case SYSTEM_SETTINGS_VIEW_SYNC_SYNCING: return "同期中…";
    case SYSTEM_SETTINGS_VIEW_SYNC_RETRY_WAIT: return "再試行待ち";
    default: return "停止中";
    }
}

void system_settings_view_format_sync_last(char *out, size_t out_size, system_settings_view_sync_last_t last,
                                           const struct tm *at, bool with_prefix)
{
    const char *prefix = with_prefix ? "前回: " : "";

    switch (last) {
    case SYSTEM_SETTINGS_VIEW_SYNC_LAST_FAILED:
        snprintf(out, out_size, "%s失敗", prefix);
        break;
    case SYSTEM_SETTINGS_VIEW_SYNC_LAST_OK:
        snprintf(out, out_size, "%s成功", prefix);
        break;
    case SYSTEM_SETTINGS_VIEW_SYNC_LAST_OK_AT:
        snprintf(out, out_size, "%s%d/%d %02d:%02d 成功", prefix, at->tm_mon + 1, at->tm_mday, at->tm_hour,
                 at->tm_min);
        break;
    default:
        snprintf(out, out_size, "%sまだありません", prefix);
        break;
    }
}

/* ---- building blocks ------------------------------------------------------- */

static void view_begin(cyd_display_screen_t *screen)
{
    cyd_ui_screen_clear(screen);
    cyd_ui_add_panel(screen, 0, 0, CYD_DISPLAY_GRID_COLS, CYD_DISPLAY_GRID_ROWS, CYD_UI_THEME_BG, 0, 0, 0);
    cyd_ui_add_settings_title(screen, VIEW_APP_TITLE);
}

static void view_stepper_at(cyd_display_screen_t *screen,
                            const char *label,
                            const char *value,
                            uint8_t row,
                            uint8_t label_cols,
                            uint8_t minus_col,
                            uint8_t value_col,
                            uint8_t value_cols,
                            uint8_t plus_col,
                            uint16_t down_action,
                            uint16_t up_action,
                            bool can_down,
                            bool can_up)
{
    const cyd_ui_stepper_row_t r = {
        .label_text = label,
        .value_text = value,
        .row = row,
        .label_col = VIEW_LABEL_COL,
        .label_span_cols = label_cols,
        .value_col = value_col,
        .value_span_cols = value_cols,
        .button_left_col = minus_col,
        .button_right_col = plus_col,
        .button_span_cols = VIEW_BUTTON_COLS,
        .button_span_rows = VIEW_ROW_ROWS,
        .decrease_action_id = down_action,
        .increase_action_id = up_action,
        .can_decrease = can_down,
        .can_increase = can_up,
    };
    (void)cyd_ui_add_stepper_row(screen, &r);
}

static void view_stepper(cyd_display_screen_t *screen,
                         const char *label,
                         const char *value,
                         uint8_t row,
                         uint16_t down_action,
                         uint16_t up_action,
                         bool can_down,
                         bool can_up)
{
    view_stepper_at(screen, label, value, row, VIEW_LABEL_COLS, VIEW_MINUS_COL, VIEW_VALUE_COL, VIEW_VALUE_COLS,
                    VIEW_PLUS_COL, down_action, up_action, can_down, can_up);
}

/* An outlined full-width button: the ordinary "go somewhere" action. */
static void view_wide_button(cyd_display_screen_t *screen, const char *text, uint8_t row, uint8_t rows,
                             uint16_t color, uint16_t tint, uint16_t action_id, bool enabled)
{
    cyd_ui_add_styled_button(screen, text, VIEW_WIDE_COL, row, VIEW_WIDE_COLS, rows, CYD_DISPLAY_FONT_BODY_BOLD,
                             color, tint, color, CYD_UI_THEME_BORDER_PX, action_id, enabled);
}

static void view_line(cyd_display_screen_t *screen, const char *text, uint8_t row, cyd_display_font_t font,
                      uint16_t color)
{
    cyd_ui_add_label(screen, text, VIEW_WIDE_COL, row, VIEW_WIDE_COLS, VIEW_TEXT_ROWS,
                     CYD_DISPLAY_ALIGN_LEFT, font, color);
}

static void view_page_nav(cyd_display_screen_t *screen, const system_settings_view_model_t *m)
{
    if (m->page_count > 0 && m->page_index < m->page_count) {
        (void)cyd_ui_add_settings_page_nav(screen, view_text(m->page_title), m->page_index, m->page_count,
                                           CYD_SETTINGS_APP_ACTION_PREV_PAGE, CYD_SETTINGS_APP_ACTION_NEXT_PAGE);
    }
    cyd_ui_add_settings_back(screen, CYD_SETTINGS_APP_ACTION_BACK);
}

/*
 * A confirm dialog: the question in 24px (12 characters at most), up to two
 * lines of consequences, and
 * [やめる] / [action] at opposite edges so a slip cannot hit the wrong one.
 * The destructive button is the only solid red element on the screen.
 */
static void view_confirm(cyd_display_screen_t *screen,
                         const char *question,
                         const char *detail1,
                         uint16_t detail1_color,
                         const char *detail2,
                         const char *action_label,
                         uint16_t cancel_action,
                         uint16_t confirm_action)
{
    cyd_ui_add_label(screen, question, VIEW_WIDE_COL, 6, VIEW_WIDE_COLS, 4,
                     CYD_DISPLAY_ALIGN_LEFT, CYD_DISPLAY_FONT_TITLE, CYD_UI_THEME_TEXT);
    if (detail1 != NULL) {
        view_line(screen, detail1, 11, CYD_DISPLAY_FONT_BODY, detail1_color);
    }
    if (detail2 != NULL) {
        view_line(screen, detail2, 14, CYD_DISPLAY_FONT_BODY, CYD_UI_THEME_WARNING);
    }
    if (cancel_action != 0) {
        cyd_ui_add_styled_button(screen, "やめる", VIEW_CONFIRM_CANCEL_COL, VIEW_CONFIRM_ROW, VIEW_CONFIRM_COLS,
                                 VIEW_CONFIRM_ROWS, CYD_DISPLAY_FONT_TITLE, CYD_UI_THEME_PRIMARY_SOFT,
                                 CYD_UI_THEME_SURFACE, CYD_UI_THEME_PRIMARY, CYD_UI_THEME_BORDER_PX,
                                 cancel_action, true);
    }
    cyd_ui_add_styled_button(screen, action_label, VIEW_CONFIRM_ACTION_COL, VIEW_CONFIRM_ROW, VIEW_CONFIRM_COLS,
                             VIEW_CONFIRM_ROWS, CYD_DISPLAY_FONT_TITLE, CYD_UI_THEME_ON_DANGER,
                             CYD_UI_THEME_DANGER, CYD_UI_THEME_DANGER, 0, confirm_action, true);
}

/* ---- pages ----------------------------------------------------------------- */

static void view_general(cyd_display_screen_t *screen, const system_settings_view_model_t *m)
{
    char brightness[16];
    char idle_return[24];

    snprintf(brightness, sizeof(brightness), "%u%%", (unsigned)m->brightness_percent);
    view_format_seconds(idle_return, sizeof(idle_return), m->idle_return_seconds);

    view_stepper(screen, "画面の明るさ", brightness, 5, CYD_SETTINGS_APP_ACTION_BRIGHTNESS_DOWN,
                 CYD_SETTINGS_APP_ACTION_BRIGHTNESS_UP, m->can_dim, m->can_brighten);
    view_stepper(screen, "無操作で戻る", idle_return, 10, CYD_SETTINGS_APP_ACTION_IDLE_RETURN_DOWN,
                 CYD_SETTINGS_APP_ACTION_IDLE_RETURN_UP, m->can_idle_return_down, m->can_idle_return_up);
    view_line(screen, "操作がないとメイン画面に戻ります", 14, CYD_DISPLAY_FONT_BODY, CYD_UI_THEME_SUBTEXT);
    view_wide_button(screen, "タッチ位置の補正", 19, 5, CYD_UI_THEME_PRIMARY_SOFT, CYD_UI_THEME_SURFACE,
                     CYD_SETTINGS_APP_ACTION_TOUCH_CALIBRATE, true);
}

static void view_time(cyd_display_screen_t *screen, const system_settings_view_model_t *m)
{
    static const char *const weekdays[] = { "日", "月", "火", "水", "木", "金", "土" };
    const struct tm *t = &m->local_time;
    char clock[16];
    char date[48];

    snprintf(clock, sizeof(clock), "%02d:%02d:%02d", t->tm_hour, t->tm_min, t->tm_sec);
    snprintf(date, sizeof(date), "%d年%d月%d日（%s）", t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
             weekdays[(unsigned)t->tm_wday % 7U]);

    cyd_ui_add_label(screen, clock, VIEW_WIDE_COL, 4, VIEW_WIDE_COLS, 6,
                     CYD_DISPLAY_ALIGN_LEFT, CYD_DISPLAY_FONT_CLOCK_MEDIUM, CYD_UI_THEME_TEXT);
    view_line(screen, date, 10, CYD_DISPLAY_FONT_BODY_BOLD, CYD_UI_THEME_TEXT);
    if (m->clock_set) {
        view_line(screen, "時計は設定済みです", 13, CYD_DISPLAY_FONT_BODY, CYD_UI_THEME_SUBTEXT);
    } else {
        view_line(screen, "時計がまだ合っていません", 13, CYD_DISPLAY_FONT_BODY, CYD_UI_THEME_WARNING);
    }
    /* Country names run long ("ニュージーランド"), so the label sits on its own
       line and the value gets the whole width between the buttons. */
    view_line(screen, "タイムゾーン", 16, CYD_DISPLAY_FONT_BODY_BOLD, CYD_UI_THEME_SUBTEXT);
    view_stepper_at(screen, "", view_text(m->timezone_label), 19, 0, VIEW_LABEL_COL, VIEW_LABEL_COL + 5, 26,
                    VIEW_LABEL_COL + 31, CYD_SETTINGS_APP_ACTION_TIMEZONE_DOWN, CYD_SETTINGS_APP_ACTION_TIMEZONE_UP,
                    m->can_timezone_down, m->can_timezone_up);
}

static void view_network1(cyd_display_screen_t *screen, const system_settings_view_model_t *m)
{
    cyd_ui_add_label(screen, "Wi-Fi", VIEW_WIDE_COL, 5, 7, VIEW_TEXT_ROWS,
                     CYD_DISPLAY_ALIGN_LEFT, CYD_DISPLAY_FONT_BODY_BOLD, CYD_UI_THEME_SUBTEXT);
    cyd_ui_add_label(screen, system_settings_view_wifi_text(m->wifi), 9, 4, 29, 4,
                     CYD_DISPLAY_ALIGN_LEFT, CYD_DISPLAY_FONT_TITLE, system_settings_view_wifi_color(m->wifi));
    view_wide_button(screen, "保存済みのネットワーク", 10, 5, CYD_UI_THEME_PRIMARY_SOFT, CYD_UI_THEME_SURFACE,
                     CYD_SETTINGS_APP_ACTION_STORED_SSIDS, true);
    cyd_ui_add_styled_button(screen, "Wi-Fi を設定する", VIEW_WIDE_COL, 17, VIEW_WIDE_COLS, 5,
                             CYD_DISPLAY_FONT_BODY_BOLD, CYD_UI_THEME_ON_PRIMARY, CYD_UI_THEME_PRIMARY,
                             CYD_UI_THEME_PRIMARY, 0, CYD_SETTINGS_APP_ACTION_WIFI, true);
}

static void view_network2(cyd_display_screen_t *screen, const system_settings_view_model_t *m)
{
    char interval[24];
    char idle[24];
    char state[40];
    char last[40];

    view_format_minutes(interval, sizeof(interval), m->sync_interval_minutes);
    view_format_seconds(idle, sizeof(idle), m->wifi_idle_seconds);
    snprintf(state, sizeof(state), "時刻同期: %s", system_settings_view_sync_text(m->sync));
    system_settings_view_format_sync_last(last, sizeof(last), m->sync_last, &m->last_sync_at, true);

    view_stepper(screen, "同期の間隔", interval, 4, CYD_SETTINGS_APP_ACTION_TIME_SYNC_DOWN,
                 CYD_SETTINGS_APP_ACTION_TIME_SYNC_UP, m->can_sync_interval_down, m->can_sync_interval_up);
    view_stepper(screen, "Wi-Fi切断", idle, 9, CYD_SETTINGS_APP_ACTION_WIFI_IDLE_DOWN,
                 CYD_SETTINGS_APP_ACTION_WIFI_IDLE_UP, m->can_wifi_idle_down, m->can_wifi_idle_up);
    view_wide_button(screen, "今すぐ時刻を合わせる", 14, 5, CYD_UI_THEME_PRIMARY_SOFT, CYD_UI_THEME_SURFACE,
                     CYD_SETTINGS_APP_ACTION_SYNC_NOW, m->sync_now_enabled);
    view_line(screen, state, 20, CYD_DISPLAY_FONT_BODY, CYD_UI_THEME_TEXT);
    view_line(screen, last, 23,
              CYD_DISPLAY_FONT_BODY,
              m->sync_last == SYSTEM_SETTINGS_VIEW_SYNC_LAST_FAILED ? CYD_UI_THEME_DANGER : CYD_UI_THEME_SUBTEXT);
}

/*
 * Three actions of increasing blast radius, ordered that way on purpose. Only
 * the last erases everything, so only it is red.
 */
static void view_nvs(cyd_display_screen_t *screen)
{
    static const struct {
        const char *label;
        const char *detail;
        uint8_t row;
        bool danger;
        uint16_t action_id;
    } actions[] = {
        { "タッチ補正を消去", "保存したタッチ補正だけを消します", 4, false,
          CYD_SETTINGS_APP_ACTION_CLEAR_TOUCH_CALIB },
        { "アプリのデータを消去", "アプリが保存したデータだけを消します", 11, false,
          CYD_SETTINGS_APP_ACTION_CLEAR_APP_DATA },
        { "すべて初期化", "Wi-Fi・設定・補正をすべて消します", 18, true,
          CYD_SETTINGS_APP_ACTION_CLEAR_NVS },
    };

    for (size_t i = 0; i < sizeof(actions) / sizeof(actions[0]); ++i) {
        view_wide_button(screen, actions[i].label, actions[i].row, 4,
                         actions[i].danger ? CYD_UI_THEME_DANGER : CYD_UI_THEME_WARNING,
                         actions[i].danger ? CYD_UI_THEME_DANGER_TINT : CYD_UI_THEME_WARNING_TINT,
                         actions[i].action_id, true);
        cyd_ui_add_label(screen, actions[i].detail, VIEW_WIDE_COL, (uint8_t)(actions[i].row + 4), VIEW_WIDE_COLS, 2,
                         CYD_DISPLAY_ALIGN_LEFT, CYD_DISPLAY_FONT_BODY, CYD_UI_THEME_SUBTEXT);
    }
}

static void view_apps(cyd_display_screen_t *screen, const system_settings_view_model_t *m)
{
    size_t count = m->app_count < SYSTEM_SETTINGS_VIEW_APPS_MAX ? m->app_count : SYSTEM_SETTINGS_VIEW_APPS_MAX;

    for (size_t i = 0; i < count; ++i) {
        view_wide_button(screen, view_text(m->apps[i]), (uint8_t)(4 + i * 5), 4, CYD_UI_THEME_PRIMARY_SOFT,
                         CYD_UI_THEME_SURFACE, (uint16_t)(CYD_SETTINGS_APP_ACTION_APP_BASE + i), true);
    }
}

static void view_stored_ssids(cyd_display_screen_t *screen, const system_settings_view_model_t *m)
{
    size_t count = m->ssid_count < SYSTEM_SETTINGS_VIEW_PROFILES_MAX ? m->ssid_count
                                                                     : SYSTEM_SETTINGS_VIEW_PROFILES_MAX;

    if (count == 0) {
        view_line(screen, "保存済みのネットワークはありません", 8, CYD_DISPLAY_FONT_BODY, CYD_UI_THEME_SUBTEXT);
    }
    /* Five 24px rows; the selected one is filled, so it reads at a glance. */
    for (size_t i = 0; i < count; ++i) {
        const bool selected = i == m->selected_ssid;
        char line[CYD_DISPLAY_TEXT_MAX_LEN + 1];
        snprintf(line, sizeof(line), "%u. %s", (unsigned)(i + 1U), view_text(m->ssids[i]));
        cyd_ui_add_styled_button(screen, line, VIEW_WIDE_COL, (uint8_t)(4 + i * 3), VIEW_WIDE_COLS, 3,
                                 CYD_DISPLAY_FONT_BODY,
                                 selected ? CYD_UI_THEME_ON_PRIMARY : CYD_UI_THEME_TEXT,
                                 selected ? CYD_UI_THEME_PRIMARY : CYD_UI_THEME_SURFACE,
                                 selected ? CYD_UI_THEME_PRIMARY : CYD_UI_THEME_LINE, 1,
                                 (uint16_t)(CYD_SETTINGS_APP_ACTION_STORED_SELECT_BASE + i), true);
    }
    cyd_ui_add_label(screen, "一番上のものから順に接続します", VIEW_WIDE_COL, 19, VIEW_WIDE_COLS, 2,
                     CYD_DISPLAY_ALIGN_LEFT, CYD_DISPLAY_FONT_BODY, CYD_UI_THEME_SUBTEXT);
    cyd_ui_add_styled_button(screen, "一番上にする", 1, 22, 18, 5, CYD_DISPLAY_FONT_BODY_BOLD,
                             CYD_UI_THEME_PRIMARY_SOFT, CYD_UI_THEME_SURFACE, CYD_UI_THEME_PRIMARY,
                             CYD_UI_THEME_BORDER_PX, CYD_SETTINGS_APP_ACTION_STORED_PREFER, count > 0);
    cyd_ui_add_styled_button(screen, "削除", 21, 22, 18, 5, CYD_DISPLAY_FONT_BODY_BOLD,
                             CYD_UI_THEME_DANGER, CYD_UI_THEME_DANGER_TINT, CYD_UI_THEME_DANGER,
                             CYD_UI_THEME_BORDER_PX, CYD_SETTINGS_APP_ACTION_STORED_DELETE, count > 0);
    cyd_ui_add_settings_back(screen, CYD_SETTINGS_APP_ACTION_BACK);
}

static void view_clear_nvs(cyd_display_screen_t *screen, const system_settings_view_model_t *m)
{
    if (m->nvs_force_initialize) {
        /* No way out: the data cannot be read, so cancel would loop back here. */
        const char *problem = view_text(m->nvs_problem);
        view_confirm(screen, "保存データが読めません",
                     problem[0] != '\0' ? problem : "保存データの形式が違います", CYD_UI_THEME_SUBTEXT,
                     "初期化が必要です。再起動します", "初期化する", 0, CYD_SETTINGS_APP_ACTION_CLEAR_NVS_CONFIRM);
        return;
    }
    view_confirm(screen, "すべて初期化しますか？", "Wi-Fi・設定・補正をすべて消します", CYD_UI_THEME_TEXT,
                 "初期化のあと再起動します", "初期化する", CYD_SETTINGS_APP_ACTION_CLEAR_NVS_CANCEL,
                 CYD_SETTINGS_APP_ACTION_CLEAR_NVS_CONFIRM);
}

static void view_message(cyd_display_screen_t *screen, const system_settings_view_model_t *m)
{
    cyd_ui_add_panel(screen, 1, 7, 38, 14, CYD_UI_THEME_SURFACE, CYD_UI_THEME_LINE, 1, 8);
    cyd_ui_add_label(screen, view_text(m->message_title), VIEW_WIDE_COL, 9, VIEW_WIDE_COLS, 4,
                     CYD_DISPLAY_ALIGN_LEFT, CYD_DISPLAY_FONT_TITLE, CYD_UI_THEME_TEXT);
    cyd_ui_add_label(screen, view_text(m->message_detail), VIEW_WIDE_COL, 15, VIEW_WIDE_COLS, 3,
                     CYD_DISPLAY_ALIGN_LEFT, CYD_DISPLAY_FONT_BODY, CYD_UI_THEME_SUBTEXT);
}

void system_settings_view_build(cyd_display_screen_t *screen, const system_settings_view_model_t *m)
{
    if (screen == NULL || m == NULL) {
        return;
    }
    view_begin(screen);

    switch (m->screen) {
    case SYSTEM_SETTINGS_VIEW_GENERAL:
        view_general(screen, m);
        break;
    case SYSTEM_SETTINGS_VIEW_TIME:
        view_time(screen, m);
        break;
    case SYSTEM_SETTINGS_VIEW_NETWORK1:
        view_network1(screen, m);
        break;
    case SYSTEM_SETTINGS_VIEW_NETWORK2:
        view_network2(screen, m);
        break;
    case SYSTEM_SETTINGS_VIEW_NVS:
        view_nvs(screen);
        break;
    case SYSTEM_SETTINGS_VIEW_APPS:
        view_apps(screen, m);
        break;
    case SYSTEM_SETTINGS_VIEW_STORED_SSIDS:
        view_stored_ssids(screen, m);
        return;
    case SYSTEM_SETTINGS_VIEW_DELETE_SSID_CONFIRM: {
        const char *ssid = m->selected_ssid < m->ssid_count ? view_text(m->ssids[m->selected_ssid]) : "";
        view_confirm(screen, "削除しますか？", ssid, CYD_UI_THEME_TEXT, NULL, "削除する",
                     CYD_SETTINGS_APP_ACTION_STORED_CANCEL_DELETE, CYD_SETTINGS_APP_ACTION_STORED_CONFIRM_DELETE);
        return;
    }
    case SYSTEM_SETTINGS_VIEW_CLEAR_TOUCH_CALIB_CONFIRM:
        view_confirm(screen, "タッチ補正を消しますか？", "保存したタッチ補正だけを消します", CYD_UI_THEME_TEXT,
                     "消去のあと再起動します", "消去する", CYD_SETTINGS_APP_ACTION_CLEAR_TOUCH_CALIB_CANCEL,
                     CYD_SETTINGS_APP_ACTION_CLEAR_TOUCH_CALIB_CONFIRM);
        return;
    case SYSTEM_SETTINGS_VIEW_CLEAR_APP_DATA_CONFIRM:
        view_confirm(screen, "データを消しますか？", "アプリが保存したデータだけを消します", CYD_UI_THEME_TEXT,
                     "Wi-Fi・設定は残ります。再起動します", "消去する", CYD_SETTINGS_APP_ACTION_CLEAR_APP_DATA_CANCEL,
                     CYD_SETTINGS_APP_ACTION_CLEAR_APP_DATA_CONFIRM);
        return;
    case SYSTEM_SETTINGS_VIEW_CLEAR_NVS_CONFIRM:
        view_clear_nvs(screen, m);
        return;
    case SYSTEM_SETTINGS_VIEW_RESTART_CONFIRM:
        view_confirm(screen, "再起動しますか？", "設定を保存してから再起動します", CYD_UI_THEME_TEXT, NULL,
                     "再起動する", CYD_SETTINGS_APP_ACTION_RESTART_CANCEL, CYD_SETTINGS_APP_ACTION_RESTART_CONFIRM);
        return;
    case SYSTEM_SETTINGS_VIEW_MESSAGE:
        view_message(screen, m);
        return;
    default:
        return;
    }
    view_page_nav(screen, m);
}
