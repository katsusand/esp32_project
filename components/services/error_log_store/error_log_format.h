#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

/*
 * How one error log line is written. No SDK in here, so it can be tested on the
 * host.
 *
 *   [2026-10-07 12:34:56 +12345ms] time_sync time_sync_task:493 ESP_ERR_TIMEOUT(0x107): time sync failed
 *   \_________ stamp ____________/ \______________________ body ____________________________________/
 *
 * The stamp is the local wall clock plus the milliseconds since boot. Before the
 * clock is set the wall clock reads 1970-01-01, and the uptime still orders the
 * lines. The body is `tag[ func:line][ CODE(0xNNN)][: message]`.
 *
 * English contract: a line is one line. Control characters in the message become
 * spaces, so a message cannot split a record in two. A line that does not fit is
 * cut and ends in "..." rather than being dropped, so the record of that error
 * still exists. A line is never longer than ERROR_LOG_LINE_MAX bytes, newline
 * and terminating NUL included.
 */
#define ERROR_LOG_LINE_MAX 256U

typedef struct {
    const char *tag;       /* the app or component; NULL reads as "?" */
    const char *func;      /* NULL when the caller did not say */
    int line;              /* ignored without func */
    bool has_code;
    int32_t code;          /* an esp_err_t */
    const char *code_name; /* esp_err_to_name(code); NULL reads as "ERR" */
    const char *message;   /* NULL reads as empty */
    bool message_cut;      /* the caller's buffer already cut the message short */
} error_log_fields_t;

/* "[2026-10-07 12:34:56 +12345ms]". Returns its length, 0 when `out` is too small. */
size_t error_log_format_stamp(char *out, size_t out_size, const struct tm *local, uint64_t uptime_ms);

/* The body, without stamp or newline. Cut with "..." when it does not fit. Returns its length. */
size_t error_log_format_body(char *out, size_t out_size, const error_log_fields_t *fields);

/* stamp + ' ' + body + '\n', cut with "..." so that it fits. Returns its length. */
size_t error_log_format_line(char *out, size_t out_size, const char *stamp, const char *body);

/*
 * The wall clock a line had when it was written, worked out later from the
 * uptime it carries: now minus how long ago it was. This is what lets a line
 * held in RAM before the clock was set get its real date once the clock is.
 */
time_t error_log_restamp(time_t now_wall, uint64_t now_uptime_ms, uint64_t entry_uptime_ms);
