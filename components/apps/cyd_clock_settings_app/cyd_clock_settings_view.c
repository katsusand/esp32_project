#include <stdio.h>
#include "cyd_clock_settings_view.h"
#include "cyd_ui.h"

/* Stepper geometry shared with the system settings screens:
   cols 1-12 label | 14-18 [−] | 19-33 value | 34-38 [+], 32px high. */
#define VIEW_ROW_ROWS 4

static const char *const k_page_titles[CYD_CLOCK_SETTINGS_VIEW_PAGE_COUNT] = {
    "アラーム1",
    "アラーム2",
    "スケジュール",
};

static void view_stepper(cyd_display_screen_t *screen, const char *label, const char *value, uint8_t row,
                         uint16_t down_action, uint16_t up_action)
{
    const cyd_ui_stepper_row_t r = {
        .label_text = label,
        .value_text = value,
        .row = row,
        .label_col = 1,
        .label_span_cols = 12,
        .value_col = 19,
        .value_span_cols = 15,
        .button_left_col = 14,
        .button_right_col = 34,
        .button_span_cols = 5,
        .button_span_rows = VIEW_ROW_ROWS,
        .decrease_action_id = down_action,
        .increase_action_id = up_action,
        /* Hours and minutes wrap around, so neither end is a limit. */
        .can_decrease = true,
        .can_increase = true,
    };
    (void)cyd_ui_add_stepper_row(screen, &r);
}

static void view_alarm(cyd_display_screen_t *screen, const cyd_clock_settings_view_model_t *m, bool alarm1)
{
    static const char *const weekdays[] = { "日", "月", "火", "水", "木", "金", "土" };
    char hour[8];
    char minute[8];
    uint8_t row = 5;

    snprintf(hour, sizeof(hour), "%02u", (unsigned)m->hour);
    snprintf(minute, sizeof(minute), "%02u", (unsigned)m->minute);

    if (alarm1) {
        /* Seven 40x32px toggles; a lit day is filled. */
        for (size_t i = 0; i < 7; ++i) {
            const bool on = (m->weekday_mask & (1U << i)) != 0;
            cyd_ui_add_styled_button(screen, weekdays[i], (uint8_t)(2 + i * 5), 5, 5, VIEW_ROW_ROWS,
                                     CYD_DISPLAY_FONT_BODY_BOLD,
                                     on ? CYD_UI_THEME_ON_PRIMARY : CYD_UI_THEME_SUBTEXT,
                                     on ? CYD_UI_THEME_PRIMARY : CYD_UI_THEME_SURFACE,
                                     on ? CYD_UI_THEME_PRIMARY : CYD_UI_THEME_LINE, 1,
                                     (uint16_t)(CYD_CLOCK_SETTINGS_ACTION_ALARM1_WEEKDAY_BASE + i), true);
        }
        cyd_ui_add_label(screen, "色の付いた曜日に鳴ります", 2, 9, 36, 3, CYD_DISPLAY_ALIGN_LEFT,
                         CYD_DISPLAY_FONT_BODY, CYD_UI_THEME_SUBTEXT);
        row = 13;
    }
    view_stepper(screen, "時", hour, row,
                 alarm1 ? CYD_CLOCK_SETTINGS_ACTION_ALARM1_HOUR_DOWN : CYD_CLOCK_SETTINGS_ACTION_ALARM2_HOUR_DOWN,
                 alarm1 ? CYD_CLOCK_SETTINGS_ACTION_ALARM1_HOUR_UP : CYD_CLOCK_SETTINGS_ACTION_ALARM2_HOUR_UP);
    view_stepper(screen, "分", minute, (uint8_t)(row + 5),
                 alarm1 ? CYD_CLOCK_SETTINGS_ACTION_ALARM1_MINUTE_DOWN : CYD_CLOCK_SETTINGS_ACTION_ALARM2_MINUTE_DOWN,
                 alarm1 ? CYD_CLOCK_SETTINGS_ACTION_ALARM1_MINUTE_UP : CYD_CLOCK_SETTINGS_ACTION_ALARM2_MINUTE_UP);
}

static const char *view_state_text(cyd_clock_settings_view_sched_state_t state)
{
    switch (state) {
    case CYD_CLOCK_SETTINGS_VIEW_SCHED_DISABLED: return "無効";
    case CYD_CLOCK_SETTINGS_VIEW_SCHED_WAITING: return "待機中";
    case CYD_CLOCK_SETTINGS_VIEW_SCHED_ACTIVE: return "動作中";
    case CYD_CLOCK_SETTINGS_VIEW_SCHED_STOPPED: return "停止";
    default: return "不明";
    }
}

/*
 * Two 16px lines per schedule. The behavior and scope words are app_scheduler
 * terms and stay in English:
 *   1 clock/alarm1                    待機中
 *     07:30  edge / app
 */
static void view_scheduler(cyd_display_screen_t *screen, const cyd_clock_settings_view_model_t *m)
{
    char summary[40];
    size_t count = m->schedule_count < CYD_CLOCK_SETTINGS_VIEW_SCHEDULES_MAX ? m->schedule_count
                                                                               : CYD_CLOCK_SETTINGS_VIEW_SCHEDULES_MAX;

    if (m->schedules_unavailable) {
        cyd_ui_add_label(screen, "スケジューラーを読めません", 1, 4, 38, 2, CYD_DISPLAY_ALIGN_LEFT,
                         CYD_DISPLAY_FONT_BODY_BOLD, CYD_UI_THEME_DANGER);
        return;
    }
    snprintf(summary, sizeof(summary), "登録 %u / %u 件", (unsigned)m->schedule_count,
             (unsigned)m->schedule_capacity);
    cyd_ui_add_label(screen, summary, 1, 4, 38, 2, CYD_DISPLAY_ALIGN_LEFT, CYD_DISPLAY_FONT_BODY_BOLD,
                     CYD_UI_THEME_TEXT);
    if (count == 0) {
        cyd_ui_add_label(screen, "スケジュールはありません", 1, 8, 38, 2, CYD_DISPLAY_ALIGN_LEFT,
                         CYD_DISPLAY_FONT_BODY, CYD_UI_THEME_SUBTEXT);
    }

    for (size_t i = 0; i < count; ++i) {
        const cyd_clock_settings_view_schedule_t *s = &m->schedules[i];
        const uint8_t row = (uint8_t)(6 + i * 4);
        char name[40];
        char detail[48];

        snprintf(name, sizeof(name), "%u %.7s/%.8s", s->slot, s->owner != NULL ? s->owner : "",
                 s->tag != NULL ? s->tag : "");
        if (s->window) {
            snprintf(detail, sizeof(detail), "%02u:%02u-%02u:%02u  %s / %s", s->at_hour, s->at_minute, s->to_hour,
                     s->to_minute, s->latched ? "latched" : "edge", s->app_scope ? "app" : "feature");
        } else {
            snprintf(detail, sizeof(detail), "%02u:%02u  %s / %s", s->at_hour, s->at_minute,
                     s->latched ? "latched" : "edge", s->app_scope ? "app" : "feature");
        }
        cyd_ui_add_label(screen, name, 1, row, 28, 2, CYD_DISPLAY_ALIGN_LEFT, CYD_DISPLAY_FONT_BODY_BOLD,
                         CYD_UI_THEME_TEXT);
        cyd_ui_add_label(screen, view_state_text(s->state), 29, row, 9, 2, CYD_DISPLAY_ALIGN_RIGHT,
                         CYD_DISPLAY_FONT_BODY,
                         s->state == CYD_CLOCK_SETTINGS_VIEW_SCHED_ACTIVE ? CYD_UI_THEME_SUCCESS_SOFT
                                                                          : CYD_UI_THEME_SUBTEXT);
        cyd_ui_add_label(screen, detail, 3, (uint8_t)(row + 2), 35, 2, CYD_DISPLAY_ALIGN_LEFT, CYD_DISPLAY_FONT_BODY,
                         CYD_UI_THEME_SUBTEXT);
    }
}

void cyd_clock_settings_view_build(cyd_display_screen_t *screen, const cyd_clock_settings_view_model_t *m)
{
    if (screen == NULL || m == NULL) {
        return;
    }
    const size_t page = m->page < CYD_CLOCK_SETTINGS_VIEW_PAGE_COUNT ? (size_t)m->page : 0U;
    const cyd_ui_settings_chrome_t chrome = {
        .app_title = "時計の設定",
        .page_title = k_page_titles[page],
        .page_index = page,
        .page_count = CYD_CLOCK_SETTINGS_VIEW_PAGE_COUNT,
        .back_action_id = CYD_CLOCK_SETTINGS_ACTION_BACK,
        .prev_page_action_id = CYD_CLOCK_SETTINGS_ACTION_PREV_PAGE,
        .next_page_action_id = CYD_CLOCK_SETTINGS_ACTION_NEXT_PAGE,
    };

    cyd_ui_screen_clear(screen);
    cyd_ui_add_panel(screen, 0, 0, CYD_DISPLAY_GRID_COLS, CYD_DISPLAY_GRID_ROWS, CYD_UI_THEME_BG, 0, 0, 0);
    (void)cyd_ui_add_settings_chrome(screen, &chrome);

    switch ((cyd_clock_settings_view_page_t)page) {
    case CYD_CLOCK_SETTINGS_VIEW_ALARM1:
        view_alarm(screen, m, true);
        break;
    case CYD_CLOCK_SETTINGS_VIEW_ALARM2:
        view_alarm(screen, m, false);
        break;
    default:
        view_scheduler(screen, m);
        break;
    }
}
