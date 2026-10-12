/*
 * The boot touch shortcut prompt (system_boot_view.c), measured with the real
 * fonts. It is the first screen of a held boot, so a line that is cut or
 * shrunk would be the first thing the user sees.
 */

#include <stdio.h>
#include <string.h>
#include "cyd_display.h"
#include "system_boot_view.h"
#include "ui_test_support.h"

static cyd_display_screen_t s_screen;

static const cyd_display_widget_t *find_label(const char *text)
{
    for (size_t i = 0; i < s_screen.widget_count; ++i) {
        const cyd_display_widget_t *w = &s_screen.widgets[i];
        if (w->type == CYD_DISPLAY_WIDGET_TEXT && strcmp(cyd_display_widget_text(w), text) == 0) {
            return w;
        }
    }
    return NULL;
}

int main(void)
{
    ui_test_set_all_text_immutable(true); /* the prompt is literals only, as on the device */

    printf("shortcut prompt\n");
    system_boot_view_build_shortcut_prompt(&s_screen);
    ui_test_check_screen("shortcut prompt", &s_screen);

    const cyd_display_widget_t *what = find_label("設定を開きます");
    const cyd_display_widget_t *todo = find_label("指を離してください");
    ui_test_check(what != NULL, "says that settings will open");
    ui_test_check(todo != NULL, "says to lift the finger");
    ui_test_check(what != NULL && todo != NULL && todo->font == CYD_DISPLAY_FONT_TITLE &&
                      what->font != CYD_DISPLAY_FONT_TITLE,
                  "the instruction is the larger line");

    bool has_button = false;
    for (size_t i = 0; i < s_screen.widget_count; ++i) {
        has_button = has_button || s_screen.widgets[i].type == CYD_DISPLAY_WIDGET_BUTTON;
    }
    /* The finger is still down; a button under it would be pressed by the release. */
    ui_test_check(!has_button, "no buttons");

    return ui_test_finish();
}
