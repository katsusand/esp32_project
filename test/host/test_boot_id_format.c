/*
 * Boot ID formatting.
 *
 * The id is written at the head of every log file, and the whole point of it is
 * that two files carrying the same id are the same run of the firmware. So it
 * has to be a well formed UUID, and it must not collapse to one value when the
 * random source is weak (before Wi-Fi is up the generator is not truly random).
 */

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "boot_id.h"

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

static bool is_well_formed(const char *text)
{
    if (strlen(text) != BOOT_ID_STRING_LEN) {
        return false;
    }
    for (size_t i = 0; i < BOOT_ID_STRING_LEN; ++i) {
        bool dash = i == 8 || i == 13 || i == 18 || i == 23;
        if (dash ? text[i] != '-' : !(isdigit((unsigned char)text[i]) || (text[i] >= 'a' && text[i] <= 'f'))) {
            return false;
        }
    }
    return true;
}

static void test_format(void)
{
    uint8_t zeros[16] = { 0 };
    uint8_t ones[16];
    char text[BOOT_ID_STRING_LEN + 1];

    memset(ones, 0xff, sizeof(ones));

    boot_id_format_uuid_v4(zeros, text);
    check(is_well_formed(text), "all-zero input is a well formed UUID");
    check(strcmp(text, "00000000-0000-4000-8000-000000000000") == 0,
          "the version nibble is forced to 4 and the variant to 10xx");

    boot_id_format_uuid_v4(ones, text);
    check(is_well_formed(text), "all-ones input is a well formed UUID");
    check(strcmp(text, "ffffffff-ffff-4fff-bfff-ffffffffffff") == 0,
          "version 4 and variant bits are set whatever the input");

    uint8_t sequence[16];
    for (size_t i = 0; i < sizeof(sequence); ++i) {
        sequence[i] = (uint8_t)(0x10 + i);
    }
    boot_id_format_uuid_v4(sequence, text);
    check(strcmp(text, "10111213-1415-4617-9819-1a1b1c1d1e1f") == 0, "bytes land in order, lower case hex");
}

static void test_mix(void)
{
    uint8_t random_a[16] = { 0 };
    uint8_t mac_1[6] = { 0x24, 0x6f, 0x28, 0x00, 0x00, 0x01 };
    uint8_t mac_2[6] = { 0x24, 0x6f, 0x28, 0x00, 0x00, 0x02 };
    uint8_t out_1[16];
    uint8_t out_2[16];
    uint8_t out_again[16];

    boot_id_mix(out_1, random_a, mac_1, 1000);
    boot_id_mix(out_again, random_a, mac_1, 1000);
    check(memcmp(out_1, out_again, sizeof(out_1)) == 0, "the mix is deterministic for the same inputs");

    /* The worst case: a random source that returns the same bytes every boot. */
    boot_id_mix(out_2, random_a, mac_2, 1000);
    check(memcmp(out_1, out_2, sizeof(out_1)) != 0, "two units differ even with identical random bytes");

    boot_id_mix(out_2, random_a, mac_1, 1001);
    check(memcmp(out_1, out_2, sizeof(out_1)) != 0, "two boots differ even with identical random bytes");

    bool halves_differ = memcmp(out_1, out_1 + 8, 8) != 0;
    check(halves_differ, "the two halves of the id are not copies of each other");

    /* With a good random source the mix must not throw it away. */
    uint8_t random_b[16];
    for (size_t i = 0; i < sizeof(random_b); ++i) {
        random_b[i] = (uint8_t)(0xa0 + 3 * i);
    }
    boot_id_mix(out_2, random_b, mac_1, 1000);
    check(memcmp(out_1, out_2, sizeof(out_1)) != 0, "different random bytes give a different id");
    uint8_t unmixed_1[16];
    uint8_t unmixed_2[16];
    for (size_t i = 0; i < 16; ++i) {
        unmixed_1[i] = (uint8_t)(out_1[i] ^ random_a[i]);
        unmixed_2[i] = (uint8_t)(out_2[i] ^ random_b[i]);
    }
    check(memcmp(unmixed_1, unmixed_2, sizeof(unmixed_1)) == 0,
          "the digest depends only on the MAC and the time, so random bytes pass straight through");
}

int main(void)
{
    test_format();
    test_mix();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
