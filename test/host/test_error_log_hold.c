/*
 * The buffer that holds log lines until they can be written.
 *
 * It has to hand lines back in the order they were made, wrap around its end
 * without corrupting anything, and - when it is full - keep the OLDEST lines and
 * say how many newer ones were refused, because the first error of a bad boot is
 * the one that explains the rest.
 */

#include <stdio.h>
#include <string.h>
#include "error_log_hold.h"

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

static bool pop_is(error_log_hold_t *hold, uint64_t want_uptime, bool want_raw, const char *want_text)
{
    uint64_t uptime = 0;
    bool raw = false;
    char text[300];
    size_t length = 0;

    return error_log_hold_pop(hold, &uptime, &raw, text, sizeof(text), &length) &&
           uptime == want_uptime && raw == want_raw && strcmp(text, want_text) == 0 && length == strlen(want_text);
}

static void test_fifo(void)
{
    uint8_t storage[512];
    error_log_hold_t hold;

    error_log_hold_init(&hold, storage, sizeof(storage));
    check(error_log_hold_is_empty(&hold) && error_log_hold_count(&hold) == 0, "starts empty");
    check(!error_log_hold_pop(&hold, NULL, NULL, NULL, 0, NULL), "popping an empty buffer says so");

    check(error_log_hold_push(&hold, 100, false, "first", 5), "push a body");
    check(error_log_hold_push(&hold, 250, true, "[raw line]\n", 11), "push a finished line");
    check(error_log_hold_push(&hold, 18446744073709551615ULL, false, "third", 5), "the largest uptime survives");
    check(error_log_hold_count(&hold) == 3, "three entries");

    check(pop_is(&hold, 100, false, "first"), "oldest first, with its uptime");
    check(pop_is(&hold, 250, true, "[raw line]\n"), "the raw flag and the newline travel with the entry");
    check(pop_is(&hold, 18446744073709551615ULL, false, "third"), "then the last one");
    check(error_log_hold_is_empty(&hold) && !error_log_hold_pop(&hold, NULL, NULL, NULL, 0, NULL), "and it is empty again");

    check(error_log_hold_push(&hold, 5, false, "", 0), "an empty text is allowed");
    check(pop_is(&hold, 5, false, ""), "and comes back empty");
}

static void test_peek_oldest_uptime(void)
{
    uint8_t storage[256];
    error_log_hold_t hold;
    uint64_t uptime = 99;

    error_log_hold_init(&hold, storage, sizeof(storage));
    check(!error_log_hold_peek_uptime(&hold, &uptime) && uptime == 99, "nothing to peek at when empty, and the output is left alone");

    (void)error_log_hold_push(&hold, 7000, false, "old", 3);
    (void)error_log_hold_push(&hold, 9000, false, "newer", 5);
    check(error_log_hold_peek_uptime(&hold, &uptime) && uptime == 7000, "the oldest entry's uptime");
    check(error_log_hold_peek_uptime(&hold, &uptime) && uptime == 7000 && error_log_hold_count(&hold) == 2,
          "looking does not take anything out");
    check(pop_is(&hold, 7000, false, "old"), "the entry is still there to pop");
    check(error_log_hold_peek_uptime(&hold, &uptime) && uptime == 9000, "after a pop, the next one is the oldest");
}

static void test_overflow_keeps_the_oldest(void)
{
    uint8_t storage[ERROR_LOG_HOLD_ENTRY_OVERHEAD * 3 + 30];
    error_log_hold_t hold;

    error_log_hold_init(&hold, storage, sizeof(storage));
    check(error_log_hold_push(&hold, 1, false, "0123456789", 10), "entry 1 fits");
    check(error_log_hold_push(&hold, 2, false, "0123456789", 10), "entry 2 fits");
    check(error_log_hold_push(&hold, 3, false, "0123456789", 10), "entry 3 fits exactly");
    check(!error_log_hold_push(&hold, 4, false, "x", 1), "one byte more is refused");
    check(!error_log_hold_push(&hold, 5, false, "y", 1), "and so is the next");
    check(error_log_hold_count(&hold) == 3, "the refused ones are not in the buffer");

    check(error_log_hold_take_dropped(&hold) == 2, "two were counted as dropped");
    check(error_log_hold_take_dropped(&hold) == 0, "taking the count clears it");

    check(pop_is(&hold, 1, false, "0123456789"), "the oldest are the ones that were kept (1)");
    check(pop_is(&hold, 2, false, "0123456789"), "(2)");
    check(pop_is(&hold, 3, false, "0123456789"), "(3)");

    /* Room is given back as entries leave. */
    check(error_log_hold_push(&hold, 6, false, "0123456789", 10), "popping made room again");
    check(error_log_hold_take_dropped(&hold) == 0, "a successful push drops nothing");

    uint8_t tiny[ERROR_LOG_HOLD_ENTRY_OVERHEAD - 1U];
    error_log_hold_init(&hold, tiny, sizeof(tiny));
    check(!error_log_hold_push(&hold, 1, false, "", 0) && error_log_hold_take_dropped(&hold) == 1,
          "a buffer smaller than one header refuses everything and counts it");

    error_log_hold_init(&hold, storage, 0);
    check(!error_log_hold_push(&hold, 1, false, "a", 1), "a zero byte buffer refuses everything");
}

static void test_wrap_around(void)
{
    uint8_t storage[100];
    error_log_hold_t hold;
    char text[40];
    bool all_ok = true;

    error_log_hold_init(&hold, storage, sizeof(storage));
    /* Many more entries than the buffer holds, of changing lengths, so the
       write position walks around the ring and every entry straddles the end
       at some point. */
    for (unsigned i = 0; i < 400U; ++i) {
        size_t length = 1U + (i * 7U) % 23U;
        for (size_t k = 0; k < length; ++k) {
            text[k] = (char)('a' + (i + k) % 26U);
        }
        text[length] = '\0';
        if (!error_log_hold_push(&hold, i, (i % 3U) == 0U, text, length)) {
            all_ok = false;
            break;
        }

        uint64_t uptime = 0;
        bool raw = false;
        char got[40];
        size_t got_length = 0;
        if (!error_log_hold_pop(&hold, &uptime, &raw, got, sizeof(got), &got_length) ||
            uptime != i || raw != ((i % 3U) == 0U) || strcmp(got, text) != 0 || got_length != length) {
            all_ok = false;
            break;
        }
    }
    check(all_ok, "400 entries of varying length come back intact, in order");

    /* Keep a few in flight at once so the head is not always at zero. */
    uint64_t next_in = 0;
    uint64_t next_out = 0;
    all_ok = true;
    for (unsigned round = 0; round < 300U && all_ok; ++round) {
        char body[16];
        int length = snprintf(body, sizeof(body), "n%llu", (unsigned long long)next_in);
        if (error_log_hold_push(&hold, next_in, false, body, (size_t)length)) {
            ++next_in;
        }
        if (round % 2U == 1U) {
            char expect[16];
            snprintf(expect, sizeof(expect), "n%llu", (unsigned long long)next_out);
            all_ok = pop_is(&hold, next_out, false, expect);
            ++next_out;
        }
    }
    check(all_ok, "with several entries always in flight, the order still holds across the wrap");
}

static void test_pop_into_a_small_buffer(void)
{
    uint8_t storage[128];
    error_log_hold_t hold;
    char small[6];
    size_t length = 99;

    error_log_hold_init(&hold, storage, sizeof(storage));
    (void)error_log_hold_push(&hold, 1, false, "abcdefghij", 10);
    (void)error_log_hold_push(&hold, 2, false, "next", 4);

    check(error_log_hold_pop(&hold, NULL, NULL, small, sizeof(small), &length), "pop into a small buffer");
    check(strcmp(small, "abcde") == 0 && length == 5, "the text is cut to fit and still NUL terminated");
    check(pop_is(&hold, 2, false, "next"), "the entry was consumed whole, so the next one is not damaged");

    (void)error_log_hold_push(&hold, 3, false, "zzz", 3);
    check(error_log_hold_pop(&hold, NULL, NULL, NULL, 0, NULL) && error_log_hold_is_empty(&hold),
          "a caller that only wants to discard can pass no buffer");
}

int main(void)
{
    test_fifo();
    test_peek_oldest_uptime();
    test_overflow_keeps_the_oldest();
    test_wrap_around();
    test_pop_into_a_small_buffer();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
