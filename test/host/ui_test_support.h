/*
 * Shared helpers for host tests of the Japanese UI: a pass/fail counter, the
 * cyd_display port hooks, and a layout check that measures every label of a
 * built screen against its box with the real font tables.
 *
 * A test program links ui_test_support.c, cyd_display_text.c, cyd_ui.c,
 * cyd_ui_fonts.c and the generated font tables, plus the views it checks.
 */
#ifndef UI_TEST_SUPPORT_H
#define UI_TEST_SUPPORT_H

#include <stdbool.h>
#include "cyd_display.h"

/* Counts one check and prints it. */
void ui_test_check(bool ok, const char *what);

/* Counts one failure without printing (for a caller that printed details). */
void ui_test_fail(void);

/*
 * Whether cyd_display_port_text_is_immutable() reports every string as
 * immutable. Set it to true before building views so that literals behave as
 * they do on the device (referenced, never cut at 40 bytes).
 */
void ui_test_set_all_text_immutable(bool immutable);

/*
 * Checks that `screen` fits the widget buffer and that every anti-aliased
 * label fits its box without the renderer shrinking or ellipsizing it.
 * Wording that only fits after shrinking is a layout bug.
 */
void ui_test_check_screen(const char *name, const cyd_display_screen_t *screen);

/* ui_test_check_screen() without printing or counting, for long sweeps. */
bool ui_test_screen_fits(const cyd_display_screen_t *screen);

/* Prints the summary; returns the process exit code. */
int ui_test_finish(void);

#endif
