#include "system_boot_view.h"
#include "cyd_ui.h"

void system_boot_view_build_shortcut_prompt(cyd_display_screen_t *screen)
{
    if (screen == NULL) {
        return;
    }

    /*
     * No buttons: nothing can be tapped yet, and the finger is still down.
     * The instruction is the larger line, because it is what the user must do.
     */
    cyd_ui_screen_clear(screen);
    cyd_ui_add_panel(screen, 0, 0, CYD_DISPLAY_GRID_COLS, CYD_DISPLAY_GRID_ROWS, CYD_UI_THEME_BG, 0, 0, 0);
    cyd_ui_add_label(screen, "設定を開きます", 1, 9, CYD_DISPLAY_GRID_COLS - 2, 3,
                     CYD_DISPLAY_ALIGN_CENTER, CYD_DISPLAY_FONT_BODY, CYD_UI_THEME_SUBTEXT);
    cyd_ui_add_label(screen, "指を離してください", 1, 13, CYD_DISPLAY_GRID_COLS - 2, 4,
                     CYD_DISPLAY_ALIGN_CENTER, CYD_DISPLAY_FONT_TITLE, CYD_UI_THEME_INFO);
}
