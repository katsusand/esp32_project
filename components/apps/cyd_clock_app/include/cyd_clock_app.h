#ifndef CYD_CLOCK_APP_H
#define CYD_CLOCK_APP_H

#include "app_shell.h"

#ifdef __cplusplus
extern "C" {
#endif

const app_shell_app_t *cyd_clock_app_get_app(void);

/*
 * Registers the clock with app_registry together with what belongs to it: its
 * settings screen and its alarms (cyd_clock_alarm). Call from the composition,
 * after app_scheduler_init().
 */
esp_err_t cyd_clock_app_register(void);

#ifdef __cplusplus
}
#endif

#endif
