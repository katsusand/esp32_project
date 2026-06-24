#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "nvs_schema.h"

#ifdef __cplusplus
extern "C" {
#endif

void nvs_health_reset(void);
void nvs_health_report_invalid(const nvs_key_descriptor_t *key, esp_err_t err, const char *reason);
bool nvs_health_requires_initialize(void);
size_t nvs_health_get_issue_count(void);
const char *nvs_health_get_summary(void);

#ifdef __cplusplus
}
#endif
