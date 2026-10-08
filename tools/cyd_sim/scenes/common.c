/*
 * Shared UI parts: the settings chrome, stepper rows and the keyboard screen
 * (cyd_text_input).
 *
 * Wording sticks to ASCII, kana and the firmware's own literals, which is all
 * the fonts are guaranteed to carry. The settings pages themselves still use
 * the legacy font; "settings_legacy_mix" shows how such a page looks inside the
 * new chrome until it is converted.
 */
#include "cyd_text_input_view.h"
#include "cyd_ui.h"
#include "sim_catalog.h"

enum {
    ACTION_BACK = 1,
    ACTION_PREV,
    ACTION_NEXT,
    ACTION_DOWN,
    ACTION_UP,
};

static void chrome(cyd_display_screen_t *screen, const char *app_title, const char *page_title,
                   size_t index, size_t count)
{
    const cyd_ui_settings_chrome_t c = {
        .app_title = app_title,
        .page_title = page_title,
        .page_index = index,
        .page_count = count,
        .back_action_id = ACTION_BACK,
        .prev_page_action_id = ACTION_PREV,
        .next_page_action_id = ACTION_NEXT,
    };
    (void)cyd_ui_add_settings_chrome(screen, &c);
}

static cyd_ui_stepper_row_t stepper(const char *label, const char *value, uint8_t row, bool down, bool up)
{
    /* The column split system_settings_app.c uses. */
    return (cyd_ui_stepper_row_t){
        .label_text = label,
        .value_text = value,
        .row = row,
        .label_col = 1,
        .label_span_cols = 14,
        .value_col = 22,
        .value_span_cols = 10,
        .button_left_col = 15,
        .button_right_col = 32,
        .button_span_cols = 7,
        .button_span_rows = 4,
        .decrease_action_id = ACTION_DOWN,
        .increase_action_id = ACTION_UP,
        .can_decrease = down,
        .can_increase = up,
    };
}

static void build_chrome(cyd_display_screen_t *screen, unsigned frame)
{
    (void)frame;
    cyd_ui_screen_clear(screen);
    chrome(screen, "ネットワーク", "ネットワーク2", 1, 3);
    cyd_ui_stepper_row_t row = stepper("ボリューム", "80", 6, true, true);
    (void)cyd_ui_add_stepper_row(screen, &row);
    row = stepper("タイムアウト", "30", 12, true, false);
    (void)cyd_ui_add_stepper_row(screen, &row);
    row = stepper("ステップ", "0", 18, false, true);
    (void)cyd_ui_add_stepper_row(screen, &row);
}

static void build_first_last(cyd_display_screen_t *screen, unsigned frame)
{
    (void)frame;
    cyd_ui_screen_clear(screen);
    chrome(screen, "ひとつだけのページ", "ページ", 0, 1);
}

static void build_legacy_mix(cyd_display_screen_t *screen, unsigned frame)
{
    (void)frame;
    cyd_ui_screen_clear(screen);
    cyd_ui_add_settings_title(screen, "SETTINGS");
    cyd_ui_add_text(screen, "Wi-Fi: connected", 2, 6, 36, 2, CYD_DISPLAY_ALIGN_LEFT, 1, CYD_UI_COLOR_WHITE);
    cyd_ui_add_text(screen, "SSID MyHome-5G", 2, 9, 36, 2, CYD_DISPLAY_ALIGN_CENTER, 2, CYD_UI_COLOR_CYAN);
    /* system_settings_app.c's current stepper geometry: 16px rows, 24px buttons. */
    const cyd_ui_stepper_row_t legacy_row = {
        .label_text = "LcdBrightness:",
        .value_text = "80",
        .row = 13,
        .label_col = 2,
        .label_span_cols = 16,
        .value_col = 26,
        .value_span_cols = 8,
        .button_left_col = 21,
        .button_right_col = 35,
        .button_span_cols = 3,
        .button_span_rows = 2,
        .decrease_action_id = ACTION_DOWN,
        .increase_action_id = ACTION_UP,
        .can_decrease = true,
        .can_increase = true,
    };
    (void)cyd_ui_add_stepper_row(screen, &legacy_row);
    cyd_ui_add_button(screen, "Touch Calib", 6, 18, 28, 3, CYD_UI_COLOR_BLUE, CYD_UI_COLOR_CYAN, 9);
    (void)cyd_ui_add_settings_page_nav(screen, "GENERAL", 0, 6, ACTION_PREV, ACTION_NEXT);
    cyd_ui_add_settings_back(screen, ACTION_BACK);
}

static cyd_text_input_view_model_t keyboard_model(void)
{
    return (cyd_text_input_view_model_t){
        .title = "Wi-Fi パスワード",
        .context_label = "SSID",
        .context_value = "MyHome-5G",
        .input_label = "PASS",
        .value = "********",
        .cursor_visible = true,
        .page = CYD_TEXT_INPUT_VIEW_PAGE_LOWER,
        .extra = CYD_TEXT_INPUT_VIEW_EXTRA_SHOW_TOGGLE,
    };
}

static void build_kb_password(cyd_display_screen_t *screen, unsigned frame)
{
    cyd_text_input_view_model_t m = keyboard_model();
    /* Blinks like the device: the cursor toggles every 500 ms (about 30 frames). */
    m.cursor_visible = (frame / 30U) % 2U == 0U;
    cyd_text_input_view_build(screen, &m);
}

static void build_kb_shown(cyd_display_screen_t *screen, unsigned frame)
{
    (void)frame;
    cyd_text_input_view_model_t m = keyboard_model();
    m.value = "correct horse battery staple";
    m.show_value = true;
    m.page = CYD_TEXT_INPUT_VIEW_PAGE_UPPER;
    cyd_text_input_view_build(screen, &m);
}

static void build_kb_symbols(cyd_display_screen_t *screen, unsigned frame)
{
    (void)frame;
    cyd_text_input_view_model_t m = keyboard_model();
    m.page = CYD_TEXT_INPUT_VIEW_PAGE_SYMBOL;
    cyd_text_input_view_build(screen, &m);
}

static void build_kb_symbols_extra(cyd_display_screen_t *screen, unsigned frame)
{
    (void)frame;
    cyd_text_input_view_model_t m = keyboard_model();
    m.page = CYD_TEXT_INPUT_VIEW_PAGE_SYMBOL_EXTRA;
    cyd_text_input_view_build(screen, &m);
}

static void build_kb_url(cyd_display_screen_t *screen, unsigned frame)
{
    (void)frame;
    const cyd_text_input_view_model_t m = {
        .title = "サーバー URL",
        .input_label = "URL",
        .value = "https://example.com/api/v1/devices/registration/very/long/path",
        .cursor_visible = true,
        .page = CYD_TEXT_INPUT_VIEW_PAGE_LOWER,
        .extra = CYD_TEXT_INPUT_VIEW_EXTRA_URL_SCHEME,
        .url_scheme_step = 1,
    };
    cyd_text_input_view_build(screen, &m);
}

static void build_kb_digits(cyd_display_screen_t *screen, unsigned frame)
{
    (void)frame;
    const cyd_text_input_view_model_t m = {
        .title = "ひづけ",
        .input_label = "YYYY-MM-DD",
        .value = "1990-04-",
        .cursor_visible = true,
        .digits_only = true,
    };
    cyd_text_input_view_build(screen, &m);
}

static const cyd_sim_scene_t k_scenes[] = {
    { "settings_chrome", "設定画面の共通枠", "見出し・戻る・増減行・ページ送り (2/3)", build_chrome },
    { "settings_single_page", "1 ページだけの設定", "前へ・次へが両方無効", build_first_last },
    { "settings_legacy_mix", "旧書体のページ", "段階 3 で日本語化するまでの見た目", build_legacy_mix },
    { "keyboard_password", "キーボード (パスワード)", "伏せ字、カーソル点滅", build_kb_password },
    { "keyboard_shown", "キーボード (表示中・大文字)", "長い値は末尾を表示", build_kb_shown },
    { "keyboard_symbols", "キーボード (記号 1)", NULL, build_kb_symbols },
    { "keyboard_symbols_extra", "キーボード (記号 2)", NULL, build_kb_symbols_extra },
    { "keyboard_url", "キーボード (URL)", "https:// 切り替え、長い URL", build_kb_url },
    { "keyboard_digits", "キーボード (数字だけ)", "生年月日などの入力", build_kb_digits },
};

CYD_SIM_REGISTER_SCENES(common, CYD_SIM_ORDER_COMMON, k_scenes);
