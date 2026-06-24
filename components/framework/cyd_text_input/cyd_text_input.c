#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "cyd_display.h"
#include "cyd_text_input.h"
#include "cyd_ui.h"

#define TAG "cyd_text_input"

#define TEXT_INPUT_KEY_BASE 0x5200
#define TEXT_INPUT_DELETE 0x5301
#define TEXT_INPUT_SPACE 0x5302
#define TEXT_INPUT_CANCEL 0x5303
#define TEXT_INPUT_SAVE 0x5304
#define TEXT_INPUT_SYMBOL 0x5305
#define TEXT_INPUT_CASE 0x5306
#define TEXT_INPUT_SHOW 0x5307
#define TEXT_INPUT_URL_SCHEME 0x5308
#define TEXT_INPUT_CURSOR_BLINK_MS 500

typedef enum {
    TEXT_INPUT_PAGE_LOWER = 0,
    TEXT_INPUT_PAGE_UPPER,
    TEXT_INPUT_PAGE_SYMBOL,
    TEXT_INPUT_PAGE_SYMBOL_EXTRA,
} text_input_page_t;

typedef struct {
    bool pending;
    bool long_pressed;
    uint16_t action_id;
} text_input_touch_tracker_t;

typedef struct {
    bool initialized;
    char title[CYD_DISPLAY_TEXT_MAX_LEN + 1];
    char context_label[17];
    char context_value[CYD_DISPLAY_TEXT_MAX_LEN + 1];
    char input_label[17];
    char value[CYD_TEXT_INPUT_MAX_LEN + 1];
    char url_restore[CYD_TEXT_INPUT_MAX_LEN + 1];
    size_t max_len;
    bool obscure_input;
    bool show_value;
    bool cursor_visible;
    TickType_t last_cursor_tick;
    uint8_t url_scheme_step;
    cyd_text_input_mode_t mode;
    text_input_page_t page;
    text_input_touch_tracker_t touch;
} text_input_session_t;

static cyd_display_screen_t s_screen;
static text_input_session_t s_session;

static bool text_input_confirmed_action(const cyd_input_event_t *event, uint16_t *action_id)
{
    if (event == NULL || event->type != CYD_INPUT_EVENT_TOUCH) {
        return false;
    }
    switch (event->data.touch.action) {
    case CYD_INPUT_TOUCH_ACTION_PRESS:
        s_session.touch.pending = cyd_display_hit_test_action(event->data.touch.x,
                                                               event->data.touch.y,
                                                               &s_session.touch.action_id);
        s_session.touch.long_pressed = false;
        return false;
    case CYD_INPUT_TOUCH_ACTION_LONG_PRESS:
    case CYD_INPUT_TOUCH_ACTION_REPEAT:
        s_session.touch.long_pressed = s_session.touch.pending;
        return false;
    case CYD_INPUT_TOUCH_ACTION_RELEASE: {
        uint16_t released = 0;
        bool confirmed = s_session.touch.pending &&
                         !s_session.touch.long_pressed &&
                         cyd_display_hit_test_action(event->data.touch.x, event->data.touch.y, &released) &&
                         released == s_session.touch.action_id;
        s_session.touch = (text_input_touch_tracker_t){ 0 };
        if (confirmed && action_id != NULL) {
            *action_id = released;
        }
        return confirmed;
    }
    default:
        return false;
    }
}

static const char *text_input_row(size_t row)
{
    static const char *lower[] = { "qwertyuiop", "asdfghjkl", "zxcvbnm.-_" };
    static const char *upper[] = { "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM.-_" };
    static const char *symbol[] = { "1234567890", "~!@#$%^&*", "-_=+,.;:/?" };
    static const char *extra[] = { "1234567890", "()[]{}<>|", "'\"`/\\" };
    if (row >= 3) {
        return "";
    }
    switch (s_session.page) {
    case TEXT_INPUT_PAGE_UPPER: return upper[row];
    case TEXT_INPUT_PAGE_SYMBOL: return symbol[row];
    case TEXT_INPUT_PAGE_SYMBOL_EXTRA: return extra[row];
    default: return lower[row];
    }
}

static void text_input_visible_value(char *dst, size_t dst_size)
{
    char masked[CYD_TEXT_INPUT_MAX_LEN + 1] = { 0 };
    const char *source = s_session.value;
    if (s_session.obscure_input && !s_session.show_value) {
        size_t len = strlen(source);
        memset(masked, '*', len);
        masked[len] = '\0';
        source = masked;
    }
    size_t len = strlen(source);
    if (len < dst_size) {
        snprintf(dst, dst_size, "%s", source);
    } else if (dst_size > 4) {
        snprintf(dst, dst_size, "...%s", source + len - (dst_size - 4));
    }
}

static void text_input_labeled_line(char *dst, size_t dst_size, const char *label, const char *value)
{
    snprintf(dst, dst_size, "%s%s%s", label != NULL ? label : "",
             value != NULL && value[0] != '\0' ? " " : "",
             value != NULL ? value : "");
}

static const char *text_input_url_button(void)
{
    static const char *labels[] = { "http://", "https://", "UNDO" };
    return labels[s_session.url_scheme_step % 3U];
}

static esp_err_t text_input_render(void)
{
    char context[CYD_DISPLAY_TEXT_MAX_LEN + 1] = { 0 };
    char value_line[CYD_DISPLAY_TEXT_MAX_LEN + 1] = { 0 };
    char visible[34] = { 0 };
    const bool has_title = s_session.title[0] != '\0';
    const bool symbol_page = s_session.page == TEXT_INPUT_PAGE_SYMBOL ||
                             s_session.page == TEXT_INPUT_PAGE_SYMBOL_EXTRA;
    const uint8_t rows[] = { 10, 13, 16 };

    text_input_visible_value(visible, sizeof(visible));
    if (s_session.cursor_visible && strlen(visible) + 1 < sizeof(visible)) {
        strcat(visible, "|");
    }
    text_input_labeled_line(context, sizeof(context), s_session.context_label, s_session.context_value);
    text_input_labeled_line(value_line, sizeof(value_line), s_session.input_label, visible);

    cyd_ui_screen_clear(&s_screen);
    cyd_ui_add_button(&s_screen, "<<", 0, 0, 6, 3, CYD_UI_COLOR_BLUE, CYD_UI_COLOR_CYAN, TEXT_INPUT_CANCEL);
    if (has_title) {
        cyd_ui_add_text(&s_screen, s_session.title, 0, 1, CYD_DISPLAY_GRID_COLS, 2,
                        CYD_DISPLAY_ALIGN_CENTER, 2, CYD_UI_COLOR_YELLOW);
    }
    if (s_session.context_label[0] != '\0') {
        cyd_ui_add_text(&s_screen, context, 1, has_title ? 5 : 4, CYD_DISPLAY_GRID_COLS - 2, 1,
                        CYD_DISPLAY_ALIGN_LEFT, 1, CYD_UI_COLOR_WHITE);
    }
    cyd_ui_add_text(&s_screen, value_line, 1, has_title ? 7 : 6, CYD_DISPLAY_GRID_COLS - 2, 1,
                    CYD_DISPLAY_ALIGN_LEFT, 1, CYD_UI_COLOR_CYAN);

    for (size_t row = 0; row < 3; ++row) {
        const char *keys = text_input_row(row);
        uint8_t offset = row == 1 ? 2 : 0;
        for (size_t i = 0; keys[i] != '\0'; ++i) {
            char label[2] = { keys[i], '\0' };
            cyd_ui_add_button(&s_screen, label, (uint8_t)(offset + i * 4), rows[row], 4, 2,
                              CYD_UI_COLOR_DARKGREY, CYD_UI_COLOR_LIGHTGREY,
                              (uint16_t)(TEXT_INPUT_KEY_BASE + (uint8_t)keys[i]));
        }
    }

    cyd_ui_add_button(&s_screen, symbol_page ? "abc" : (s_session.page == TEXT_INPUT_PAGE_UPPER ? "abc" : "ABC"),
                      0, 21, 8, 3, CYD_UI_COLOR_BLUE, CYD_UI_COLOR_CYAN, TEXT_INPUT_CASE);
    cyd_ui_add_button(&s_screen, s_session.page == TEXT_INPUT_PAGE_SYMBOL ? "()[]" : "123",
                      8, 21, 8, 3, CYD_UI_COLOR_BLUE, CYD_UI_COLOR_CYAN, TEXT_INPUT_SYMBOL);
    cyd_ui_add_button(&s_screen, "SPACE", 16, 21, 16, 3,
                      CYD_UI_COLOR_DARKGREY, CYD_UI_COLOR_LIGHTGREY, TEXT_INPUT_SPACE);
    cyd_ui_add_button(&s_screen, "DEL", 32, 21, 8, 3,
                      CYD_UI_COLOR_RED, CYD_UI_COLOR_LIGHTGREY, TEXT_INPUT_DELETE);
    if (s_session.obscure_input) {
        char show[16];
        snprintf(show, sizeof(show), "[%c] SHOW", s_session.show_value ? 'x' : ' ');
        cyd_ui_add_button(&s_screen, show, 1, 26, 18, 3,
                          s_session.show_value ? CYD_UI_COLOR_BLUE : CYD_UI_COLOR_DARKGREY,
                          CYD_UI_COLOR_LIGHTGREY, TEXT_INPUT_SHOW);
    } else if (s_session.mode == CYD_TEXT_INPUT_MODE_URL) {
        cyd_ui_add_button(&s_screen, text_input_url_button(), 1, 26, 18, 3,
                          CYD_UI_COLOR_BLUE, CYD_UI_COLOR_CYAN, TEXT_INPUT_URL_SCHEME);
    }
    cyd_ui_add_button(&s_screen, "SAVE", 21, 26, 18, 3,
                      CYD_UI_COLOR_GREEN, CYD_UI_COLOR_LIGHTGREY, TEXT_INPUT_SAVE);
    return cyd_ui_submit(&s_screen);
}

static void text_input_reset_transient_state(void)
{
    s_session.cursor_visible = true;
    s_session.last_cursor_tick = xTaskGetTickCount();
    s_session.url_scheme_step = 0;
    s_session.url_restore[0] = '\0';
}

esp_err_t cyd_text_input_begin_session(const cyd_text_input_config_t *config)
{
    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG, "config is null");
    ESP_RETURN_ON_FALSE(config->input_label != NULL, ESP_ERR_INVALID_ARG, TAG, "input label is null");
    ESP_RETURN_ON_FALSE(config->max_len > 0 && config->max_len <= CYD_TEXT_INPUT_MAX_LEN,
                        ESP_ERR_INVALID_ARG, TAG, "invalid max len");
    ESP_RETURN_ON_FALSE(config->mode >= CYD_TEXT_INPUT_MODE_GENERIC && config->mode <= CYD_TEXT_INPUT_MODE_URL,
                        ESP_ERR_INVALID_ARG, TAG, "invalid mode");

    memset(&s_session, 0, sizeof(s_session));
    s_session.initialized = true;
    s_session.max_len = config->max_len;
    s_session.obscure_input = config->obscure_input || config->mode == CYD_TEXT_INPUT_MODE_PASSWORD;
    s_session.show_value = !s_session.obscure_input;
    s_session.mode = config->mode;
    snprintf(s_session.title, sizeof(s_session.title), "%s", config->title != NULL ? config->title : "");
    snprintf(s_session.context_label, sizeof(s_session.context_label), "%s",
             config->context_label != NULL ? config->context_label : "");
    snprintf(s_session.context_value, sizeof(s_session.context_value), "%s",
             config->context_value != NULL ? config->context_value : "");
    snprintf(s_session.input_label, sizeof(s_session.input_label), "%s", config->input_label);
    snprintf(s_session.value, sizeof(s_session.value), "%s",
             config->initial_text != NULL ? config->initial_text : "");
    s_session.value[s_session.max_len] = '\0';
    text_input_reset_transient_state();
    return text_input_render();
}

esp_err_t cyd_text_input_poll_session(const cyd_input_event_t *event,
                                      cyd_text_input_result_t *result,
                                      char *text_out,
                                      size_t text_out_size)
{
    ESP_RETURN_ON_FALSE(result != NULL, ESP_ERR_INVALID_ARG, TAG, "result is null");
    ESP_RETURN_ON_FALSE(s_session.initialized, ESP_ERR_INVALID_STATE, TAG, "session not initialized");
    *result = CYD_TEXT_INPUT_RESULT_CONTINUE;

    if (event == NULL) {
        TickType_t now = xTaskGetTickCount();
        if ((now - s_session.last_cursor_tick) >= pdMS_TO_TICKS(TEXT_INPUT_CURSOR_BLINK_MS)) {
            s_session.cursor_visible = !s_session.cursor_visible;
            s_session.last_cursor_tick = now;
            return text_input_render();
        }
        return ESP_OK;
    }

    uint16_t action = 0;
    if (!text_input_confirmed_action(event, &action)) {
        return ESP_OK;
    }
    size_t len = strlen(s_session.value);
    if (action == TEXT_INPUT_CANCEL) {
        s_session.initialized = false;
        *result = CYD_TEXT_INPUT_RESULT_CANCELLED;
        return ESP_OK;
    }
    if (action == TEXT_INPUT_SAVE) {
        if (text_out != NULL && text_out_size > 0) {
            snprintf(text_out, text_out_size, "%s", s_session.value);
        }
        s_session.initialized = false;
        *result = CYD_TEXT_INPUT_RESULT_SAVED;
        return ESP_OK;
    }
    if (action == TEXT_INPUT_DELETE && len > 0) {
        s_session.value[len - 1] = '\0';
    } else if (action == TEXT_INPUT_SPACE && len < s_session.max_len) {
        s_session.value[len] = ' ';
        s_session.value[len + 1] = '\0';
    } else if (action == TEXT_INPUT_CASE) {
        if (s_session.page == TEXT_INPUT_PAGE_SYMBOL || s_session.page == TEXT_INPUT_PAGE_SYMBOL_EXTRA) {
            s_session.page = TEXT_INPUT_PAGE_LOWER;
        } else {
            s_session.page = s_session.page == TEXT_INPUT_PAGE_UPPER ? TEXT_INPUT_PAGE_LOWER : TEXT_INPUT_PAGE_UPPER;
        }
    } else if (action == TEXT_INPUT_SYMBOL) {
        s_session.page = s_session.page == TEXT_INPUT_PAGE_SYMBOL ? TEXT_INPUT_PAGE_SYMBOL_EXTRA : TEXT_INPUT_PAGE_SYMBOL;
    } else if (action == TEXT_INPUT_SHOW && s_session.obscure_input) {
        s_session.show_value = !s_session.show_value;
    } else if (action == TEXT_INPUT_URL_SCHEME && s_session.mode == CYD_TEXT_INPUT_MODE_URL) {
        if (s_session.url_scheme_step == 0) {
            snprintf(s_session.url_restore, sizeof(s_session.url_restore), "%s", s_session.value);
        }
        static const char *schemes[] = { "http://", "https://" };
        const char *replacement = s_session.url_scheme_step < 2
            ? schemes[s_session.url_scheme_step]
            : s_session.url_restore;
        snprintf(s_session.value, sizeof(s_session.value), "%s", replacement);
        s_session.value[s_session.max_len] = '\0';
        s_session.url_scheme_step = (uint8_t)((s_session.url_scheme_step + 1U) % 3U);
    } else if (action >= TEXT_INPUT_KEY_BASE && action <= TEXT_INPUT_KEY_BASE + 0x7f && len < s_session.max_len) {
        s_session.value[len] = (char)(action - TEXT_INPUT_KEY_BASE);
        s_session.value[len + 1] = '\0';
    }

    s_session.cursor_visible = true;
    s_session.last_cursor_tick = xTaskGetTickCount();
    if (action != TEXT_INPUT_URL_SCHEME) {
        s_session.url_scheme_step = 0;
        s_session.url_restore[0] = '\0';
    }
    return text_input_render();
}
