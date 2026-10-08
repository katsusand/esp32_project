#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "cyd_display.h"
#include "cyd_text_input.h"
#include "cyd_text_input_view.h"
#include "cyd_ui.h"

#define TAG "cyd_text_input"

#define TEXT_INPUT_KEY_BASE CYD_TEXT_INPUT_VIEW_ACTION_KEY_BASE
#define TEXT_INPUT_DELETE CYD_TEXT_INPUT_VIEW_ACTION_DELETE
#define TEXT_INPUT_SPACE CYD_TEXT_INPUT_VIEW_ACTION_SPACE
#define TEXT_INPUT_CANCEL CYD_TEXT_INPUT_VIEW_ACTION_CANCEL
#define TEXT_INPUT_SAVE CYD_TEXT_INPUT_VIEW_ACTION_SAVE
#define TEXT_INPUT_SYMBOL CYD_TEXT_INPUT_VIEW_ACTION_SYMBOL
#define TEXT_INPUT_CASE CYD_TEXT_INPUT_VIEW_ACTION_CASE
#define TEXT_INPUT_SHOW CYD_TEXT_INPUT_VIEW_ACTION_SHOW
#define TEXT_INPUT_URL_SCHEME CYD_TEXT_INPUT_VIEW_ACTION_URL_SCHEME
#define TEXT_INPUT_CURSOR_BLINK_MS 500

#define TEXT_INPUT_PAGE_LOWER CYD_TEXT_INPUT_VIEW_PAGE_LOWER
#define TEXT_INPUT_PAGE_UPPER CYD_TEXT_INPUT_VIEW_PAGE_UPPER
#define TEXT_INPUT_PAGE_SYMBOL CYD_TEXT_INPUT_VIEW_PAGE_SYMBOL
#define TEXT_INPUT_PAGE_SYMBOL_EXTRA CYD_TEXT_INPUT_VIEW_PAGE_SYMBOL_EXTRA
typedef cyd_text_input_view_page_t text_input_page_t;

typedef struct {
    bool pending;
    bool long_pressed;
    uint16_t action_id;
} text_input_touch_tracker_t;

typedef struct {
    bool initialized;
    /* Copied on a character boundary: Japanese is 3 bytes per character. */
    char title[CYD_DISPLAY_TEXT_MAX_LEN + 1];
    char context_label[CYD_DISPLAY_TEXT_MAX_LEN + 1];
    char context_value[CYD_DISPLAY_TEXT_MAX_LEN + 1];
    char input_label[CYD_DISPLAY_TEXT_MAX_LEN + 1];
    char value[CYD_TEXT_INPUT_MAX_LEN + 1];
    size_t fixed_prefix_len;
    bool force_upper;
    bool digits_only;
    char auto_separator;
    uint8_t auto_groups[CYD_TEXT_INPUT_MAX_GROUPS];
    size_t auto_group_count;
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
        s_session.touch.pending = cyd_display_screen_hit_test(&s_screen,
                                                              event->data.touch.x,
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
                         cyd_display_screen_hit_test(&s_screen,
                                                     event->data.touch.x,
                                                     event->data.touch.y,
                                                     &released) &&
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

/*
 * Separators are presentation only: the value holds exactly what was typed.
 * That keeps DEL a plain one-character operation and means the caller never has
 * to strip formatting back out.
 *
 * The formatted length is prefix + typed characters + one separator per
 * completed group, with no trailing separator once the value is full.
 */
static size_t text_input_group_len(size_t group_index)
{
    if (s_session.auto_group_count == 0U) {
        return 0U;
    }
    if (group_index >= s_session.auto_group_count) {
        group_index = s_session.auto_group_count - 1U;
    }
    return s_session.auto_groups[group_index];
}

/* Separators shown before the `typed`-th character, i.e. group boundaries the
   value has already passed. */
static size_t text_input_separator_count(size_t typed)
{
    size_t separators = 0;
    size_t consumed = 0;

    if (s_session.auto_group_count == 0U) {
        return 0U;
    }
    for (size_t group = 0; consumed < typed; ++group) {
        size_t len = text_input_group_len(group);

        if (len == 0U || typed <= consumed + len) {
            break;
        }
        consumed += len;
        separators++;
    }
    return separators;
}

static size_t text_input_formatted_len(size_t typed)
{
    return s_session.fixed_prefix_len + typed + text_input_separator_count(typed);
}

/* How many characters the value can hold once separators are accounted for. */
static size_t text_input_value_capacity(void)
{
    size_t typed = 0;

    if (s_session.auto_group_count == 0U) {
        return s_session.max_len;
    }
    while (text_input_formatted_len(typed + 1U) <= s_session.max_len) {
        typed++;
    }
    return s_session.fixed_prefix_len + typed;
}

/*
 * A completed group shows its separator immediately, so the operator sees the
 * grouping while typing. It is dropped again when the character before it is
 * deleted, and never shown once no further character fits.
 */
static void text_input_format_value(char *dst, size_t dst_size)
{
    size_t out = 0;
    size_t typed = 0;
    size_t group = 0;
    size_t in_group = 0;
    const char *value = s_session.value;

    for (size_t i = 0; value[i] != '\0' && out + 1U < dst_size; ++i) {
        if (i >= s_session.fixed_prefix_len && s_session.auto_group_count > 0U) {
            if (in_group == text_input_group_len(group)) {
                dst[out++] = s_session.auto_separator;
                group++;
                in_group = 0;
                if (out + 1U >= dst_size) {
                    break;
                }
            }
            in_group++;
            typed++;
        }
        dst[out++] = value[i];
    }
    /* A completed group shows its separator right away, so the grouping is
       visible while typing -- but not once no further character fits. */
    if (s_session.auto_group_count > 0U && typed > 0U &&
        in_group == text_input_group_len(group) &&
        text_input_formatted_len(typed + 1U) <= s_session.max_len &&
        out + 1U < dst_size) {
        dst[out++] = s_session.auto_separator;
    }
    dst[out] = '\0';
}

/* The value as shown: formatted, and masked while a password is hidden. */
static void text_input_visible_value(char *dst, size_t dst_size)
{
    char formatted[CYD_TEXT_INPUT_MAX_LEN + 1] = { 0 };

    text_input_format_value(formatted, sizeof(formatted));
    if (s_session.obscure_input && !s_session.show_value) {
        size_t len = strlen(formatted);
        if (len >= dst_size) {
            len = dst_size - 1U;
        }
        memset(dst, '*', len);
        dst[len] = '\0';
        return;
    }
    snprintf(dst, dst_size, "%s", formatted);
}

static esp_err_t text_input_render(void)
{
    char visible[CYD_TEXT_INPUT_MAX_LEN + 1] = { 0 };
    cyd_text_input_view_extra_t extra = CYD_TEXT_INPUT_VIEW_EXTRA_NONE;

    text_input_visible_value(visible, sizeof(visible));
    if (s_session.obscure_input) {
        extra = CYD_TEXT_INPUT_VIEW_EXTRA_SHOW_TOGGLE;
    } else if (s_session.mode == CYD_TEXT_INPUT_MODE_URL) {
        extra = CYD_TEXT_INPUT_VIEW_EXTRA_URL_SCHEME;
    }

    const cyd_text_input_view_model_t model = {
        .title = s_session.title,
        .context_label = s_session.context_label,
        .context_value = s_session.context_value,
        .input_label = s_session.input_label,
        .value = visible,
        .cursor_visible = s_session.cursor_visible,
        .page = s_session.page,
        .force_upper = s_session.force_upper,
        .digits_only = s_session.digits_only,
        .extra = extra,
        .show_value = s_session.show_value,
        .url_scheme_step = s_session.url_scheme_step,
    };
    cyd_text_input_view_build(&s_screen, &model);
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
    s_session.force_upper = config->force_upper;
    s_session.digits_only = config->digits_only;
    s_session.auto_separator = config->auto_separator;
    if (config->auto_separator != '\0' && config->auto_groups != NULL) {
        ESP_RETURN_ON_FALSE(config->auto_group_count > 0U &&
                                config->auto_group_count <= CYD_TEXT_INPUT_MAX_GROUPS,
                            ESP_ERR_INVALID_ARG, TAG, "invalid auto group count");
        for (size_t i = 0; i < config->auto_group_count; ++i) {
            ESP_RETURN_ON_FALSE(config->auto_groups[i] > 0U,
                                ESP_ERR_INVALID_ARG, TAG, "auto group length is zero");
            s_session.auto_groups[i] = config->auto_groups[i];
        }
        s_session.auto_group_count = config->auto_group_count;
    }
    if (s_session.force_upper) {
        s_session.page = TEXT_INPUT_PAGE_UPPER;
    }
    (void)cyd_display_utf8_copy(s_session.title, sizeof(s_session.title), config->title);
    (void)cyd_display_utf8_copy(s_session.context_label, sizeof(s_session.context_label), config->context_label);
    (void)cyd_display_utf8_copy(s_session.context_value, sizeof(s_session.context_value), config->context_value);
    (void)cyd_display_utf8_copy(s_session.input_label, sizeof(s_session.input_label), config->input_label);
    const char *fixed_prefix = config->fixed_prefix != NULL ? config->fixed_prefix : "";
    const char *initial_text = config->initial_text != NULL ? config->initial_text : "";

    s_session.fixed_prefix_len = strlen(fixed_prefix);
    ESP_RETURN_ON_FALSE(s_session.fixed_prefix_len < config->max_len,
                        ESP_ERR_INVALID_ARG, TAG, "fixed prefix does not fit in max len");
    /* The prefix is part of the value, so an initial text that already carries
       it is not doubled. */
    if (strncmp(initial_text, fixed_prefix, s_session.fixed_prefix_len) == 0) {
        snprintf(s_session.value, sizeof(s_session.value), "%s", initial_text);
    } else {
        snprintf(s_session.value, sizeof(s_session.value), "%s%s", fixed_prefix, initial_text);
    }
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
            /* The caller gets what the operator saw, separators included: for a
               date the grouping IS the format. A trailing separator is dropped
               because it belongs to a group that was never typed. */
            char formatted[CYD_TEXT_INPUT_MAX_LEN + 1] = { 0 };
            size_t formatted_len = 0;

            text_input_format_value(formatted, sizeof(formatted));
            formatted_len = strlen(formatted);
            if (s_session.auto_group_count > 0U && formatted_len > 0U &&
                formatted[formatted_len - 1U] == s_session.auto_separator) {
                formatted[formatted_len - 1U] = '\0';
            }
            snprintf(text_out, text_out_size, "%s", formatted);
        }
        s_session.initialized = false;
        *result = CYD_TEXT_INPUT_RESULT_SAVED;
        return ESP_OK;
    }
    if (action == TEXT_INPUT_DELETE && len > s_session.fixed_prefix_len) {
        s_session.value[len - 1] = '\0';
    } else if (action == TEXT_INPUT_SPACE && len < text_input_value_capacity()) {
        s_session.value[len] = ' ';
        s_session.value[len + 1] = '\0';
    } else if (action == TEXT_INPUT_CASE) {
        /* With the case locked the button still has a job: it is the way back
           from the symbol pages. */
        if (s_session.force_upper) {
            s_session.page = TEXT_INPUT_PAGE_UPPER;
        } else if (s_session.page == TEXT_INPUT_PAGE_SYMBOL || s_session.page == TEXT_INPUT_PAGE_SYMBOL_EXTRA) {
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
    } else if (action >= TEXT_INPUT_KEY_BASE && action <= TEXT_INPUT_KEY_BASE + 0x7f &&
               len < text_input_value_capacity()) {
        char ch = (char)(action - TEXT_INPUT_KEY_BASE);

        if (s_session.force_upper) {
            ch = (char)toupper((unsigned char)ch);
        }
        s_session.value[len] = ch;
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
