#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t sd_card_storage_init(void);
bool sd_card_storage_is_mounted(void);
const char *sd_card_storage_get_mount_point(void);

#ifdef __cplusplus
}
#endif
