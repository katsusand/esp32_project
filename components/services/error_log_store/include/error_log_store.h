#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The shared error log: any component records an error by saying what failed and
 * with what code, and the line lands on the SD card in one format.
 *
 *     ERROR_LOG(err, "connect failed (%s)", reason);
 *     ERROR_LOG_MSG("card %s was refused", uid);
 *
 * writes, to the card and to serial:
 *
 *     [2026-10-07 12:34:56 +12345ms] wifi_connection connect_task:312 ESP_ERR_TIMEOUT(0x107): connect failed (AP not found)
 *
 * English contract:
 *  - The macros use the file's own `TAG` as the app or component name, and add
 *    `__func__` and `__LINE__`. Define TAG as the component's name, as the rest
 *    of the code base does.
 *  - They never fail and never wait for the card: the line is queued, or held in
 *    RAM when it cannot be written yet. Do not call them from an interrupt.
 *  - The message is printf style and limited to ERROR_LOG_LINE_MAX bytes in all;
 *    a longer one is cut and ends in "...".
 *  - The same event repeating is written once, then a single line says how many
 *    were left out (see error_log_dedupe.h).
 *  - Files are `ERR_<date>_<nn>.LOG`, one date's lines appended to the same file
 *    until it reaches 256 KB, and every file or appended section starts with a
 *    `boot:` line carrying the boot id (see docs/error_log_store.md).
 */
#define ERROR_LOG(err, ...) error_log_store_report(TAG, __func__, __LINE__, (err), __VA_ARGS__)
#define ERROR_LOG_MSG(...) error_log_store_report(TAG, __func__, __LINE__, ESP_OK, __VA_ARGS__)

/*
 * Records one error. With `err` ESP_OK there is no code field. Also writes the
 * line to the serial log at error level. Prefer the macros.
 */
void error_log_store_report(const char *tag, const char *func, int line, esp_err_t err, const char *fmt, ...)
    __attribute__((format(printf, 5, 6)));

/*
 * How the question "is the clock right?" ended. Lines wait in RAM until it is
 * answered (and the card is ready), so that a file is never started before it is
 * known whether its date is real.
 *
 * English contract: time_sync reports SYNCED when a sync worked and FAILED when
 * a request ended without one (no connection, or NTP gave up). A build that has
 * no time source says UNAVAILABLE at start so nothing waits for it. The first
 * answer counts; so does a clock that is already valid, and so does
 * CONFIG_ERROR_LOG_STORE_TIME_WAIT_SECONDS passing, which counts as FAILED.
 */
typedef enum {
    ERROR_LOG_TIME_SYNCED = 0,
    ERROR_LOG_TIME_FAILED,
    ERROR_LOG_TIME_UNAVAILABLE,
} error_log_time_outcome_t;

void error_log_store_notify_time_decided(error_log_time_outcome_t outcome);

/*
 * The card is ready: mounted, and sd_card_writer_start() has run. Lines held so
 * far are written as soon as the time question is answered too (at once if it
 * already is). Call it again after error_log_store_stop(). ESP_OK also when the
 * lines are still being held; an error only when the stream could not be opened.
 */
esp_err_t error_log_store_start(void);
/*
 * Closes the stream and holds later lines in RAM until error_log_store_start()
 * runs again. For a card that is going away (unmount) or is full; lines already
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

/*
 * The older entry points. They keep working and take the same path (held,
 * folded when repeated, one format), but do not write to serial: their callers
 * have their own serial line.
 */
esp_err_t error_log_store_write_error_log(const char *line);
esp_err_t error_log_store_append_message(const char *tag, const char *message);
esp_err_t error_log_store_append_esp_err(const char *tag, const char *message, esp_err_t err);

#ifdef __cplusplus
}
#endif
