#ifndef CYD_CLOCK_APP_H
#define CYD_CLOCK_APP_H

#include "app_shell.h"

#ifdef __cplusplus
extern "C" {
#endif

const app_shell_app_t *cyd_clock_app_get_app(void);

/* See cyd_clock_settings_app_register(): call from the composition. */
esp_err_t cyd_clock_app_register(void);

#ifdef __cplusplus
}
#endif

#endif
