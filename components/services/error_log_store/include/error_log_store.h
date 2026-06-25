#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t error_log_store_append_message(const char *tag, const char *message);
esp_err_t error_log_store_append_esp_err(const char *tag, const char *message, esp_err_t err);

#ifdef __cplusplus
}
#endif
