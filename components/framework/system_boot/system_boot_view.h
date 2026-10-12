#ifndef SYSTEM_BOOT_VIEW_H
#define SYSTEM_BOOT_VIEW_H

/*
 * Screens system_boot shows before any app runs.
 *
 * The only one so far is the boot touch shortcut prompt: the panel was held
 * through boot, settings will open, and the finger should come off. It is
 * shown as soon as the display is up and before system_boot waits for the
 * release, so the user is never left holding a dark screen.
 *
 * English contract: the build functions call no service and touch no global
 * state, so the simulator and the host tests can build them by hand.
 */

#include "cyd_display.h"

#ifdef __cplusplus
extern "C" {
#endif

void system_boot_view_build_shortcut_prompt(cyd_display_screen_t *screen);

#ifdef __cplusplus
}
#endif

#endif
