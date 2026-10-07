/*
 * Error log line format.
 *
 * A log is only useful if every line can be read the same way: one record per
 * line, the same fields in the same order, never longer than the buffer that
 * carries it, and never silently missing. These checks pin that.
 */

#include <stdio.h>
#include <string.h>
#include "error_log_format.h"

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

static void expect_text(const char *got, const char *want, const char *what)
{
    bool ok = strcmp(got, want) == 0;

    check(ok, what);
    if (!ok) {
        printf("       got:  [%s]\n       want: [%s]\n", got, want);
    }
}

static struct tm make_tm(int year, int month, int day, int hour, int minute, int second)
{
    struct tm t = { 0 };

    t.tm_year = year - 1900;
    t.tm_mon = month - 1;
    t.tm_mday = day;
    t.tm_hour = hour;
    t.tm_min = minute;
    t.tm_sec = second;
    return t;
}

static void test_stamp(void)
{
    char out[64];
    struct tm t = make_tm(2026, 10, 7, 12, 34, 56);

    check(error_log_format_stamp(out, sizeof(out), &t, 12345) == strlen("[2026-10-07 12:34:56 +12345ms]"),
          "the stamp reports its length");
    expect_text(out, "[2026-10-07 12:34:56 +12345ms]", "date and time, then the uptime in ms");

    t = make_tm(1970, 1, 1, 0, 0, 5);
    (void)error_log_format_stamp(out, sizeof(out), &t, 5123);
    expect_text(out, "[1970-01-01 00:00:05 +5123ms]", "a clock that was never set reads 1970-01-01");

    t = make_tm(2026, 1, 2, 3, 4, 5);
    (void)error_log_format_stamp(out, sizeof(out), &t, 0);
    expect_text(out, "[2026-01-02 03:04:05 +0ms]", "single digits are zero padded, except the uptime");

    (void)error_log_format_stamp(out, sizeof(out), &t, 18446744073709551615ULL);
    expect_text(out, "[2026-01-02 03:04:05 +18446744073709551615ms]", "the largest uptime still fits");

    char tiny[8];
    check(error_log_format_stamp(tiny, sizeof(tiny), &t, 0) == 0 && tiny[0] == '\0',
          "a buffer that is too small gives an empty stamp, not an overrun");
}

static void test_body(void)
{
    char out[128];
    error_log_fields_t f = {
        .tag = "time_sync", .func = "time_sync_task", .line = 493,
        .has_code = true, .code = 0x107, .code_name = "ESP_ERR_TIMEOUT",
        .message = "time sync failed",
    };

    check(error_log_format_body(out, sizeof(out), &f) == strlen(out), "the body reports its length");
    expect_text(out, "time_sync time_sync_task:493 ESP_ERR_TIMEOUT(0x107): time sync failed",
                "tag, function:line, code, then the message");

    f.has_code = false;
    (void)error_log_format_body(out, sizeof(out), &f);
    expect_text(out, "time_sync time_sync_task:493: time sync failed", "without a code there is no code field");

    f.func = NULL;
    (void)error_log_format_body(out, sizeof(out), &f);
    expect_text(out, "time_sync: time sync failed", "callers that give no function get tag and message only");

    f.has_code = true;
    f.code = -1;
    f.code_name = "ESP_FAIL";
    (void)error_log_format_body(out, sizeof(out), &f);
    expect_text(out, "time_sync ESP_FAIL(-1): time sync failed",
                "a negative code is written in decimal, not as 0xffffffff");

    f.code = 0x10c;
    f.code_name = NULL;
    (void)error_log_format_body(out, sizeof(out), &f);
    expect_text(out, "time_sync ERR(0x10c): time sync failed", "a code with no name keeps its number");

    f.message = "";
    (void)error_log_format_body(out, sizeof(out), &f);
    expect_text(out, "time_sync ERR(0x10c)", "an empty message leaves no dangling colon");

    f.tag = NULL;
    f.has_code = false;
    f.message = NULL;
    (void)error_log_format_body(out, sizeof(out), &f);
    expect_text(out, "?", "missing tag and message do not crash");
}

static void test_sanitize(void)
{
    char out[128];
    error_log_fields_t f = {
        .tag = "wifi", .message = "line one\nline two\r\tend\x01",
    };

    (void)error_log_format_body(out, sizeof(out), &f);
    expect_text(out, "wifi: line one line two  end ", "control characters become spaces, so a message cannot split a record");
    check(strchr(out, '\n') == NULL && strchr(out, '\r') == NULL, "no newline survives in the body");

    f.message = "caf\xc3\xa9";
    (void)error_log_format_body(out, sizeof(out), &f);
    expect_text(out, "wifi: caf\xc3\xa9", "UTF-8 bytes pass through untouched");
}

static void test_body_truncation(void)
{
    char out[32];
    error_log_fields_t f = {
        .tag = "t", .message = "0123456789012345678901234567890123456789",
    };

    size_t length = error_log_format_body(out, sizeof(out), &f);
    check(length == strlen(out) && length == sizeof(out) - 1U, "an over-long body fills the buffer exactly");
    check(strcmp(out + length - 3U, "...") == 0, "and says it was cut");
    expect_text(out, "t: 0123456789012345678901234...", "the cut keeps the start of the message and ends in three dots");

    f.message = "short";
    f.message_cut = true;
    char roomy[64];
    (void)error_log_format_body(roomy, sizeof(roomy), &f);
    expect_text(roomy, "t: short...", "a message the caller already cut is marked too, without losing more of it");

    char exact[8];
    f.message = "abcd";
    f.message_cut = false;
    (void)error_log_format_body(exact, sizeof(exact), &f);
    expect_text(exact, "t: abcd", "a body that fits exactly is not marked");

    char one[1];
    check(error_log_format_body(one, sizeof(one), &f) == 0 && one[0] == '\0', "a one byte buffer holds just the NUL");
    check(error_log_format_body(NULL, 0, &f) == 0, "a zero byte buffer is not written to");
}

static void test_line(void)
{
    char out[ERROR_LOG_LINE_MAX];

    size_t length = error_log_format_line(out, sizeof(out), "[2026-10-07 12:34:56 +12345ms]", "wifi: down");
    check(length == strlen(out), "the line reports its length");
    expect_text(out, "[2026-10-07 12:34:56 +12345ms] wifi: down\n", "stamp, a space, the body, a newline");

    char body[400];
    memset(body, 'x', sizeof(body) - 1U);
    body[sizeof(body) - 1U] = '\0';
    length = error_log_format_line(out, sizeof(out), "[2026-10-07 12:34:56 +12345ms]", body);
    check(length <= ERROR_LOG_LINE_MAX - 1U, "a line never exceeds the buffer, NUL included");
    check(out[length - 1U] == '\n' && strchr(out, '\n') == out + length - 1U, "an over-long line still ends in exactly one newline");
    check(strcmp(out + length - 4U, "...\n") == 0, "and is marked as cut");

    char tight[8];
    length = error_log_format_line(tight, sizeof(tight), "[x]", "abcdefghij");
    check(length < sizeof(tight) && tight[length - 1U] == '\n' && tight[length] == '\0', "even a tiny buffer gets a terminated line");
    check(error_log_format_line(tight, 1, "[x]", "a") == 0 && tight[0] == '\0', "a one byte buffer holds just the NUL");
}

static void test_restamp(void)
{
    check(error_log_restamp(1000000, 20000, 5000) == 1000000 - 15, "a line from 15 s ago is stamped 15 s before now");
    check(error_log_restamp(1000000, 20000, 19999) == 1000000, "less than a second ago is now");
    check(error_log_restamp(1000000, 20000, 20000) == 1000000, "the same instant is now");
    check(error_log_restamp(1000000, 20000, 90000) == 1000000, "an uptime from the future never moves the clock forward");
    /* The clock was never set: wall time is just the uptime counted from 1970. A line from 3 s into the boot must still read 3 s. */
    check(error_log_restamp(20, 20000, 3000) == 3, "an unset clock gives back the uptime, so 1970 lines still read in order");
}

int main(void)
{
    test_stamp();
    test_body();
    test_sanitize();
    test_body_truncation();
    test_line();
    test_restamp();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
