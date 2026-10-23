#include <stdio.h>
#include <string.h>
#include "cyd_ui.h"
#include "settings_pin_logic.h"
#include "settings_pin_view.h"

/*
 * Layout (8px grid rows):
 *
 *   0-3    [戻る] title, or [戻る] [recovery button]
 *   4-6    what to do now, or what went wrong
 *   7-9    one dot per digit typed
 *   10-13  [ 1 ] [ 2 ] [ 3 ]
 *   15-18  [ 4 ] [ 5 ] [ 6 ]
 *   20-23  [ 7 ] [ 8 ] [ 9 ]
 *   25-28  [削除] [ 0 ] [決定]
 *
 * Keys are 96x32px with 8px between them, larger than any other key on the
 * device, so a resistive panel pressed with a fingertip lands on one key.
 * Text stays 16px like the other settings screens; the size is in the keys.
 */
#define VIEW_HEADER_ROWS 4
#define VIEW_SIDE_BUTTON_COLS 8
#define VIEW_PROMPT_ROW 4
#define VIEW_PROMPT_ROWS 3
#define VIEW_DOTS_ROW 7
#define VIEW_DOTS_ROWS 3
#define VIEW_WIDE_COL 1
#define VIEW_WIDE_COLS 38
#define VIEW_KEY_FIRST_ROW 10
#define VIEW_KEY_ROWS 4
#define VIEW_KEY_ROW_STEP 5
#define VIEW_KEY_FIRST_COL 1
#define VIEW_KEY_COLS 12
#define VIEW_KEY_COL_STEP 13

static const char *view_text(const char *text)
{
    return text != NULL ? text : "";
}

void settings_pin_view_format_wait(char *out, size_t out_size, uint32_t seconds)
{
    if (seconds >= 60U) {
        if (seconds % 60U == 0U) {
            snprintf(out, out_size, "あと%u分お待ちください", (unsigned)(seconds / 60U));
        } else {
            snprintf(out, out_size, "あと%u分%u秒お待ちください", (unsigned)(seconds / 60U),
                     (unsigned)(seconds % 60U));
        }
    } else {
        snprintf(out, out_size, "あと%u秒お待ちください", (unsigned)seconds);
    }
}

static void view_header(cyd_display_screen_t *screen, const settings_pin_view_model_t *m)
{
    cyd_ui_add_panel(screen, 0, 0, CYD_DISPLAY_GRID_COLS, VIEW_HEADER_ROWS, CYD_UI_THEME_SURFACE, 0, 0, 0);
    cyd_ui_add_styled_button(screen, "戻る", 0, 0, VIEW_SIDE_BUTTON_COLS, VIEW_HEADER_ROWS,
                             CYD_DISPLAY_FONT_BODY_BOLD, CYD_UI_THEME_PRIMARY_SOFT, CYD_UI_THEME_SURFACE,
                             CYD_UI_THEME_PRIMARY, CYD_UI_THEME_BORDER_PX, SETTINGS_PIN_VIEW_ACTION_CANCEL, true);

    /* The recovery button takes the title's place: the prompt below already
       says what this screen is, and the top edge is far from the keys. */
    if (m->screen == SETTINGS_PIN_VIEW_ENTER && m->recovery_label != NULL && m->recovery_label[0] != '\0') {
        cyd_ui_add_styled_button(screen, m->recovery_label, VIEW_SIDE_BUTTON_COLS + 1, 0,
                                 CYD_DISPLAY_GRID_COLS - VIEW_SIDE_BUTTON_COLS - 1, VIEW_HEADER_ROWS,
                                 CYD_DISPLAY_FONT_BODY_BOLD, CYD_UI_THEME_PRIMARY_SOFT, CYD_UI_THEME_SURFACE,
                                 CYD_UI_THEME_PRIMARY, CYD_UI_THEME_BORDER_PX, SETTINGS_PIN_VIEW_ACTION_RECOVERY,
                                 true);
        return;
    }
    cyd_ui_add_label(screen, view_text(m->title), VIEW_SIDE_BUTTON_COLS + 1, 0,
                     CYD_DISPLAY_GRID_COLS - VIEW_SIDE_BUTTON_COLS - 2, VIEW_HEADER_ROWS,
                     CYD_DISPLAY_ALIGN_LEFT, CYD_DISPLAY_FONT_BODY_BOLD, CYD_UI_THEME_TEXT);
}

static void view_prompt(cyd_display_screen_t *screen, const settings_pin_view_model_t *m)
{
    static char wait[48];
    const char *text = "";
    uint16_t color = CYD_UI_THEME_TEXT;

    switch (m->screen) {
    case SETTINGS_PIN_VIEW_ENTER:
        if (m->wait_seconds > 0) {
            settings_pin_view_format_wait(wait, sizeof(wait), m->wait_seconds);
            text = wait;
            color = CYD_UI_THEME_WARNING;
        } else if (m->notice == SETTINGS_PIN_VIEW_NOTICE_WRONG) {
            text = "PIN が違います";
            color = CYD_UI_THEME_DANGER_SOFT;
        } else {
            text = "PIN を入力してください";
        }
        break;
    case SETTINGS_PIN_VIEW_NEW_FIRST:
        if (m->notice == SETTINGS_PIN_VIEW_NOTICE_MISMATCH) {
            text = "一致しませんでした。最初から入力";
            color = CYD_UI_THEME_DANGER_SOFT;
        } else if (m->notice == SETTINGS_PIN_VIEW_NOTICE_SAVE_FAILED) {
            text = "保存できませんでした";
            color = CYD_UI_THEME_DANGER_SOFT;
        } else {
            text = "新しい PIN を入力（6〜8桁）";
        }
        break;
    case SETTINGS_PIN_VIEW_NEW_SECOND:
        text = "確認のため、もう一度入力してください";
        break;
    default:
        break;
    }
    cyd_ui_add_label(screen, text, VIEW_WIDE_COL, VIEW_PROMPT_ROW, VIEW_WIDE_COLS, VIEW_PROMPT_ROWS,
                     CYD_DISPLAY_ALIGN_CENTER, CYD_DISPLAY_FONT_BODY_BOLD, color);
}

/* Dots only: the count is all anyone looking over a shoulder may learn. */
static void view_dots(cyd_display_screen_t *screen, const settings_pin_view_model_t *m)
{
    static char dots[SETTINGS_PIN_MAX_DIGITS * 4 + 1];
    size_t pos = 0;
    uint8_t count = m->entered < SETTINGS_PIN_MAX_DIGITS ? m->entered : SETTINGS_PIN_MAX_DIGITS;

    cyd_ui_add_panel(screen, VIEW_WIDE_COL, VIEW_DOTS_ROW, VIEW_WIDE_COLS, VIEW_DOTS_ROWS, CYD_UI_THEME_SURFACE,
                     CYD_UI_THEME_LINE, 1, 4);
    if (count == 0) {
        return;
    }
    for (uint8_t i = 0; i < count; ++i) {
        /* "●" is 3 bytes in UTF-8; a space between dots keeps them countable. */
        if (i > 0) {
            dots[pos++] = ' ';
        }
        memcpy(&dots[pos], "●", 3);
        pos += 3;
    }
    dots[pos] = '\0';
    cyd_ui_add_label(screen, dots, VIEW_WIDE_COL, VIEW_DOTS_ROW, VIEW_WIDE_COLS, VIEW_DOTS_ROWS,
                     CYD_DISPLAY_ALIGN_CENTER, CYD_DISPLAY_FONT_BODY_BOLD, CYD_UI_THEME_TEXT);
}

static void view_key(cyd_display_screen_t *screen, const char *text, size_t col, size_t row, uint16_t fg,
                     uint16_t bg, uint16_t border, uint8_t border_px, uint16_t action_id, bool enabled)
{
    cyd_ui_add_styled_button(screen, text, (uint8_t)(VIEW_KEY_FIRST_COL + col * VIEW_KEY_COL_STEP),
                             (uint8_t)(VIEW_KEY_FIRST_ROW + row * VIEW_KEY_ROW_STEP), VIEW_KEY_COLS, VIEW_KEY_ROWS,
                             CYD_DISPLAY_FONT_BODY_BOLD, fg, bg, border, border_px, action_id, enabled);
}

static void view_keys(cyd_display_screen_t *screen, const settings_pin_view_model_t *m)
{
    static const char *const digits[] = { "0", "1", "2", "3", "4", "5", "6", "7", "8", "9" };
    const bool waiting = m->screen == SETTINGS_PIN_VIEW_ENTER && m->wait_seconds > 0;
    const bool can_type = !waiting && m->entered < SETTINGS_PIN_MAX_DIGITS;

    for (size_t d = 1; d <= 9; ++d) {
        view_key(screen, digits[d], (d - 1) % 3, (d - 1) / 3, CYD_UI_THEME_TEXT, CYD_UI_THEME_SURFACE,
                 CYD_UI_THEME_LINE, 1, (uint16_t)(SETTINGS_PIN_VIEW_ACTION_DIGIT_BASE + d), can_type);
    }
    view_key(screen, "削除", 0, 3, CYD_UI_THEME_DANGER_SOFT, CYD_UI_THEME_DANGER_TINT, CYD_UI_THEME_DANGER_SOFT,
             CYD_UI_THEME_BORDER_PX, SETTINGS_PIN_VIEW_ACTION_DELETE, !waiting && m->entered > 0);
    view_key(screen, digits[0], 1, 3, CYD_UI_THEME_TEXT, CYD_UI_THEME_SURFACE, CYD_UI_THEME_LINE, 1,
             SETTINGS_PIN_VIEW_ACTION_DIGIT_BASE, can_type);
    view_key(screen, "決定", 2, 3, CYD_UI_THEME_ON_SUCCESS, CYD_UI_THEME_SUCCESS, CYD_UI_THEME_SUCCESS, 0,
             SETTINGS_PIN_VIEW_ACTION_OK, !waiting && m->entered >= SETTINGS_PIN_MIN_DIGITS);
}

static void view_message(cyd_display_screen_t *screen, const settings_pin_view_model_t *m)
{
    cyd_ui_add_panel(screen, 1, 7, 38, 14, CYD_UI_THEME_SURFACE, CYD_UI_THEME_LINE, 1, 8);
    cyd_ui_add_label(screen, view_text(m->message_title), 2, 10, 36, 3, CYD_DISPLAY_ALIGN_LEFT,
                     CYD_DISPLAY_FONT_BODY_BOLD, CYD_UI_THEME_TEXT);
    cyd_ui_add_label(screen, view_text(m->message_detail), 2, 15, 36, 3, CYD_DISPLAY_ALIGN_LEFT,
                     CYD_DISPLAY_FONT_BODY, CYD_UI_THEME_SUBTEXT);
}

void settings_pin_view_build(cyd_display_screen_t *screen, const settings_pin_view_model_t *m)
{
    if (screen == NULL || m == NULL) {
        return;
    }
    cyd_ui_screen_clear(screen);
    cyd_ui_add_panel(screen, 0, 0, CYD_DISPLAY_GRID_COLS, CYD_DISPLAY_GRID_ROWS, CYD_UI_THEME_BG, 0, 0, 0);
    if (m->screen == SETTINGS_PIN_VIEW_MESSAGE) {
        cyd_ui_add_panel(screen, 0, 0, CYD_DISPLAY_GRID_COLS, VIEW_HEADER_ROWS, CYD_UI_THEME_SURFACE, 0, 0, 0);
        cyd_ui_add_label(screen, view_text(m->title), VIEW_SIDE_BUTTON_COLS + 1, 0,
                         CYD_DISPLAY_GRID_COLS - VIEW_SIDE_BUTTON_COLS - 2, VIEW_HEADER_ROWS,
                         CYD_DISPLAY_ALIGN_LEFT, CYD_DISPLAY_FONT_BODY_BOLD, CYD_UI_THEME_TEXT);
        view_message(screen, m);
        return;
    }
    view_header(screen, m);
    view_prompt(screen, m);
    view_dots(screen, m);
    view_keys(screen, m);
}
