#ifndef CYD_TEXT_INPUT_H
#define CYD_TEXT_INPUT_H

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "cyd_input.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CYD_TEXT_INPUT_MAX_LEN 127U

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
    const char *initial_text;
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
