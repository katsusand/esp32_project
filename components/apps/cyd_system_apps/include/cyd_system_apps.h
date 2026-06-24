#ifndef CYD_SYSTEM_APPS_H
#define CYD_SYSTEM_APPS_H

#include "app_shell.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *label;
    const app_shell_app_t *app;
} system_settings_extension_t;

const app_shell_app_t *system_info_app_get_app(void);

const app_shell_app_t *system_settings_app_get_app(void);

const app_shell_app_t *system_touch_calibration_app_get_app(void);

void system_settings_open_stored_ssids(void);
void system_settings_open_clear_touch_calib_confirm(void);
void system_settings_open_clear_nvs_confirm(void);

void system_settings_set_extension(const system_settings_extension_t *extension);

#ifdef __cplusplus
}
#endif

#endif
