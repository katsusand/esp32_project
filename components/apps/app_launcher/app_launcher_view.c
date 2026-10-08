#include "app_launcher_view.h"
#include "cyd_ui.h"

void app_launcher_view_build(cyd_display_screen_t *screen, const app_launcher_view_model_t *m)
{
    if (screen == NULL || m == NULL) {
        return;
    }
    size_t count = m->count < APP_LAUNCHER_VIEW_APPS_PER_PAGE ? m->count : APP_LAUNCHER_VIEW_APPS_PER_PAGE;

    cyd_ui_screen_clear(screen);
    cyd_ui_add_panel(screen, 0, 0, CYD_DISPLAY_GRID_COLS, CYD_DISPLAY_GRID_ROWS, CYD_UI_THEME_BG, 0, 0, 0);
    cyd_ui_add_settings_title(screen, "アプリ");

    if (m->total == 0) {
        cyd_ui_add_label(screen, "登録されたアプリはありません", 1, 12, 38, 3, CYD_DISPLAY_ALIGN_CENTER,
                         CYD_DISPLAY_FONT_BODY, CYD_UI_THEME_SUBTEXT);
    }
    for (size_t i = 0; i < count; ++i) {
        cyd_ui_add_styled_button(screen, m->titles[i] != NULL ? m->titles[i] : "", 2, (uint8_t)(5 + i * 5), 36, 4,
                                 CYD_DISPLAY_FONT_BODY_BOLD, CYD_UI_THEME_TEXT, CYD_UI_THEME_SURFACE,
                                 CYD_UI_THEME_LINE, CYD_UI_THEME_BORDER_PX,
                                 (uint16_t)(APP_LAUNCHER_ACTION_APP_BASE + i), true);
    }
    /* One page needs no navigation; drawing two dead buttons would only add noise. */
    if (m->page_count > 1 && m->page_index < m->page_count) {
        (void)cyd_ui_add_settings_page_nav(screen, "アプリ", m->page_index, m->page_count,
                                           APP_LAUNCHER_ACTION_PREV_PAGE, APP_LAUNCHER_ACTION_NEXT_PAGE);
    }
    if (m->show_back) {
        cyd_ui_add_settings_back(screen, APP_LAUNCHER_ACTION_BACK);
    }
}
