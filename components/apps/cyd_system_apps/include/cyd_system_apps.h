#ifndef CYD_SYSTEM_APPS_H
#define CYD_SYSTEM_APPS_H

#include "app_shell.h"
#include "system_settings_fix.h"

#ifdef __cplusplus
extern "C" {
#endif

const app_shell_app_t *system_info_app_get_app(void);

const app_shell_app_t *system_settings_app_get_app(void);

const app_shell_app_t *system_touch_calibration_app_get_app(void);
/*
 * Opens touch calibration over whatever is on screen, and afterwards puts
 * that screen back as it was entered (its own back destination intact).
 * Safe from any task: it only queues the switch. Does nothing while
 * calibration is already active.
 *
 * English contract: use this for a shortcut that can fire on any screen (the
 * BOOT button hold). Opening calibration with app_shell_switch_to_from(cal,
 * active) instead made the interrupted app treat calibration as where it
 * came from, so its back button reopened calibration.
 */
esp_err_t system_touch_calibration_open_over_active(void);

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
