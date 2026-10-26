#ifndef SETTINGS_PIN_INTERNAL_H
#define SETTINGS_PIN_INTERNAL_H

/*
 * What the settings app and the PIN screen tell each other inside
 * cyd_system_apps. Not for products: they use settings_pin.h.
 */

#include <stdbool.h>
#include "esp_err.h"
#include "app_shell.h"

/* The settings page: start setting (or changing) the PIN. Returns to settings. */
esp_err_t settings_pin_begin_new_pin(void);
/* The settings page: remove the PIN and the failure count. */
esp_err_t settings_pin_turn_off(void);

bool settings_pin_has_recovery(void);

/*
 * Called from settings' step(). When the PIN is entered and the idle timeout
 * has passed, closes it again and shows the PIN screen in front of `self`.
 * This covers a device that booted into settings, where settings is the home
 * app and the shell's idle return has nowhere to go. True when it switched.
 */
bool settings_pin_relock_if_idle(const app_shell_app_t *self);

/*
 * Called first in settings' enter(). Normally the enter guard has already
 * decided and this returns false. If the product never called
 * settings_pin_init(), it installs the guard now, logs an error, and diverts
 * this entry to the PIN screen when needed (returning true: enter() must stop).
 */
bool settings_pin_divert_unguarded(const app_shell_app_t *self, const app_shell_app_t *from_app);

/* settings app side, implemented in system_settings_app.c */
bool system_settings_forced_initialize_armed(void);
void system_settings_discard_direct_view(void);

#endif
