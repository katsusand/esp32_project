#include <stdio.h>
#include <string.h>
#include "cyd_text_input_view.h"
#include "cyd_ui.h"

/*
 * Layout (8px grid rows):
 *
 *   0-3    [戻る] title                          [保存]
 *   4-6    context label  context value           (optional)
 *   7-10   | input label  value|                |
 *   11-22  three rows of 32px character keys
 *   23-26  [ABC] [123] [   空白   ] [削除]
 *   27-29  [文字を表示] / [https://]              (password / URL only)
 *
 * The rows do not move when the context line is absent, so a key is always
 * where the operator's finger expects it.
 */
#define VIEW_HEADER_ROWS 4
#define VIEW_SIDE_BUTTON_COLS 8
#define VIEW_CONTEXT_ROW 4
#define VIEW_CONTEXT_ROWS 3
#define VIEW_VALUE_ROW 7
#define VIEW_VALUE_ROWS 4
#define VIEW_KEY_FIRST_ROW 11
#define VIEW_KEY_ROWS 4
#define VIEW_KEY_COLS 4
#define VIEW_FUNCTION_ROW 23
#define VIEW_FUNCTION_ROWS 4
#define VIEW_EXTRA_ROW 27
#define VIEW_EXTRA_ROWS 3
#define VIEW_EXTRA_COLS 20

/* Left and right padding inside the value box, in grid columns. */
#define VIEW_TEXT_INSET_COLS 1

const char *cyd_text_input_view_key_row(const cyd_text_input_view_model_t *model, size_t row)
{
    static const char *lower[] = { "qwertyuiop", "asdfghjkl", "zxcvbnm.-_" };
    static const char *upper[] = { "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM.-_" };
    static const char *symbol[] = { "1234567890", "~!@#$%^&*", "-_=+,.;:/?" };
    static const char *extra[] = { "1234567890", "()[]{}<>|", "'\"`/\\" };

    if (model == NULL || row >= 3) {
        return "";
    }
    if (model->digits_only) {
        return row == 0 ? "1234567890" : "";
    }
    if (model->force_upper && model->page == CYD_TEXT_INPUT_VIEW_PAGE_LOWER) {
        return upper[row];
    }
    switch (model->page) {
    case CYD_TEXT_INPUT_VIEW_PAGE_UPPER: return upper[row];
    case CYD_TEXT_INPUT_VIEW_PAGE_SYMBOL: return symbol[row];
    case CYD_TEXT_INPUT_VIEW_PAGE_SYMBOL_EXTRA: return extra[row];
    default: return lower[row];
    }
}

static const char *view_text(const char *text)
{
    return text != NULL ? text : "";
}

/* Grid columns that `text` needs in `font`, rounded up. */
static uint8_t view_cols_for(cyd_display_font_t font, const char *text)
{
    int32_t px = cyd_display_text_width(font, text);
    return (uint8_t)((px + CYD_DISPLAY_GRID_CELL_PX - 1) / CYD_DISPLAY_GRID_CELL_PX);
}

/*
 * The part of `value` (plus the cursor) that fits `max_px`: the whole value,
 * or "…" and as much of its end as fits. The end is what the operator is
 * typing, so it must stay visible. The cursor's width is always reserved, so
 * the text does not shift when it blinks.
 */
static void view_fit_value_tail(char *dst, size_t dst_size, const char *value, bool cursor, int32_t max_px)
{
    static const char ellipsis[] = "…";
    char probe[CYD_DISPLAY_TEXT_MAX_LEN + 8];
    const char *start = value;
    size_t len = strlen(value);
    bool cut = false;

    for (;;) {
        size_t tail = len - (size_t)(start - value);
        snprintf(probe, sizeof(probe), "%s%s|", cut ? ellipsis : "", start);
        if (tail + (cut ? sizeof(ellipsis) - 1U : 0U) + 1U <= CYD_DISPLAY_TEXT_MAX_LEN &&
            cyd_display_text_width(CYD_DISPLAY_FONT_BODY, probe) <= max_px) {
            break;
        }
        if (*start == '\0') {
            break;
        }
        /* Drop one whole UTF-8 character from the front. */
        start++;
        while ((*start & 0xC0) == 0x80) {
            start++;
        }
        cut = true;
    }
    snprintf(dst, dst_size, "%s%s%s", cut ? ellipsis : "", start, cursor ? "|" : "");
}

/* "label value" on one line: the label in subdued bold, the value after it. */
static void view_add_labeled_line(cyd_display_screen_t *screen,
                                  const char *label,
                                  const char *value,
                                  bool value_is_input,
                                  bool cursor,
                                  uint8_t row,
                                  uint8_t span_rows)
{
    const uint8_t first_col = VIEW_TEXT_INSET_COLS;
    const uint8_t last_col = CYD_DISPLAY_GRID_COLS - VIEW_TEXT_INSET_COLS;
    uint8_t value_col = first_col;

    if (label[0] != '\0') {
        uint8_t label_cols = view_cols_for(CYD_DISPLAY_FONT_BODY_BOLD, label);
        if (label_cols > (last_col - first_col) / 2) {
            label_cols = (uint8_t)((last_col - first_col) / 2);
        }
        cyd_ui_add_label(screen, label, first_col, row, label_cols, span_rows,
                         CYD_DISPLAY_ALIGN_LEFT, CYD_DISPLAY_FONT_BODY_BOLD, CYD_UI_THEME_SUBTEXT);
        value_col = (uint8_t)(first_col + label_cols + 1U);
    }

    const uint8_t value_cols = (uint8_t)(last_col - value_col);
    if (value_is_input) {
        char shown[CYD_DISPLAY_TEXT_MAX_LEN + 1];
        view_fit_value_tail(shown, sizeof(shown), value, cursor,
                            (int32_t)value_cols * CYD_DISPLAY_GRID_CELL_PX);
        cyd_ui_add_label(screen, shown, value_col, row, value_cols, span_rows,
                         CYD_DISPLAY_ALIGN_LEFT, CYD_DISPLAY_FONT_BODY, CYD_UI_THEME_TEXT);
    } else if (value[0] != '\0') {
        cyd_ui_add_label(screen, value, value_col, row, value_cols, span_rows,
                         CYD_DISPLAY_ALIGN_LEFT, CYD_DISPLAY_FONT_BODY, CYD_UI_THEME_TEXT);
    }
}

static void view_add_outlined(cyd_display_screen_t *screen,
                              const char *text,
                              uint8_t col,
                              uint8_t row,
                              uint8_t span_cols,
                              uint8_t span_rows,
                              uint16_t color,
                              uint16_t tint,
                              uint16_t action_id,
                              bool enabled)
{
    cyd_ui_add_styled_button(screen, text, col, row, span_cols, span_rows, CYD_DISPLAY_FONT_BODY_BOLD,
                             color, tint, color, CYD_UI_THEME_BORDER_PX, action_id, enabled);
}

static void view_add_keys(cyd_display_screen_t *screen, const cyd_text_input_view_model_t *model)
{
    for (size_t row = 0; row < 3; ++row) {
        const char *keys = cyd_text_input_view_key_row(model, row);
        size_t count = strlen(keys);
        uint8_t first_col = (uint8_t)((CYD_DISPLAY_GRID_COLS - count * VIEW_KEY_COLS) / 2U);
        uint8_t grid_row = (uint8_t)(VIEW_KEY_FIRST_ROW + row * VIEW_KEY_ROWS);

        /* 16px for every key: a 32px key leaves 20px for text, and W, M, @ and %
           are 24px wide in the 24px face. One size reads better than a few
           keys shrinking on their own. */
        for (size_t i = 0; i < count; ++i) {
            char label[2] = { keys[i], '\0' };
            cyd_ui_add_styled_button(screen, label, (uint8_t)(first_col + i * VIEW_KEY_COLS), grid_row,
                                     VIEW_KEY_COLS, VIEW_KEY_ROWS, CYD_DISPLAY_FONT_BODY_BOLD,
                                     CYD_UI_THEME_TEXT, CYD_UI_THEME_SURFACE, CYD_UI_THEME_LINE, 1,
                                     (uint16_t)(CYD_TEXT_INPUT_VIEW_ACTION_KEY_BASE + (uint8_t)keys[i]), true);
        }
    }
}

static void view_add_function_row(cyd_display_screen_t *screen, const cyd_text_input_view_model_t *model)
{
    const bool symbol_page = model->page == CYD_TEXT_INPUT_VIEW_PAGE_SYMBOL ||
                             model->page == CYD_TEXT_INPUT_VIEW_PAGE_SYMBOL_EXTRA;
    const bool letters_enabled = !model->digits_only;
    const char *case_label = "ABC";

    /* The case key is also the way back from the symbol pages. */
    if (!model->force_upper && (symbol_page || model->page == CYD_TEXT_INPUT_VIEW_PAGE_UPPER)) {
        case_label = "abc";
    }
    view_add_outlined(screen, case_label, 0, VIEW_FUNCTION_ROW, 8, VIEW_FUNCTION_ROWS,
                      CYD_UI_THEME_PRIMARY_SOFT, CYD_UI_THEME_SURFACE,
                      CYD_TEXT_INPUT_VIEW_ACTION_CASE, letters_enabled);
    view_add_outlined(screen, model->page == CYD_TEXT_INPUT_VIEW_PAGE_SYMBOL ? "()[]" : "123",
                      8, VIEW_FUNCTION_ROW, 8, VIEW_FUNCTION_ROWS,
                      CYD_UI_THEME_PRIMARY_SOFT, CYD_UI_THEME_SURFACE,
                      CYD_TEXT_INPUT_VIEW_ACTION_SYMBOL, letters_enabled);
    view_add_outlined(screen, "空白", 16, VIEW_FUNCTION_ROW, 16, VIEW_FUNCTION_ROWS,
                      CYD_UI_THEME_TEXT, CYD_UI_THEME_SURFACE,
                      CYD_TEXT_INPUT_VIEW_ACTION_SPACE, letters_enabled);
    view_add_outlined(screen, "削除", 32, VIEW_FUNCTION_ROW, 8, VIEW_FUNCTION_ROWS,
                      CYD_UI_THEME_DANGER_SOFT, CYD_UI_THEME_DANGER_TINT,
                      CYD_TEXT_INPUT_VIEW_ACTION_DELETE, true);
}

static void view_add_extra(cyd_display_screen_t *screen, const cyd_text_input_view_model_t *model)
{
    static const char *const schemes[] = { "http://", "https://", "元に戻す" };

    switch (model->extra) {
    case CYD_TEXT_INPUT_VIEW_EXTRA_SHOW_TOGGLE:
        view_add_outlined(screen, model->show_value ? "文字を隠す" : "文字を表示",
                          0, VIEW_EXTRA_ROW, VIEW_EXTRA_COLS, VIEW_EXTRA_ROWS,
                          CYD_UI_THEME_PRIMARY_SOFT, CYD_UI_THEME_SURFACE,
                          CYD_TEXT_INPUT_VIEW_ACTION_SHOW, true);
        break;
    case CYD_TEXT_INPUT_VIEW_EXTRA_URL_SCHEME:
        view_add_outlined(screen, schemes[model->url_scheme_step % 3U],
                          0, VIEW_EXTRA_ROW, VIEW_EXTRA_COLS, VIEW_EXTRA_ROWS,
                          CYD_UI_THEME_PRIMARY_SOFT, CYD_UI_THEME_SURFACE,
                          CYD_TEXT_INPUT_VIEW_ACTION_URL_SCHEME, true);
        break;
    default:
        break;
    }
}

void cyd_text_input_view_build(cyd_display_screen_t *screen, const cyd_text_input_view_model_t *model)
{
    if (screen == NULL || model == NULL) {
        return;
    }
    const char *context_label = view_text(model->context_label);
    const char *context_value = view_text(model->context_value);

    cyd_ui_screen_clear(screen);
    cyd_ui_add_panel(screen, 0, 0, CYD_DISPLAY_GRID_COLS, CYD_DISPLAY_GRID_ROWS, CYD_UI_THEME_BG, 0, 0, 0);

    /* Header: cancel on the left, save on the right, both where a thumb rests.
       The title comes from the caller, so it is 16px bold: 11 kanji fit, where
       24px would shrink most real titles anyway. */
    cyd_ui_add_panel(screen, 0, 0, CYD_DISPLAY_GRID_COLS, VIEW_HEADER_ROWS, CYD_UI_THEME_SURFACE, 0, 0, 0);
    view_add_outlined(screen, "戻る", 0, 0, VIEW_SIDE_BUTTON_COLS, VIEW_HEADER_ROWS,
                      CYD_UI_THEME_PRIMARY_SOFT, CYD_UI_THEME_SURFACE,
                      CYD_TEXT_INPUT_VIEW_ACTION_CANCEL, true);
    cyd_ui_add_label(screen, view_text(model->title), VIEW_SIDE_BUTTON_COLS + 1, 0,
                     CYD_DISPLAY_GRID_COLS - 2 * (VIEW_SIDE_BUTTON_COLS + 1), VIEW_HEADER_ROWS,
                     CYD_DISPLAY_ALIGN_CENTER, CYD_DISPLAY_FONT_BODY_BOLD, CYD_UI_THEME_TEXT);
    cyd_ui_add_styled_button(screen, "保存", CYD_DISPLAY_GRID_COLS - VIEW_SIDE_BUTTON_COLS, 0,
                             VIEW_SIDE_BUTTON_COLS, VIEW_HEADER_ROWS, CYD_DISPLAY_FONT_BODY_BOLD,
                             CYD_UI_THEME_ON_SUCCESS, CYD_UI_THEME_SUCCESS, CYD_UI_THEME_SUCCESS, 0,
                             CYD_TEXT_INPUT_VIEW_ACTION_SAVE, true);

    if (context_label[0] != '\0' || context_value[0] != '\0') {
        view_add_labeled_line(screen, context_label, context_value, false, false,
                              VIEW_CONTEXT_ROW, VIEW_CONTEXT_ROWS);
    }

    cyd_ui_add_panel(screen, 0, VIEW_VALUE_ROW, CYD_DISPLAY_GRID_COLS, VIEW_VALUE_ROWS,
                     CYD_UI_THEME_SURFACE, CYD_UI_THEME_PRIMARY, CYD_UI_THEME_BORDER_PX, 4);
    view_add_labeled_line(screen, view_text(model->input_label), view_text(model->value), true,
                          model->cursor_visible, VIEW_VALUE_ROW, VIEW_VALUE_ROWS);

    view_add_keys(screen, model);
    view_add_function_row(screen, model);
    view_add_extra(screen, model);
}
