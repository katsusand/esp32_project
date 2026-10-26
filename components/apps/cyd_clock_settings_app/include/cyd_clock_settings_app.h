#ifndef CYD_CLOCK_SETTINGS_APP_H
#define CYD_CLOCK_SETTINGS_APP_H

#include "app_shell.h"

#ifdef __cplusplus
extern "C" {
#endif

const app_shell_app_t *cyd_clock_settings_app_get_app(void);

/*
 * Applies the clock's saved choices that act outside this screen: whether the
 * clock's info button asks for the settings PIN (the "情報画面" page). Called
 * once from cyd_clock_app_register(). The choice is stored in app scope
 * ("app_clock"), so it goes with the clock, and Clear App Data clears it.
 */
esp_err_t cyd_clock_settings_app_init(void);

/*
 * There is no register function here on purpose. This screen is registered as
 * `cyd_clock_app`'s `settings_app`, so it cannot appear without the clock and
 * never shows up in the launcher as a peer app.
 */

#ifdef __cplusplus
}
#endif

#endif
