/*
 * Reference scenes for the renderer itself: every face, the theme colours and
 * the fit rules. Useful when changing cyd_display_render.hpp or regenerating
 * the fonts, independent of any app.
 *
 * The wording sticks to kana, ASCII and scripts/ui_fonts/extra_chars.txt on
 * purpose: the fonts carry only those and the characters of the firmware's own
 * string literals, so any other kanji here would draw as a box in a project
 * whose firmware does not happen to use it.
 */
#include <stdio.h>
#include "cyd_ui.h"
#include "sim_catalog.h"

static void background(cyd_display_screen_t *screen)
{
    cyd_ui_add_panel(screen, 0, 0, 40, 30, CYD_UI_THEME_BG, 0, 0, 0);
}

static void build_faces(cyd_display_screen_t *screen, unsigned frame)
{
    (void)frame;
    background(screen);
    cyd_ui_add_label(screen, "16px ひらがな カタカナ ABC 123", 1, 0, 38, 3,
                     CYD_DISPLAY_ALIGN_LEFT, CYD_DISPLAY_FONT_BODY, CYD_UI_THEME_TEXT);
    cyd_ui_add_label(screen, "16px ボールド Wi-Fi", 1, 3, 38, 3,
                     CYD_DISPLAY_ALIGN_LEFT, CYD_DISPLAY_FONT_BODY_BOLD, CYD_UI_THEME_TEXT);
    cyd_ui_add_label(screen, "24px タイトル", 1, 6, 38, 4,
                     CYD_DISPLAY_ALIGN_LEFT, CYD_DISPLAY_FONT_TITLE, CYD_UI_THEME_INFO);
    cyd_ui_add_label(screen, "12:34", 1, 10, 38, 7,
                     CYD_DISPLAY_ALIGN_LEFT, CYD_DISPLAY_FONT_CLOCK_MEDIUM, CYD_UI_THEME_TEXT);
    cyd_ui_add_label(screen, "09:41", 1, 18, 38, 9,
                     CYD_DISPLAY_ALIGN_CENTER, CYD_DISPLAY_FONT_CLOCK_LARGE, CYD_UI_THEME_TEXT);
    cyd_ui_add_label(screen, "2026年10月8日（木）", 1, 27, 38, 3,
                     CYD_DISPLAY_ALIGN_RIGHT, CYD_DISPLAY_FONT_BODY, CYD_UI_THEME_SUBTEXT);
}

static void build_theme(cyd_display_screen_t *screen, unsigned frame)
{
    (void)frame;
    background(screen);
    cyd_ui_add_panel(screen, 0, 0, 40, 3, CYD_UI_THEME_SURFACE, 0, 0, 0);
    cyd_ui_add_label(screen, "テーマとボタン", 1, 0, 38, 3,
                     CYD_DISPLAY_ALIGN_LEFT, CYD_DISPLAY_FONT_BODY_BOLD, CYD_UI_THEME_TEXT);
    cyd_ui_add_styled_button(screen, "はい", 1, 4, 12, 6, CYD_DISPLAY_FONT_TITLE,
                             CYD_UI_THEME_ON_SUCCESS, CYD_UI_THEME_SUCCESS, CYD_UI_THEME_SUCCESS,
                             0, 1, true);
    cyd_ui_add_styled_button(screen, "いいえ", 14, 4, 12, 6, CYD_DISPLAY_FONT_TITLE,
                             CYD_UI_THEME_PRIMARY_SOFT, CYD_UI_THEME_SURFACE, CYD_UI_THEME_PRIMARY,
                             CYD_UI_THEME_BORDER_PX, 2, true);
    cyd_ui_add_styled_button(screen, "むこう", 27, 4, 12, 6, CYD_DISPLAY_FONT_TITLE,
                             CYD_UI_THEME_TEXT, CYD_UI_THEME_PRIMARY, CYD_UI_THEME_PRIMARY,
                             0, 3, false);
    cyd_ui_add_styled_button(screen, "まって", 1, 11, 12, 6, CYD_DISPLAY_FONT_TITLE,
                             CYD_UI_THEME_WARNING, CYD_UI_THEME_WARNING_TINT, CYD_UI_THEME_WARNING,
                             CYD_UI_THEME_BORDER_PX, 4, true);
    cyd_ui_add_styled_button(screen, "ガイド", 14, 11, 12, 6, CYD_DISPLAY_FONT_TITLE,
                             CYD_UI_THEME_ON_INFO, CYD_UI_THEME_INFO, CYD_UI_THEME_INFO,
                             0, 5, true);
    cyd_ui_add_styled_button(screen, "きけん", 27, 11, 12, 6, CYD_DISPLAY_FONT_TITLE,
                             CYD_UI_THEME_DANGER, CYD_UI_THEME_DANGER_TINT, CYD_UI_THEME_DANGER,
                             CYD_UI_THEME_BORDER_PX, 6, true);
    cyd_ui_add_panel(screen, 1, 19, 38, 9, CYD_UI_THEME_INFO_TINT, CYD_UI_THEME_INFO,
                     CYD_UI_THEME_BORDER_PX, 10);
    cyd_ui_add_label(screen, "カードをタッチ", 1, 19, 38, 9,
                     CYD_DISPLAY_ALIGN_CENTER, CYD_DISPLAY_FONT_TITLE, CYD_UI_THEME_INFO);
}

static void build_fit(cyd_display_screen_t *screen, unsigned frame)
{
    static char copied[96];
    (void)frame;
    background(screen);
    cyd_ui_add_label(screen, "はみだしのチェック", 1, 0, 38, 3,
                     CYD_DISPLAY_ALIGN_LEFT, CYD_DISPLAY_FONT_BODY_BOLD, CYD_UI_THEME_SUBTEXT);
    /* TITLE that does not fit falls back to BODY_BOLD. */
    cyd_ui_add_panel(screen, 1, 3, 38, 4, CYD_UI_THEME_SURFACE, 0, 0, 4);
    cyd_ui_add_label(screen, "しばらくしてからもういちどおためしください", 1, 3, 38, 4,
                     CYD_DISPLAY_ALIGN_CENTER, CYD_DISPLAY_FONT_TITLE, CYD_UI_THEME_TEXT);
    /* ...and is cut with an ellipsis when even that is too wide. */
    cyd_ui_add_panel(screen, 1, 8, 24, 4, CYD_UI_THEME_SURFACE, 0, 0, 4);
    cyd_ui_add_label(screen, "かんりしゃにれんらくしてください", 1, 8, 24, 4,
                     CYD_DISPLAY_ALIGN_LEFT, CYD_DISPLAY_FONT_BODY, CYD_UI_THEME_TEXT);
    /* A non-literal is copied, and cut at a character boundary at 40 bytes. */
    snprintf(copied, sizeof(copied), "%s", "しばらくしてからもういちどおためしください");
    cyd_ui_add_panel(screen, 1, 13, 38, 4, CYD_UI_THEME_SURFACE, 0, 0, 4);
    cyd_ui_add_label(screen, copied, 1, 13, 38, 4,
                     CYD_DISPLAY_ALIGN_LEFT, CYD_DISPLAY_FONT_BODY, CYD_UI_THEME_WARNING);
    /* Clock digits fall back from 64px to 48px. */
    cyd_ui_add_label(screen, "2026/10/08", 1, 18, 30, 8,
                     CYD_DISPLAY_ALIGN_CENTER, CYD_DISPLAY_FONT_CLOCK_LARGE, CYD_UI_THEME_TEXT);
    /* A character outside the subset shows as a box, not a gap. */
    cyd_ui_add_label(screen, "→ \xE9\xBE\x98", 1, 26, 38, 4,
                     CYD_DISPLAY_ALIGN_LEFT, CYD_DISPLAY_FONT_BODY, CYD_UI_THEME_SUBTEXT);
}

static const cyd_sim_scene_t k_scenes[] = {
    { "specimen_faces", "書体見本", "body / body_bold / title / clock_medium / clock_large", build_faces },
    { "specimen_theme", "テーマ色とボタン", "塗り・枠線・無効・注意・案内・危険", build_theme },
    { "specimen_fit", "はみ出しの扱い", "縮小 → 省略記号、動的文字列の切り詰め、未収録文字", build_fit },
};

CYD_SIM_REGISTER_SCENES(specimen, CYD_SIM_ORDER_SPECIMEN, k_scenes);
