#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Opens the error log stream on the SD card writer (sd_card_writer_start()
 * first). Until then, and without a card, lines are dropped. The file itself
 * is created on the first line, so an error-free boot leaves no file behind.
 */
esp_err_t error_log_store_start(void);
esp_err_t error_log_store_write_error_log(const char *line);
esp_err_t error_log_store_append_message(const char *tag, const char *message);
esp_err_t error_log_store_append_esp_err(const char *tag, const char *message, esp_err_t err);

#ifdef __cplusplus
}
#endif
