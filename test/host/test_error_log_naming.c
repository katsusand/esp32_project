/*
 * Error log file names, and today's file.
 *
 * The file name is the only index the logs have: a person finds a day by its
 * name, and the firmware finds the file to append to by it. A name that parses
 * wrong means a second file for the same day, or appending into somebody else's.
 */

#include <stdio.h>
#include <string.h>
#include "error_log_naming.h"

int g_log_quiet = 1;

static int checks = 0;
static int failures = 0;

static void check(bool ok, const char *what)
{
    checks++;
    if (!ok) {
        printf("  FAIL %s\n", what);
        failures++;
    } else {
        printf("  ok   %s\n", what);
    }
}

static void test_build(void)
{
    char out[ERROR_LOG_NAME_MAX];
    error_log_date_t date = { 2026, 10, 7 };

    check(error_log_naming_build(out, sizeof(out), &date, 1) && strcmp(out, "ERR_2026-10-07_01.LOG") == 0,
          "date, then the number to two digits");
    check(error_log_naming_build(out, sizeof(out), &date, 123) && strcmp(out, "ERR_2026-10-07_123.LOG") == 0,
          "the number grows past two digits instead of stopping at 99");
    check(error_log_naming_build(out, sizeof(out), &date, 9999) && strcmp(out, "ERR_2026-10-07_9999.LOG") == 0,
          "the highest number still fits");
    check(!error_log_naming_build(out, sizeof(out), &date, 0), "there is no number 0");
    check(!error_log_naming_build(out, sizeof(out), &date, 10000), "there is nothing past the maximum");

    error_log_date_t epoch = { 1970, 1, 1 };
    check(error_log_naming_build(out, sizeof(out), &epoch, 3) && strcmp(out, "ERR_1970-01-01_03.LOG") == 0,
          "a clock that was never set names the file 1970-01-01");

    char tiny[10];
    check(!error_log_naming_build(tiny, sizeof(tiny), &date, 1), "a buffer that is too small fails instead of cutting the name");

    /* 8.3 would not hold this; the long name is the point. */
    check(strlen("ERR_2026-10-07_9999.LOG") < ERROR_LOG_NAME_MAX, "the longest name fits ERROR_LOG_NAME_MAX");
}

static void test_parse(void)
{
    error_log_date_t date = { 0 };
    unsigned sequence = 0;

    check(error_log_naming_parse("ERR_2026-10-07_01.LOG", &date, &sequence) &&
              date.year == 2026 && date.month == 10 && date.day == 7 && sequence == 1,
          "a name it built reads back");
    check(error_log_naming_parse("ERR_2026-10-07_123.LOG", &date, &sequence) && sequence == 123, "three digit numbers");
    check(error_log_naming_parse("err_2026-10-07_05.log", &date, &sequence) && sequence == 5,
          "lower case, as a card renamed on a PC may have it");
    check(error_log_naming_parse("ERR_1970-01-01_02.LOG", &date, &sequence) && date.year == 1970, "the unset-clock date");

    check(!error_log_naming_parse("ERR_0001.LOG", &date, &sequence), "the older ERR_0001.LOG names are not ours any more");
    check(!error_log_naming_parse("error_0001.log", &date, &sequence), "nor is the original error_0001.log");
    check(!error_log_naming_parse("ERR_2026-10-07_1.LOG", &date, &sequence), "a one digit number is not a name we write");
    check(!error_log_naming_parse("ERR_2026-10-07_00.LOG", &date, &sequence), "number 0 is not a name we write");
    check(!error_log_naming_parse("ERR_2026-10-07_10000.LOG", &date, &sequence), "past the maximum");
    check(!error_log_naming_parse("ERR_2026-13-07_01.LOG", &date, &sequence), "month 13");
    check(!error_log_naming_parse("ERR_2026-00-07_01.LOG", &date, &sequence), "month 0");
    check(!error_log_naming_parse("ERR_2026-10-32_01.LOG", &date, &sequence), "day 32");
    check(!error_log_naming_parse("ERR_2026-10-00_01.LOG", &date, &sequence), "day 0");
    check(!error_log_naming_parse("ERR_2026-10-07_01.LOG.bak", &date, &sequence), "a backup copy is not the log");
    check(!error_log_naming_parse("ERR_2026-10-07_01.TXT", &date, &sequence), "wrong extension");
    check(!error_log_naming_parse("XERR_2026-10-07_01.LOG", &date, &sequence), "wrong prefix");
    check(!error_log_naming_parse("ERR_2026-10-07_01", &date, &sequence), "no extension");
    check(!error_log_naming_parse("ERR_2026-10-07_.LOG", &date, &sequence), "no number");
    check(!error_log_naming_parse("ERR_2026-10-07_0a.LOG", &date, &sequence), "a non-digit in the number");
    check(!error_log_naming_parse("ERR_2026-1-07_01.LOG", &date, &sequence), "a one digit month");
    check(!error_log_naming_parse("", &date, &sequence), "empty");
    check(!error_log_naming_parse("ERR_", &date, &sequence), "just the prefix");
    check(!error_log_naming_parse(NULL, &date, &sequence), "NULL");
    check(error_log_naming_parse("ERR_2026-10-07_01.LOG", NULL, NULL), "the outputs are optional");
}

static void test_date_helpers(void)
{
    error_log_date_t a = { 2026, 10, 7 };
    error_log_date_t b = { 2026, 10, 7 };
    error_log_date_t c = { 2026, 10, 8 };
    char text[16];

    check(error_log_naming_same_date(&a, &b), "the same date");
    check(!error_log_naming_same_date(&a, &c), "a different day");
    error_log_naming_date_text(text, sizeof(text), &a);
    check(strcmp(text, "2026-10-07") == 0, "date as text");
}

static void test_resolve(void)
{
    error_log_target_t t = { 0 };
    error_log_scan_t none = { .found = false };
    error_log_scan_t roomy = { .found = true, .max_sequence = 3, .max_sequence_size = 1000 };
    error_log_scan_t full = { .found = true, .max_sequence = 3, .max_sequence_size = 256U * 1024U };
    error_log_scan_t nearly = { .found = true, .max_sequence = 3, .max_sequence_size = 256U * 1024U - 1U };
    error_log_scan_t last = { .found = true, .max_sequence = ERROR_LOG_NAMING_MAX_SEQUENCE, .max_sequence_size = 256U * 1024U };
    error_log_scan_t last_roomy = { .found = true, .max_sequence = ERROR_LOG_NAMING_MAX_SEQUENCE, .max_sequence_size = 10 };
    const uint64_t limit = 256U * 1024U;

    check(error_log_naming_resolve(&none, limit, &t) && t.sequence == 1 && !t.append, "no file for today starts number 1");
    check(error_log_naming_resolve(&roomy, limit, &t) && t.sequence == 3 && t.append, "the newest file with room is appended to");
    check(error_log_naming_resolve(&nearly, limit, &t) && t.sequence == 3 && t.append, "one byte under the limit still has room");
    check(error_log_naming_resolve(&full, limit, &t) && t.sequence == 4 && !t.append, "a file at the limit is followed by the next number");
    check(!error_log_naming_resolve(&last, limit, &t), "past the last number there is no file for today");
    check(error_log_naming_resolve(&last_roomy, limit, &t) && t.sequence == ERROR_LOG_NAMING_MAX_SEQUENCE && t.append,
          "the last number can still be appended to while it has room");
}

int main(void)
{
    test_build();
    test_parse();
    test_date_helpers();
    test_resolve();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
