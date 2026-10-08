#ifndef CYD_TEXT_INPUT_VIEW_H
#define CYD_TEXT_INPUT_VIEW_H

/*
 * The keyboard screen of cyd_text_input, built from a model only.
 *
 * cyd_text_input.c owns the session (typing, paging, the cursor blink) and
 * fills a cyd_text_input_view_model_t; this file turns the model into widgets.
 * Keeping the two apart lets the simulator and the host tests build every
 * keyboard state by hand. Apps use cyd_text_input.h, not this header.
 *
 * English contract: cyd_text_input_view_build() calls no service, reads no
 * clock and touches no global state. Every string in the model is only read
 * while building, so the model may point at stack buffers.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "cyd_display.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Action ids of the keyboard screen. A character key is KEY_BASE + its ASCII code. */
#define CYD_TEXT_INPUT_VIEW_ACTION_KEY_BASE 0x5200
#define CYD_TEXT_INPUT_VIEW_ACTION_DELETE 0x5301
#define CYD_TEXT_INPUT_VIEW_ACTION_SPACE 0x5302
#define CYD_TEXT_INPUT_VIEW_ACTION_CANCEL 0x5303
#define CYD_TEXT_INPUT_VIEW_ACTION_SAVE 0x5304
#define CYD_TEXT_INPUT_VIEW_ACTION_SYMBOL 0x5305
#define CYD_TEXT_INPUT_VIEW_ACTION_CASE 0x5306
#define CYD_TEXT_INPUT_VIEW_ACTION_SHOW 0x5307
#define CYD_TEXT_INPUT_VIEW_ACTION_URL_SCHEME 0x5308

typedef enum {
    CYD_TEXT_INPUT_VIEW_PAGE_LOWER = 0,
    CYD_TEXT_INPUT_VIEW_PAGE_UPPER,
    CYD_TEXT_INPUT_VIEW_PAGE_SYMBOL,
    CYD_TEXT_INPUT_VIEW_PAGE_SYMBOL_EXTRA,
} cyd_text_input_view_page_t;

/* The optional button in the bottom row. */
typedef enum {
    CYD_TEXT_INPUT_VIEW_EXTRA_NONE = 0,
    CYD_TEXT_INPUT_VIEW_EXTRA_SHOW_TOGGLE, /* password: show or hide the value */
    CYD_TEXT_INPUT_VIEW_EXTRA_URL_SCHEME,  /* URL: http:// -> https:// -> undo */
} cyd_text_input_view_extra_t;

typedef struct {
    const char *title;         /* header; may be empty */
    const char *context_label; /* e.g. "SSID"; the context line is hidden when both are empty */
    const char *context_value;
    const char *input_label;   /* shown before the value; may be empty */
    /* The value as the operator should see it: formatted, and masked with '*'
       when hidden. Any length; the view keeps its tail visible. */
    const char *value;
    bool cursor_visible;
    cyd_text_input_view_page_t page;
    bool force_upper;
    bool digits_only;
    cyd_text_input_view_extra_t extra;
    bool show_value;         /* SHOW_TOGGLE: the value is currently shown */
    uint8_t url_scheme_step; /* URL_SCHEME: 0 http://, 1 https://, 2 undo */
} cyd_text_input_view_model_t;

/* The keys of one of the three key rows ("" for an unused row). */
const char *cyd_text_input_view_key_row(const cyd_text_input_view_model_t *model, size_t row);

/* Clears `screen` and builds the keyboard screen for `model`. */
void cyd_text_input_view_build(cyd_display_screen_t *screen, const cyd_text_input_view_model_t *model);

#ifdef __cplusplus
}
#endif

#endif
