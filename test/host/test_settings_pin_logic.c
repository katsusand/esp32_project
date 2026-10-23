/*
 * Settings PIN logic: the digits being typed, the wait after wrong PINs, the
 * "type it twice" check, and comparing a typed PIN with a stored hash.
 *
 * The hash here is a stand-in, not PBKDF2: what is tested is that the record
 * is filled and compared through the hash with its salt and iteration count,
 * and that anything else is a mismatch. mbedtls itself is not tested.
 */

#include <stdio.h>
#include <string.h>
#include "settings_pin_logic.h"

static int s_failures;
static int s_checks;

static void check(int ok, const char *what)
{
    ++s_checks;
    if (!ok) {
        ++s_failures;
        printf("FAIL: %s\n", what);
    } else {
        printf("ok: %s\n", what);
    }
}

static int s_hash_calls;

/* Deterministic and sensitive to every input: pin, salt and iterations. */
static bool fake_hash(const char *pin, size_t pin_len, const uint8_t *salt, size_t salt_len, uint32_t iterations,
                      uint8_t out[SETTINGS_PIN_HASH_LEN])
{
    uint32_t h = 2166136261u ^ iterations;

    ++s_hash_calls;
    for (size_t i = 0; i < pin_len; ++i) {
        h = (h ^ (uint8_t)pin[i]) * 16777619u;
    }
    h = (h ^ 0xffu) * 16777619u; /* "1234560" must not collide with "123456" + "0" in the salt */
    for (size_t i = 0; i < salt_len; ++i) {
        h = (h ^ salt[i]) * 16777619u;
    }
    for (size_t i = 0; i < SETTINGS_PIN_HASH_LEN; ++i) {
        h = (h ^ (uint32_t)i) * 16777619u;
        out[i] = (uint8_t)(h >> 24);
    }
    return true;
}

static bool failing_hash(const char *pin, size_t pin_len, const uint8_t *salt, size_t salt_len, uint32_t iterations,
                         uint8_t out[SETTINGS_PIN_HASH_LEN])
{
    (void)pin;
    (void)pin_len;
    (void)salt;
    (void)salt_len;
    (void)iterations;
    memset(out, 0, SETTINGS_PIN_HASH_LEN);
    return false;
}

static settings_pin_entry_t entry_of(const char *digits)
{
    settings_pin_entry_t e;

    memset(&e, 0, sizeof(e));
    for (const char *p = digits; *p != '\0'; ++p) {
        (void)settings_pin_entry_push(&e, *p);
    }
    return e;
}

static void test_entry(void)
{
    settings_pin_entry_t e;
    memset(&e, 0, sizeof(e));

    check(!settings_pin_entry_pop(&e), "nothing to delete in an empty entry");
    check(!settings_pin_entry_push(&e, 'a') && !settings_pin_entry_push(&e, '/') && e.len == 0,
          "only digits are taken");
    for (int i = 0; i < SETTINGS_PIN_MAX_DIGITS; ++i) {
        check(settings_pin_entry_complete(&e) == (i >= SETTINGS_PIN_MIN_DIGITS), "complete only from 6 digits");
        check(settings_pin_entry_push(&e, (char)('0' + i)), "a digit is added");
    }
    check(settings_pin_entry_complete(&e), "8 digits are complete");
    check(!settings_pin_entry_push(&e, '9') && e.len == SETTINGS_PIN_MAX_DIGITS, "a ninth digit is refused");
    check(strcmp(e.digits, "01234567") == 0, "the digits are kept in order");
    check(settings_pin_entry_pop(&e) && e.len == 7 && strcmp(e.digits, "0123456") == 0, "delete removes the last");
    settings_pin_entry_clear(&e);
    uint8_t zero[sizeof(e)];
    memset(zero, 0, sizeof(zero));
    check(memcmp(&e, zero, sizeof(e)) == 0, "clear wipes every byte");
}

static void test_wait(void)
{
    static const struct {
        uint32_t failures;
        uint32_t seconds;
    } table[] = {
        { 0, 0 },    { 1, 0 },     { 4, 0 },     { 5, 30 },     { 6, 60 },          { 7, 120 },
        { 8, 240 },  { 9, 480 },   { 10, 960 },  { 11, 1920 },  { 12, 3600 },       { 13, 3600 },
        { 100, 3600 }, { 0xffffffffu, 3600 },
    };
    char what[64];

    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); ++i) {
        uint32_t got = settings_pin_wait_seconds(table[i].failures);
        snprintf(what, sizeof(what), "%u wrong in a row waits %u s (got %u)", (unsigned)table[i].failures,
                 (unsigned)table[i].seconds, (unsigned)got);
        check(got == table[i].seconds, what);
    }
    uint32_t previous = 0;
    int monotonic = 1;
    for (uint32_t f = 0; f < 200; ++f) {
        uint32_t w = settings_pin_wait_seconds(f);
        monotonic &= w >= previous && w <= SETTINGS_PIN_MAX_WAIT_SECONDS;
        previous = w;
    }
    check(monotonic, "the wait never shrinks as failures grow, and never passes the cap");
}

static void test_setup(void)
{
    settings_pin_setup_t s;
    settings_pin_entry_t a = entry_of("123456");
    settings_pin_entry_t b = entry_of("123457");
    settings_pin_entry_t longer = entry_of("1234560");
    settings_pin_entry_t short_one = entry_of("12345");

    memset(&s, 0, sizeof(s));
    settings_pin_setup_reset(&s);
    check(settings_pin_setup_submit(&s, &short_one) == SETTINGS_PIN_SETUP_TOO_SHORT &&
              s.stage == SETTINGS_PIN_SETUP_FIRST,
          "5 digits are too short and change nothing");
    check(settings_pin_setup_submit(&s, &a) == SETTINGS_PIN_SETUP_NEED_SECOND && s.stage == SETTINGS_PIN_SETUP_SECOND,
          "the first entry asks for the second");
    check(settings_pin_setup_submit(&s, &a) == SETTINGS_PIN_SETUP_MATCH && strcmp(s.first.digits, "123456") == 0,
          "the same PIN twice matches and is kept");

    settings_pin_setup_reset(&s);
    (void)settings_pin_setup_submit(&s, &a);
    check(settings_pin_setup_submit(&s, &b) == SETTINGS_PIN_SETUP_MISMATCH && s.stage == SETTINGS_PIN_SETUP_FIRST &&
              s.first.len == 0,
          "a different second entry starts over and forgets the first");
    check(settings_pin_setup_submit(&s, &b) == SETTINGS_PIN_SETUP_NEED_SECOND &&
              settings_pin_setup_submit(&s, &b) == SETTINGS_PIN_SETUP_MATCH,
          "after a mismatch the new pair is what counts");

    settings_pin_setup_reset(&s);
    (void)settings_pin_setup_submit(&s, &a);
    check(settings_pin_setup_submit(&s, &longer) == SETTINGS_PIN_SETUP_MISMATCH,
          "a longer PIN with the same start is a mismatch");
    (void)settings_pin_setup_submit(&s, &longer);
    check(settings_pin_setup_submit(&s, &a) == SETTINGS_PIN_SETUP_MISMATCH,
          "a shorter PIN with the same start is a mismatch");
    settings_pin_setup_reset(&s);
    (void)settings_pin_setup_submit(&s, &a);
    check(settings_pin_setup_submit(&s, &short_one) == SETTINGS_PIN_SETUP_TOO_SHORT &&
              s.stage == SETTINGS_PIN_SETUP_SECOND,
          "a too short second entry keeps the first");
}

static void test_record(void)
{
    settings_pin_record_t r;
    settings_pin_entry_t pin = entry_of("24681357");
    settings_pin_entry_t other = entry_of("24681358");
    settings_pin_entry_t prefix = entry_of("2468135");

    memset(&r, 0, sizeof(r));
    for (size_t i = 0; i < sizeof(r.salt); ++i) {
        r.salt[i] = (uint8_t)(i * 7 + 1);
    }
    check(!settings_pin_record_fill(&r, &pin, fake_hash), "no iterations, no record");
    r.iterations = 4096;
    s_hash_calls = 0;
    check(settings_pin_record_fill(&r, &pin, fake_hash) && s_hash_calls == 1, "the record is filled by one hash");

    check(settings_pin_record_matches(&r, &pin, fake_hash), "the right PIN matches");
    check(!settings_pin_record_matches(&r, &other, fake_hash), "one digit off does not match");
    check(!settings_pin_record_matches(&r, &prefix, fake_hash), "a prefix of the PIN does not match");

    settings_pin_record_t salted = r;
    salted.salt[0] ^= 1;
    check(!settings_pin_record_matches(&salted, &pin, fake_hash), "the salt is part of the hash");
    settings_pin_record_t fewer = r;
    fewer.iterations = 4095;
    check(!settings_pin_record_matches(&fewer, &pin, fake_hash), "the iteration count is part of the hash");

    check(!settings_pin_record_matches(&r, &pin, failing_hash), "a failing hash is a mismatch, never a match");
    settings_pin_record_t zero_hash = r;
    memset(zero_hash.hash, 0, sizeof(zero_hash.hash));
    check(!settings_pin_record_matches(&zero_hash, &pin, failing_hash),
          "a failing hash does not match a zeroed record either");

    settings_pin_entry_t short_one = entry_of("12345");
    check(!settings_pin_record_matches(&r, &short_one, fake_hash), "a too short entry never matches");
    check(!settings_pin_record_matches(&r, &pin, NULL) && !settings_pin_record_matches(NULL, &pin, fake_hash),
          "missing parts never match");

    uint8_t a[4] = { 1, 2, 3, 4 };
    uint8_t b[4] = { 1, 2, 3, 5 };
    check(settings_pin_equal_ct(a, a, 4) && !settings_pin_equal_ct(a, b, 4) && settings_pin_equal_ct(a, b, 3),
          "the comparison looks at every byte asked for");
}

int main(void)
{
    printf("entry\n");
    test_entry();
    printf("wait\n");
    test_wait();
    printf("type it twice\n");
    test_setup();
    printf("stored hash\n");
    test_record();
    printf("%d checks, %d failed\n", s_checks, s_failures);
    return s_failures == 0 ? 0 : 1;
}
