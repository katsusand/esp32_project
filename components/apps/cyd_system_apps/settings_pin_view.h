#ifndef SETTINGS_PIN_VIEW_H
#define SETTINGS_PIN_VIEW_H

/*
 * The PIN screen: a large digits-only keypad, built from a model only.
 *
 * settings_pin_app.c decides what the screen shows; this file owns every word
 * on it and the key layout. The simulator and the host tests build it by hand.
 *
 * English contract: settings_pin_view_build() calls no service and touches no
 * global state. The model never carries the digits themselves, only how many
 * have been typed, so the screen cannot show a PIN even by mistake.
 */

#include <stdbool.h>
#include <stdint.h>
#include "cyd_display.h"

#ifdef __cplusplus
extern "C" {
#endif

/* + digit 0-9 */
#define SETTINGS_PIN_VIEW_ACTION_DIGIT_BASE 0x2500
#define SETTINGS_PIN_VIEW_ACTION_DELETE 0x250a
#define SETTINGS_PIN_VIEW_ACTION_OK 0x250b
#define SETTINGS_PIN_VIEW_ACTION_CANCEL 0x250c
#define SETTINGS_PIN_VIEW_ACTION_RECOVERY 0x250d

typedef enum {
    SETTINGS_PIN_VIEW_ENTER = 0,  /* type the PIN to get in */
    SETTINGS_PIN_VIEW_NEW_FIRST,  /* a new PIN, first time */
    SETTINGS_PIN_VIEW_NEW_SECOND, /* the same new PIN again */
    SETTINGS_PIN_VIEW_MESSAGE,    /* a notice without keys, e.g. "restarting" */
} settings_pin_view_screen_t;

typedef enum {
    SETTINGS_PIN_VIEW_NOTICE_NONE = 0,
    SETTINGS_PIN_VIEW_NOTICE_WRONG,      /* ENTER: the PIN did not match */
    SETTINGS_PIN_VIEW_NOTICE_MISMATCH,   /* NEW_FIRST: the two entries differed */
    SETTINGS_PIN_VIEW_NOTICE_SAVE_FAILED /* NEW_*: the new PIN could not be stored */
} settings_pin_view_notice_t;

typedef struct {
    settings_pin_view_screen_t screen;
    settings_pin_view_notice_t notice;
    const char *title;       /* header text, e.g. "設定" */
    uint8_t entered;         /* digits typed so far; shown as dots */
    uint32_t wait_seconds;   /* ENTER: > 0 while attempts are paused; keys are off */
    const char *recovery_label; /* ENTER: a second way in, e.g. a code from the server; NULL hides it */
    const char *message_title;  /* MESSAGE */
    const char *message_detail; /* MESSAGE */
} settings_pin_view_model_t;

/* "あと2分30秒お待ちください" and the like. */
void settings_pin_view_format_wait(char *out, size_t out_size, uint32_t seconds);

/* Clears `screen` and builds the screen the model selects. */
void settings_pin_view_build(cyd_display_screen_t *screen, const settings_pin_view_model_t *model);

#ifdef __cplusplus
}
#endif

#endif
