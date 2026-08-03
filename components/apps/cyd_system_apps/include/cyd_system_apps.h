#ifndef CYD_SYSTEM_APPS_H
#define CYD_SYSTEM_APPS_H

#include "app_shell.h"

#ifdef __cplusplus
extern "C" {
#endif

const app_shell_app_t *system_info_app_get_app(void);

const app_shell_app_t *system_settings_app_get_app(void);

const app_shell_app_t *system_touch_calibration_app_get_app(void);

void system_settings_open_stored_ssids(void);
void system_settings_open_clear_touch_calib_confirm(void);
void system_settings_open_clear_nvs_confirm(void);

/*
 * App-specific screens are reached through `app_registry`, not through a
 * setter here. The settings app grows an APPS page listing every registered
 * app, so any number of apps can contribute an entry.
 *
 * English supplement: this replaced a single extension slot that allowed
 * exactly one app to extend settings. Register with app_registry_register().
 */

#ifdef __cplusplus
}
#endif

#endif
