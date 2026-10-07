#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Error log file names, and which file today's lines go to. No SDK in here, so
 * it can be tested on the host.
 *
 *   ERR_2026-10-07_01.LOG
 *       \________/ \/
 *          date    number within that date
 *
 * The date is the local date. Before the clock is set it is 1970-01-01, and that
 * is the point: a file named that way says the clock was not set when it was
 * started.
 *
 * English contract: the number is at least two digits and grows past 99 instead
 * of stopping there, because a unit whose clock never gets set starts every boot
 * in the same 1970-01-01 bucket. It ends at ERROR_LOG_NAMING_MAX_SEQUENCE.
 * Names are matched case-insensitively when scanning, so a card that was renamed
 * on a PC still counts; names are always written in upper case. The older
 * "ERR_0001.LOG" files do not match and are left alone.
 */
#define ERROR_LOG_NAME_MAX 40U
#define ERROR_LOG_NAMING_MAX_SEQUENCE 9999U

typedef struct {
    int year;
    int month; /* 1..12 */
    int day;   /* 1..31 */
} error_log_date_t;

/* "ERR_2026-10-07_01.LOG". False when `sequence` is 0 or past the maximum, or `out` is too small. */
bool error_log_naming_build(char *out, size_t out_size, const error_log_date_t *date, unsigned sequence);

/* Reads a file name written by error_log_naming_build(). False for anything else. */
bool error_log_naming_parse(const char *name, error_log_date_t *date, unsigned *sequence);

bool error_log_naming_same_date(const error_log_date_t *a, const error_log_date_t *b);

/* "2026-10-07", for comparing the date of the open file with today's. */
void error_log_naming_date_text(char *out, size_t out_size, const error_log_date_t *date);

/* What a scan of the card found for today's date. */
typedef struct {
    bool found;                /* at least one file for this date */
    unsigned max_sequence;     /* the highest number among them */
    uint64_t max_sequence_size; /* that file's size in bytes */
} error_log_scan_t;

typedef struct {
    unsigned sequence;
    bool append; /* the file exists and has room: add to it instead of creating it */
} error_log_target_t;

/*
 * Today's file: none yet starts number 1; the newest one with room (smaller than
 * `max_file_size`) is appended to; a full one is followed by the next number.
 * False when the numbers are used up, so no file can be opened for today.
 *
 * "Room" is about the file, not the card: the card running out of space is
 * sd_card_status' FULL.
 *
 * English contract: pass a `max_file_size` a little below the real limit. The
 * writer ends a file when the next line would not fit, which leaves it just
 * under the limit; judged against the limit itself that file would still have
 * "room" and be appended to again.
 */
bool error_log_naming_resolve(const error_log_scan_t *scan, uint64_t max_file_size, error_log_target_t *out);
