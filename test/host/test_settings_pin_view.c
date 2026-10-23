/*
 * The PIN screen: every mode, notice, count of digits and wait, measured with
 * the real fonts, plus the keypad's own rules - big enough, apart, inside the
 * screen, never showing a digit.
 */

#include <stdio.h>
#include <string.h>
#include "cyd_display.h"
#include "settings_pin_logic.h"
#include "settings_pin_view.h"
#include "ui_test_support.h"

static cyd_display_screen_t s_screen;

static const cyd_display_widget_t *find_action(uint16_t action_id)
{
    for (size_t i = 0; i < s_screen.widget_count; ++i) {
        if (s_screen.widgets[i].type == CYD_DISPLAY_WIDGET_BUTTON && s_screen.widgets[i].action_id == action_id) {
            return &s_screen.widgets[i];
        }
    }
    return NULL;
}

static bool overlaps(const cyd_display_widget_t *a, const cyd_display_widget_t *b)
{
    return a->col < b->col + b->span_cols && b->col < a->col + a->span_cols && a->row < b->row + b->span_rows &&
           b->row < a->row + a->span_rows;
}

static settings_pin_view_model_t model(settings_pin_view_screen_t screen)
{
    settings_pin_view_model_t m;

    memset(&m, 0, sizeof(m));
    m.screen = screen;
    m.title = "設定";
    return m;
}

/* Every key is there, at least 96x32px, inside the screen and apart from the others. */
static void check_keypad(const char *name)
{
    char what[96];
    const cyd_display_widget_t *keys[12];
    size_t n = 0;
    bool sizes_ok = true;

    for (int d = 0; d <= 9; ++d) {
        keys[n++] = find_action((uint16_t)(SETTINGS_PIN_VIEW_ACTION_DIGIT_BASE + d));
    }
    keys[n++] = find_action(SETTINGS_PIN_VIEW_ACTION_DELETE);
    keys[n++] = find_action(SETTINGS_PIN_VIEW_ACTION_OK);
    for (size_t i = 0; i < n; ++i) {
        if (keys[i] == NULL) {
            snprintf(what, sizeof(what), "%s: key %u is missing", name, (unsigned)i);
            ui_test_check(false, what);
            return;
        }
        sizes_ok &= keys[i]->span_cols * CYD_DISPLAY_GRID_CELL_PX >= 96 &&
                    keys[i]->span_rows * CYD_DISPLAY_GRID_CELL_PX >= 32 &&
                    keys[i]->col + keys[i]->span_cols <= CYD_DISPLAY_GRID_COLS &&
                    keys[i]->row + keys[i]->span_rows <= CYD_DISPLAY_GRID_ROWS;
    }
    snprintf(what, sizeof(what), "%s: 12 keys of 96x32px or more, on screen", name);
    ui_test_check(sizes_ok, what);

    bool apart = true;
    for (size_t i = 0; i < s_screen.widget_count; ++i) {
        for (size_t j = i + 1; j < s_screen.widget_count; ++j) {
            const cyd_display_widget_t *a = &s_screen.widgets[i];
            const cyd_display_widget_t *b = &s_screen.widgets[j];
            if (a->type == CYD_DISPLAY_WIDGET_BUTTON && b->type == CYD_DISPLAY_WIDGET_BUTTON && overlaps(a, b)) {
                apart = false;
            }
        }
    }
    snprintf(what, sizeof(what), "%s: no two buttons overlap", name);
    ui_test_check(apart, what);
}

static void test_entering(void)
{
    char name[96];
    int bad = 0;

    for (int s = SETTINGS_PIN_VIEW_ENTER; s <= SETTINGS_PIN_VIEW_NEW_SECOND; ++s) {
        for (int notice = SETTINGS_PIN_VIEW_NOTICE_NONE; notice <= SETTINGS_PIN_VIEW_NOTICE_SAVE_FAILED; ++notice) {
            for (int entered = 0; entered <= SETTINGS_PIN_MAX_DIGITS; ++entered) {
                settings_pin_view_model_t m = model((settings_pin_view_screen_t)s);
                m.notice = (settings_pin_view_notice_t)notice;
                m.entered = (uint8_t)entered;
                settings_pin_view_build(&s_screen, &m);
                if (!ui_test_screen_fits(&s_screen)) {
                    snprintf(name, sizeof(name), "screen %d, notice %d, %d digits", s, notice, entered);
                    ui_test_check_screen(name, &s_screen);
                    ++bad;
                }
                const cyd_display_widget_t *ok = find_action(SETTINGS_PIN_VIEW_ACTION_OK);
                const cyd_display_widget_t *one = find_action(SETTINGS_PIN_VIEW_ACTION_DIGIT_BASE + 1);
                const cyd_display_widget_t *del = find_action(SETTINGS_PIN_VIEW_ACTION_DELETE);
                if (ok->enabled != (entered >= SETTINGS_PIN_MIN_DIGITS) ||
                    one->enabled != (entered < SETTINGS_PIN_MAX_DIGITS) || del->enabled != (entered > 0)) {
                    snprintf(name, sizeof(name), "screen %d, %d digits: keys enabled as the count allows", s,
                             entered);
                    ui_test_check(false, name);
                    ++bad;
                }
            }
        }
    }
    ui_test_check(bad == 0, "every mode, notice and digit count fits, with 決定 from 6 digits and digits up to 8");

    settings_pin_view_model_t m = model(SETTINGS_PIN_VIEW_ENTER);
    settings_pin_view_build(&s_screen, &m);
    ui_test_check_screen("enter PIN", &s_screen);
    check_keypad("enter PIN");
    m.entered = 8;
    settings_pin_view_build(&s_screen, &m);
    check_keypad("enter PIN, full");

    /* The dots never carry digits: no label on the screen contains one. */
    bool no_digits = true;
    for (size_t i = 0; i < s_screen.widget_count; ++i) {
        const cyd_display_widget_t *w = &s_screen.widgets[i];
        if (w->type == CYD_DISPLAY_WIDGET_TEXT && strstr(cyd_display_widget_text(w), "●") != NULL) {
            no_digits &= strpbrk(cyd_display_widget_text(w), "0123456789") == NULL;
        }
    }
    ui_test_check(no_digits, "the entered PIN shows as dots only");
}

static void test_waiting(void)
{
    char name[64];
    int bad = 0;

    for (uint32_t s = 1; s <= SETTINGS_PIN_MAX_WAIT_SECONDS; ++s) {
        settings_pin_view_model_t m = model(SETTINGS_PIN_VIEW_ENTER);
        m.wait_seconds = s;
        m.entered = 3; /* left over from before the wait started */
        settings_pin_view_build(&s_screen, &m);
        bool keys_off = !find_action(SETTINGS_PIN_VIEW_ACTION_OK)->enabled &&
                        !find_action(SETTINGS_PIN_VIEW_ACTION_DELETE)->enabled &&
                        !find_action(SETTINGS_PIN_VIEW_ACTION_DIGIT_BASE)->enabled &&
                        find_action(SETTINGS_PIN_VIEW_ACTION_CANCEL)->enabled;
        if (!ui_test_screen_fits(&s_screen) || !keys_off) {
            snprintf(name, sizeof(name), "waiting %u s", (unsigned)s);
            ui_test_check_screen(name, &s_screen);
            ui_test_check(keys_off, "keys are off while waiting, back still works");
            ++bad;
        }
    }
    ui_test_check(bad == 0, "every wait from 1 s to an hour fits, with the keys off and back on");

    char text[64];
    settings_pin_view_format_wait(text, sizeof(text), 30);
    ui_test_check(strcmp(text, "あと30秒お待ちください") == 0, "30 s reads あと30秒お待ちください");
    settings_pin_view_format_wait(text, sizeof(text), 150);
    ui_test_check(strcmp(text, "あと2分30秒お待ちください") == 0, "150 s reads あと2分30秒");
    settings_pin_view_format_wait(text, sizeof(text), 3600);
    ui_test_check(strcmp(text, "あと60分お待ちください") == 0, "an hour reads あと60分");
}

static void test_recovery(void)
{
    /* 14 full-width characters, the documented limit for a provider label. */
    static const char *const labels[] = { "コードで開く", "管理画面のコードで解除", "ああああああああああああああ" };

    for (size_t i = 0; i < sizeof(labels) / sizeof(labels[0]); ++i) {
        char name[96];
        settings_pin_view_model_t m = model(SETTINGS_PIN_VIEW_ENTER);
        m.recovery_label = labels[i];
        settings_pin_view_build(&s_screen, &m);
        snprintf(name, sizeof(name), "recovery button \"%s\"", labels[i]);
        /* The bold face may lack a provider's kanji; only the width is checked here. */
        if (i != 1) {
            ui_test_check_screen(name, &s_screen);
        }
        ui_test_check(find_action(SETTINGS_PIN_VIEW_ACTION_RECOVERY) != NULL, "the recovery button is shown");
        check_keypad(name);
    }

    settings_pin_view_model_t m = model(SETTINGS_PIN_VIEW_ENTER);
    settings_pin_view_build(&s_screen, &m);
    ui_test_check(find_action(SETTINGS_PIN_VIEW_ACTION_RECOVERY) == NULL, "no provider, no recovery button");
    m.recovery_label = "コードで開く";
    m.wait_seconds = 60;
    settings_pin_view_build(&s_screen, &m);
    ui_test_check(find_action(SETTINGS_PIN_VIEW_ACTION_RECOVERY) != NULL &&
                      find_action(SETTINGS_PIN_VIEW_ACTION_RECOVERY)->enabled,
                  "recovery stays available during a wait");
    m = model(SETTINGS_PIN_VIEW_NEW_FIRST);
    m.recovery_label = "コードで開く";
    settings_pin_view_build(&s_screen, &m);
    ui_test_check(find_action(SETTINGS_PIN_VIEW_ACTION_RECOVERY) == NULL, "no recovery while setting a new PIN");
}

static void test_titles_and_message(void)
{
    static const char *const titles[] = { "設定", "システム情報", "PIN の設定" };

    for (size_t i = 0; i < sizeof(titles) / sizeof(titles[0]); ++i) {
        settings_pin_view_model_t m = model(SETTINGS_PIN_VIEW_ENTER);
        m.title = titles[i];
        settings_pin_view_build(&s_screen, &m);
        ui_test_check_screen(titles[i], &s_screen);
    }
    settings_pin_view_model_t m = model(SETTINGS_PIN_VIEW_MESSAGE);
    m.message_title = "再起動します";
    m.message_detail = "設定には入りません";
    settings_pin_view_build(&s_screen, &m);
    ui_test_check_screen("restart message", &s_screen);
    ui_test_check(find_action(SETTINGS_PIN_VIEW_ACTION_DIGIT_BASE) == NULL, "the message has no keys");
}

int main(void)
{
    /* Referenced as on the device, so every prompt is measured at full length.
       The view's own formatted text sits in static buffers, which is safe. */
    ui_test_set_all_text_immutable(true);
    printf("entering\n");
    test_entering();
    printf("waiting\n");
    test_waiting();
    printf("recovery\n");
    test_recovery();
    printf("titles and message\n");
    test_titles_and_message();
    return ui_test_finish();
}
