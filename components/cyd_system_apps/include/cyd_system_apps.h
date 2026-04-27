#ifndef CYD_SYSTEM_APPS_H
#define CYD_SYSTEM_APPS_H

#include "app_shell.h"

#ifdef __cplusplus
extern "C" {
#endif

const app_shell_app_t *cyd_info_app_get_app(void);

const app_shell_app_t *cyd_settings_app_get_app(void);

#ifdef __cplusplus
}
#endif

#endif
