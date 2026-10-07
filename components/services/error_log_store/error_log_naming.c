#include "error_log_naming.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define PREFIX "ERR_"
#define SUFFIX ".LOG"

bool error_log_naming_build(char *out, size_t out_size, const error_log_date_t *date, unsigned sequence)
{
    if (sequence == 0U || sequence > ERROR_LOG_NAMING_MAX_SEQUENCE) {
        return false;
    }
    int written = snprintf(out,
                           out_size,
                           PREFIX "%04d-%02d-%02d_%02u" SUFFIX,
                           date->year,
                           date->month,
                           date->day,
                           sequence);
    return written > 0 && (size_t)written < out_size;
}

static bool read_digits(const char **cursor, size_t count, int *value)
{
    int result = 0;

    for (size_t i = 0; i < count; ++i) {
        if (!isdigit((unsigned char)(*cursor)[i])) {
            return false;
        }
        result = result * 10 + ((*cursor)[i] - '0');
    }
    *cursor += count;
    *value = result;
    return true;
}

static bool match_literal_nocase(const char **cursor, const char *literal)
{
    size_t length = strlen(literal);

    for (size_t i = 0; i < length; ++i) {
        if (toupper((unsigned char)(*cursor)[i]) != literal[i]) {
            return false;
        }
    }
    *cursor += length;
    return true;
}

bool error_log_naming_parse(const char *name, error_log_date_t *date, unsigned *sequence)
{
    const char *p = name;
    int year = 0;
    int month = 0;
    int day = 0;

    if (name == NULL || !match_literal_nocase(&p, PREFIX) ||
        !read_digits(&p, 4, &year) || *p++ != '-' ||
        !read_digits(&p, 2, &month) || *p++ != '-' ||
        !read_digits(&p, 2, &day) || *p++ != '_') {
        return false;
    }

    /* Two or more digits, and nothing but digits. */
    unsigned long number = 0;
    size_t digits = 0;
    while (isdigit((unsigned char)*p)) {
        number = number * 10UL + (unsigned long)(*p - '0');
        if (++digits > 6U) {
            return false;
        }
        ++p;
    }
    if (digits < 2U || !match_literal_nocase(&p, SUFFIX) || *p != '\0') {
        return false;
    }
    if (month < 1 || month > 12 || day < 1 || day > 31 ||
        number == 0UL || number > ERROR_LOG_NAMING_MAX_SEQUENCE) {
        return false;
    }

    if (date != NULL) {
        *date = (error_log_date_t){ .year = year, .month = month, .day = day };
    }
    if (sequence != NULL) {
        *sequence = (unsigned)number;
    }
    return true;
}

bool error_log_naming_same_date(const error_log_date_t *a, const error_log_date_t *b)
{
    return a->year == b->year && a->month == b->month && a->day == b->day;
}

void error_log_naming_date_text(char *out, size_t out_size, const error_log_date_t *date)
{
    (void)snprintf(out, out_size, "%04d-%02d-%02d", date->year, date->month, date->day);
}

bool error_log_naming_resolve(const error_log_scan_t *scan, uint64_t max_file_size, error_log_target_t *out)
{
    if (!scan->found) {
        *out = (error_log_target_t){ .sequence = 1U, .append = false };
        return true;
    }
    if (scan->max_sequence_size < max_file_size) {
        *out = (error_log_target_t){ .sequence = scan->max_sequence, .append = true };
        return true;
    }
    if (scan->max_sequence >= ERROR_LOG_NAMING_MAX_SEQUENCE) {
        return false;
    }
    *out = (error_log_target_t){ .sequence = scan->max_sequence + 1U, .append = false };
    return true;
}
