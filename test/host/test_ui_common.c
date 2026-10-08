/*
 * Shared UI parts: the settings chrome, stepper rows and the keyboard screen.
 *
 * Every keyboard state and the chrome are built and measured against their
 * boxes with the real fonts (ui_test_check_screen), and the value line is
 * checked to keep the end of a long value in view.
 */

#include <stdio.h>
#include <string.h>
#include "cyd_display.h"
#include "cyd_text_input_view.h"
#include "cyd_ui.h"
#include "ui_test_support.h"

static cyd_display_screen_t s_screen;

static const cyd_display_widget_t *find_action(const cyd_display_screen_t *screen, uint16_t action_id)
{
    for (size_t i = 0; i < screen->widget_count; ++i) {
        if (screen->widgets[i].type == CYD_DISPLAY_WIDGET_BUTTON && screen->widgets[i].action_id == action_id) {
            return &screen->widgets[i];
        }
    }
    return NULL;
}

/* The value label sits on the value row in the regular 16px face. */
static const char *value_text(const cyd_display_screen_t *screen)
{
    for (size_t i = 0; i < screen->widget_count; ++i) {
        const cyd_display_widget_t *w = &screen->widgets[i];
        if (w->type == CYD_DISPLAY_WIDGET_TEXT && w->font == CYD_DISPLAY_FONT_BODY && w->row == 7) {
            return cyd_display_widget_text(w);
        }
    }
    return NULL;
}

static bool ends_with(const char *s, const char *suffix)
{
    size_t a = strlen(s);
    size_t b = strlen(suffix);
    return a >= b && strcmp(s + a - b, suffix) == 0;
}

static void test_chrome(void)
{
    const cyd_ui_settings_chrome_t chrome = {
        .app_title = "ネットワーク",
        .page_title = "アラーム",
        .page_index = 0,
        .page_count = 3,
        .back_action_id = 1,
        .prev_page_action_id = 2,
        .next_page_action_id = 3,
    };
    cyd_ui_screen_clear(&s_screen);
    ui_test_check(cyd_ui_add_settings_chrome(&s_screen, &chrome) == ESP_OK, "chrome builds");
    ui_test_check_screen("chrome", &s_screen);

    const cyd_display_widget_t *prev = find_action(&s_screen, 2);
    const cyd_display_widget_t *next = find_action(&s_screen, 3);
    ui_test_check(prev != NULL && !prev->enabled, "the first page disables 前へ");
    ui_test_check(next != NULL && next->enabled, "and enables 次へ");
    ui_test_check(find_action(&s_screen, 1) != NULL && find_action(&s_screen, 1)->row + find_action(&s_screen, 1)->span_rows <=
                      CYD_UI_SETTINGS_CONTENT_FIRST_ROW,
                  "the back button stays above the content area");
    ui_test_check(next != NULL && next->row > CYD_UI_SETTINGS_CONTENT_LAST_ROW,
                  "the page buttons stay below the content area");

    /* A long page title is cut on a character boundary, keeping " n/N". */
    cyd_ui_screen_clear(&s_screen);
    (void)cyd_ui_add_settings_page_nav(&s_screen, "ながいながいながいながいページのなまえ", 1, 12, 2, 3);
    const char *line = NULL;
    for (size_t i = 0; i < s_screen.widget_count; ++i) {
        if (s_screen.widgets[i].type == CYD_DISPLAY_WIDGET_TEXT) {
            line = cyd_display_widget_text(&s_screen.widgets[i]);
        }
    }
    ui_test_check(line != NULL && ends_with(line, " 2/12") && strlen(line) <= CYD_DISPLAY_TEXT_MAX_LEN,
                  "a long page title keeps its page counter");
}

static void test_stepper(void)
{
    cyd_ui_stepper_row_t row = {
        .label_text = "あかるさ",
        .value_text = "80",
        .row = 6,
        .label_col = 1,
        .label_span_cols = 14,
        .value_col = 22,
        .value_span_cols = 10,
        .button_left_col = 15,
        .button_right_col = 32,
        .button_span_cols = 7,
        .button_span_rows = 4,
        .decrease_action_id = 10,
        .increase_action_id = 11,
        .can_decrease = true,
        .can_increase = false,
    };
    cyd_ui_screen_clear(&s_screen);
    ui_test_check(cyd_ui_add_stepper_row(&s_screen, &row) == ESP_OK, "a 32px stepper row builds");
    ui_test_check_screen("32px stepper row", &s_screen);
    ui_test_check(find_action(&s_screen, 10)->font == CYD_DISPLAY_FONT_TITLE, "its buttons use 24px text");
    ui_test_check(!find_action(&s_screen, 11)->enabled, "a limit disables its button");

    /* system_settings_app.c's current geometry: 24x16px buttons. */
    row.label_text = "LcdBrightness:";
    row.label_col = 2;
    row.label_span_cols = 16;
    row.button_left_col = 21;
    row.button_right_col = 35;
    row.button_span_cols = 3;
    row.button_span_rows = 2;
    cyd_ui_screen_clear(&s_screen);
    (void)cyd_ui_add_stepper_row(&s_screen, &row);
    const cyd_display_widget_t *down = find_action(&s_screen, 10);
    ui_test_check(down->font == CYD_DISPLAY_FONT_LEGACY && strcmp(cyd_display_widget_text(down), "-") == 0,
                  "a legacy-sized button falls back to the legacy \"-\"");
    ui_test_check(s_screen.widgets[0].font == CYD_DISPLAY_FONT_LEGACY,
                  "a label too wide for 16px bold falls back to the legacy font");
}

static cyd_text_input_view_model_t keyboard(void)
{
    return (cyd_text_input_view_model_t){
        .title = "Wi-Fi パスワード",
        .context_label = "SSID",
        .context_value = "MyHome-5G",
        .input_label = "PASS",
        .value = "********",
        .cursor_visible = true,
        .extra = CYD_TEXT_INPUT_VIEW_EXTRA_SHOW_TOGGLE,
    };
}

static void test_keyboard_states(void)
{
    static const struct {
        cyd_text_input_view_page_t page;
        const char *name;
    } pages[] = {
        { CYD_TEXT_INPUT_VIEW_PAGE_LOWER, "keyboard lower" },
        { CYD_TEXT_INPUT_VIEW_PAGE_UPPER, "keyboard upper" },
        { CYD_TEXT_INPUT_VIEW_PAGE_SYMBOL, "keyboard symbols" },
        { CYD_TEXT_INPUT_VIEW_PAGE_SYMBOL_EXTRA, "keyboard symbols 2" },
    };
    cyd_text_input_view_model_t m = keyboard();

    for (size_t i = 0; i < sizeof(pages) / sizeof(pages[0]); ++i) {
        m.page = pages[i].page;
        cyd_text_input_view_build(&s_screen, &m);
        ui_test_check_screen(pages[i].name, &s_screen);
    }

    m.page = CYD_TEXT_INPUT_VIEW_PAGE_LOWER;
    m.show_value = true;
    cyd_text_input_view_build(&s_screen, &m);
    ui_test_check_screen("keyboard, password shown", &s_screen);

    m = (cyd_text_input_view_model_t){
        .title = "URL",
        .input_label = "URL",
        .value = "http://",
        .extra = CYD_TEXT_INPUT_VIEW_EXTRA_URL_SCHEME,
    };
    for (uint8_t step = 0; step < 3; ++step) {
        char name[48];
        m.url_scheme_step = step;
        cyd_text_input_view_build(&s_screen, &m);
        snprintf(name, sizeof(name), "keyboard URL step %u", (unsigned)step);
        ui_test_check_screen(name, &s_screen);
    }

    m = (cyd_text_input_view_model_t){ .title = "ひづけ", .input_label = "YYYY-MM-DD", .digits_only = true };
    cyd_text_input_view_build(&s_screen, &m);
    ui_test_check_screen("keyboard digits only", &s_screen);
    const cyd_display_widget_t *space = find_action(&s_screen, CYD_TEXT_INPUT_VIEW_ACTION_SPACE);
    ui_test_check(space != NULL && !space->enabled, "digits only disables 空白");
    ui_test_check(find_action(&s_screen, CYD_TEXT_INPUT_VIEW_ACTION_KEY_BASE + 'a') == NULL,
                  "digits only shows no letters");

    m = (cyd_text_input_view_model_t){ .force_upper = true, .page = CYD_TEXT_INPUT_VIEW_PAGE_LOWER };
    ui_test_check(strcmp(cyd_text_input_view_key_row(&m, 0), "QWERTYUIOP") == 0,
                  "force_upper shows upper-case keys on the lower page");
}

static void test_value_tail(void)
{
    cyd_text_input_view_model_t m = keyboard();
    const char *shown;

    m.value = "https://example.com/api/v1/devices/registration/very/long/path";
    cyd_text_input_view_build(&s_screen, &m);
    shown = value_text(&s_screen);
    ui_test_check(shown != NULL && strncmp(shown, "…", 3) == 0 && ends_with(shown, "long/path|"),
                  "a long value shows \"…\" and its end, with the cursor");
    ui_test_check(shown != NULL && strlen(shown) <= CYD_DISPLAY_TEXT_MAX_LEN,
                  "and fits the 40-byte widget text");

    m.cursor_visible = false;
    cyd_text_input_view_build(&s_screen, &m);
    shown = value_text(&s_screen);
    ui_test_check(shown != NULL && ends_with(shown, "long/path"), "a hidden cursor does not move the text");

    m.value = "あいうえおかきくけこさしすせそたちつてとなにぬねの";
    cyd_text_input_view_build(&s_screen, &m);
    shown = value_text(&s_screen);
    ui_test_check(shown != NULL && strncmp(shown, "…", 3) == 0 && ((unsigned char)shown[3] & 0xC0) != 0x80 &&
                      ends_with(shown, "の"),
                  "a long Japanese value is cut on a character boundary");

    m.value = "short";
    cyd_text_input_view_build(&s_screen, &m);
    shown = value_text(&s_screen);
    ui_test_check(shown != NULL && strcmp(shown, "short") == 0, "a short value is shown whole");
}

int main(void)
{
    /* Copy every string: the keyboard view builds its value line in a stack
       buffer, which "immutable" would keep a dangling reference to. Nothing
       here is longer than the 40-byte copy. */
    ui_test_set_all_text_immutable(false);
    printf("settings chrome\n");
    test_chrome();
    printf("stepper row\n");
    test_stepper();
    printf("keyboard states\n");
    test_keyboard_states();
    printf("keyboard value line\n");
    test_value_tail();
    return ui_test_finish();
}
