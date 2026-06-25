#pragma once

#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t sd_card_files_write_new_text(const char *relative_path, const char *text);
esp_err_t sd_card_files_append_existing_text(const char *relative_path, const char *text);
esp_err_t sd_card_files_overwrite_text(const char *relative_path, const char *text);

esp_err_t sd_card_files_write_new_binary(const char *relative_path, const void *data, size_t size);
esp_err_t sd_card_files_append_existing_binary(const char *relative_path, const void *data, size_t size);
esp_err_t sd_card_files_overwrite_binary(const char *relative_path, const void *data, size_t size);

#ifdef __cplusplus
}
#endif
