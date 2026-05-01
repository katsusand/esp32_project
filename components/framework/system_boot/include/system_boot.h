#ifndef SYSTEM_BOOT_H
#define SYSTEM_BOOT_H

#include "esp_err.h"
#include "app_shell.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t system_boot_start(const app_shell_app_t *home_app);

#ifdef __cplusplus
}
#endif

#endif
