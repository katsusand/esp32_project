#ifndef CYD_TEXT_INPUT_H
#define CYD_TEXT_INPUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "cyd_input.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CYD_TEXT_INPUT_MAX_LEN 127U
/* Groups in an auto-formatted value, e.g. 3 for YYYY-MM-DD. */
#define CYD_TEXT_INPUT_MAX_GROUPS 4U

typedef enum {
    CYD_TEXT_INPUT_MODE_GENERIC = 0,
    CYD_TEXT_INPUT_MODE_PASSWORD,
    CYD_TEXT_INPUT_MODE_URL,
} cyd_text_input_mode_t;

typedef struct {
    const char *title;
    const char *context_label;
    const char *context_value;
    const char *input_label;
    /*
     * Text the value always starts with. It is shown as part of the value,
     * DEL cannot remove it, and it is included in the saved result and in
     * `max_len`. Use it for constant key prefixes the operator should not have
     * to type -- or be able to break.
     */
    const char *fixed_prefix;
    const char *initial_text;
    /*
     * Upper-cases every typed character and locks the keyboard to its
     * upper-case page. For values whose alphabet is upper-case anyway, letting
     * the operator type lower case only creates a way to get it wrong.
     */
    bool force_upper;
    /*
     * Digits only: the keyboard shows 0-9 and nothing else, and the page and
     * space keys are disabled. For values like a date there is no other
     * character worth offering.
     */
    bool digits_only;
    /*
     * Auto-formatting for grouped values such as XXXX-XXXX or YYYY-MM-DD.
     * `auto_groups` lists the group lengths in order and the last one repeats,
     * so a single entry means uniform grouping. `auto_separator` is shown
     * between groups but never stored, so DEL stays a plain one-character
     * operation; the separators do count towards `max_len` and appear in the
     * saved result.
     */
    char auto_separator;
    const uint8_t *auto_groups;
    size_t auto_group_count;
    size_t max_len;
    bool obscure_input;
    cyd_text_input_mode_t mode;
} cyd_text_input_config_t;

typedef enum {
    CYD_TEXT_INPUT_RESULT_CONTINUE = 0,
    CYD_TEXT_INPUT_RESULT_CANCELLED,
    CYD_TEXT_INPUT_RESULT_SAVED,
} cyd_text_input_result_t;

esp_err_t cyd_text_input_begin_session(const cyd_text_input_config_t *config);
esp_err_t cyd_text_input_poll_session(const cyd_input_event_t *event,
                                      cyd_text_input_result_t *result,
                                      char *text_out,
                                      size_t text_out_size);

#ifdef __cplusplus
}
#endif

#endif
