/*
 * Folding an error that repeats into one line and a count.
 *
 * Done wrong this either hides errors (folding two different ones) or fails to
 * help (a count that is never reported, so the log shows one line for a storm of
 * a thousand). So: only identical events fold, a burst is always reported when
 * its window ends, and a full table gives up the least useful entry.
 */

#include <stdio.h>
#include <string.h>
#include "error_log_dedupe.h"

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

#define WINDOW 10000U

static bool event(error_log_dedupe_t *d, uint32_t key, uint64_t now, size_t *report_count, error_log_repeat_t *reports)
{
    return error_log_dedupe_check(d, key, "wifi connect:12", now, reports, report_count);
}

static void test_fold_within_window(void)
{
    error_log_dedupe_t d;
    error_log_repeat_t reports[2];
    size_t n = 99;

    error_log_dedupe_init(&d, WINDOW);
    check(event(&d, 1, 1000, &n, reports) && n == 0, "the first occurrence is written");
    check(!event(&d, 1, 2000, &n, reports) && n == 0, "the same again, soon after, is only counted");
    check(!event(&d, 1, 9999, &n, reports) && n == 0, "still inside the window");

    check(event(&d, 1, 11000, &n, reports) && n == 1, "the same after the window is written, and reports the burst");
    check(reports[0].count == 2 && strcmp(reports[0].label, "wifi connect:12") == 0,
          "two repeats were left out, and the report says which event");
    check(!event(&d, 1, 12000, &n, reports) && n == 0, "a new window has started from that write");
}

static void test_window_edge(void)
{
    error_log_dedupe_t d;
    error_log_repeat_t reports[2];
    size_t n = 0;

    error_log_dedupe_init(&d, WINDOW);
    (void)event(&d, 7, 0, &n, reports);
    check(!event(&d, 7, WINDOW - 1U, &n, reports), "one millisecond before the window ends: folded");
    check(event(&d, 7, WINDOW, &n, reports), "exactly at the end of the window: written");
}

static void test_different_events_never_fold(void)
{
    error_log_dedupe_t d;
    error_log_repeat_t reports[2];
    size_t n = 0;

    error_log_dedupe_init(&d, WINDOW);
    check(event(&d, 1, 100, &n, reports), "event A");
    check(event(&d, 2, 110, &n, reports), "event B is not folded into A");
    check(!event(&d, 1, 120, &n, reports), "A repeated is folded");
    check(!event(&d, 2, 130, &n, reports), "B repeated is folded on its own");

    uint32_t base = error_log_dedupe_key("wifi", "connect", 12, 0x107, "failed");
    check(base == error_log_dedupe_key("wifi", "connect", 12, 0x107, "failed"), "the key is stable");
    check(base != error_log_dedupe_key("time", "connect", 12, 0x107, "failed"), "a different tag is a different event");
    check(base != error_log_dedupe_key("wifi", "retry", 12, 0x107, "failed"), "a different function");
    check(base != error_log_dedupe_key("wifi", "connect", 13, 0x107, "failed"), "a different line");
    check(base != error_log_dedupe_key("wifi", "connect", 12, 0x103, "failed"), "a different code");
    check(base != error_log_dedupe_key("wifi", "connect", 12, 0x107, "failed again"),
          "a different message - the same error with other numbers in it is never folded away");
    check(error_log_dedupe_key(NULL, NULL, 0, 0, NULL) == error_log_dedupe_key("", "", 0, 0, ""),
          "missing parts read as empty");
}

static void test_collect(void)
{
    error_log_dedupe_t d;
    error_log_repeat_t reports[2];
    error_log_repeat_t out[4];
    size_t n = 0;
    uint64_t due = 0;

    error_log_dedupe_init(&d, WINDOW);
    check(!error_log_dedupe_next_due_ms(&d, 0, &due), "nothing pending at first");
    (void)event(&d, 1, 1000, &n, reports);
    check(!error_log_dedupe_next_due_ms(&d, 1500, &due), "a single occurrence leaves nothing to report");

    (void)event(&d, 1, 2000, &n, reports);
    (void)event(&d, 1, 3000, &n, reports);
    check(error_log_dedupe_next_due_ms(&d, 3000, &due) && due == 8000, "the burst is due when its window, from the first write, ends");
    check(error_log_dedupe_next_due_ms(&d, 20000, &due) && due == 0, "already past: due now");

    check(error_log_dedupe_collect(&d, 5000, false, out, 4) == 0, "before the window ends nothing is collected");
    check(error_log_dedupe_collect(&d, 11000, false, out, 4) == 1 && out[0].count == 2, "after it, the burst is reported with its count");
    check(error_log_dedupe_collect(&d, 11000, false, out, 4) == 0, "and only once");
    check(!error_log_dedupe_next_due_ms(&d, 11000, &due), "nothing is pending any more");

    check(event(&d, 1, 11500, &n, reports) && n == 0, "after a collect the same event starts fresh: written, with no stale report");

    (void)event(&d, 1, 12000, &n, reports);
    check(error_log_dedupe_collect(&d, 12500, true, out, 4) == 1 && out[0].count == 1,
          "force collects a burst whose window has not ended (used when the log is closed)");
}

static void test_table_full(void)
{
    error_log_dedupe_t d;
    error_log_repeat_t reports[2];
    size_t n = 0;

    error_log_dedupe_init(&d, WINDOW);
    for (uint32_t k = 1; k <= ERROR_LOG_DEDUPE_SLOTS; ++k) {
        (void)event(&d, k, k * 10U, &n, reports); /* eight distinct events */
    }
    (void)event(&d, 1, 100, &n, reports); /* event 1 now has a burst waiting */
    check(event(&d, 100, 200, &n, reports) && n == 0,
          "a ninth event is written, and pushes out one that has nothing to report");
    check(!event(&d, 1, 210, &n, reports), "the one with a count waiting was kept");

    /* Now every slot has something pending. */
    error_log_dedupe_init(&d, WINDOW);
    for (uint32_t k = 1; k <= ERROR_LOG_DEDUPE_SLOTS; ++k) {
        (void)event(&d, k, k * 10U, &n, reports);
        (void)event(&d, k, k * 10U + 1U, &n, reports);
    }
    check(event(&d, 100, 500, &n, reports) && n == 1,
          "when every entry has a count waiting, the oldest is given up and its count is reported, not lost");
    check(reports[0].count == 1, "with its own count");
}

static void test_off_and_labels(void)
{
    error_log_dedupe_t d;
    error_log_repeat_t reports[2];
    size_t n = 5;

    error_log_dedupe_init(&d, 0);
    check(event(&d, 1, 0, &n, reports) && event(&d, 1, 1, &n, reports) && event(&d, 1, 2, &n, reports) && n == 0,
          "a window of 0 turns folding off: everything is written");

    error_log_dedupe_init(&d, WINDOW);
    char long_label[200];
    memset(long_label, 'L', sizeof(long_label) - 1U);
    long_label[sizeof(long_label) - 1U] = '\0';
    (void)error_log_dedupe_check(&d, 9, long_label, 0, reports, &n);
    (void)error_log_dedupe_check(&d, 9, long_label, 1, reports, &n);
    error_log_repeat_t out[1];
    check(error_log_dedupe_collect(&d, 0, true, out, 1) == 1 && strlen(out[0].label) == ERROR_LOG_DEDUPE_LABEL_MAX - 1U,
          "an over-long label is cut, not overrun");
    (void)error_log_dedupe_check(&d, 10, NULL, 0, reports, &n);
    check(n == 0, "a missing label is accepted");
}

int main(void)
{
    test_fold_within_window();
    test_window_edge();
    test_different_events_never_fold();
    test_collect();
    test_table_full();
    test_off_and_labels();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
