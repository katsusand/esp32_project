#pragma once

#include <stdbool.h>
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
/*
 * Closes the stream and drops later lines until error_log_store_start() runs
 * again. For a card that is going away (unmount) or is full; lines already
 * queued are flushed first when the card still works.
 *
 * English contract: safe to call while other tasks log. After it returns no
 * producer holds the stream, so the card can be unmounted. Start and stop must
 * be called from one task.
 */
esp_err_t error_log_store_stop(void);
/*
 * True once the card writer gave up on the error log stream (write error, card
 * pulled, no space). Lines are dropped from then on. Only stop followed by
 * start opens a stream that can write again.
 */
bool error_log_store_is_failed(void);
esp_err_t error_log_store_write_error_log(const char *line);
esp_err_t error_log_store_append_message(const char *tag, const char *message);
esp_err_t error_log_store_append_esp_err(const char *tag, const char *message, esp_err_t err);

#ifdef __cplusplus
}
#endif
