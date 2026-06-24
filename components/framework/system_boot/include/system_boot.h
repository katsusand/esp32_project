#ifndef SYSTEM_BOOT_H
#define SYSTEM_BOOT_H

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool setup_shortcut_requested;
} system_boot_result_t;

esp_err_t system_boot_start(system_boot_result_t *result);

#ifdef __cplusplus
}
#endif

#endif
