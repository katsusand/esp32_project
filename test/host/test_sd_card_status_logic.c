/*
 * SD card status decisions.
 *
 * What the screen says about the card hangs on three small decisions: which
 * state a failed mount means, whether a short test write means "full" or
 * "broken", and how long to wait before trying again. A wrong answer here shows
 * as the wrong icon, or as a unit without a card that retries the SPI bus every
 * few seconds forever.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "sd_card_status.h"

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

static void test_mount_error_classification(void)
{
    /* esp_vfs_fat_sdspi_mount() returns ESP_FAIL only when f_mount() failed. */
    check(sd_card_status_classify_mount_error(ESP_OK) == SD_CARD_STATUS_OK, "mount ok -> OK");
    check(sd_card_status_classify_mount_error(ESP_FAIL) == SD_CARD_STATUS_NO_FILESYSTEM,
          "ESP_FAIL (f_mount failed) -> NO_FILESYSTEM");

    /* An empty slot looks like a card that never answers. */
    check(sd_card_status_classify_mount_error(ESP_ERR_TIMEOUT) == SD_CARD_STATUS_NO_CARD,
          "timeout -> NO_CARD");
    check(sd_card_status_classify_mount_error(ESP_ERR_INVALID_RESPONSE) == SD_CARD_STATUS_NO_CARD,
          "invalid response -> NO_CARD");
    check(sd_card_status_classify_mount_error(ESP_ERR_INVALID_CRC) == SD_CARD_STATUS_NO_CARD,
          "bad CRC -> NO_CARD");
    check(sd_card_status_classify_mount_error(ESP_ERR_NOT_SUPPORTED) == SD_CARD_STATUS_NO_CARD,
          "unsupported card -> NO_CARD");

    /* Host-side failures must not read as "insert a card". */
    check(sd_card_status_classify_mount_error(ESP_ERR_NO_MEM) == SD_CARD_STATUS_FAULT, "no memory -> FAULT");
    check(sd_card_status_classify_mount_error(ESP_ERR_INVALID_STATE) == SD_CARD_STATUS_FAULT,
          "bus already taken -> FAULT");
    check(sd_card_status_classify_mount_error(ESP_ERR_INVALID_ARG) == SD_CARD_STATUS_FAULT,
          "bad configuration -> FAULT");
}

static void test_write_result_classification(void)
{
    check(sd_card_status_classify_write_result(512, 512, 0) == SD_CARD_STATUS_OK, "full write -> OK");
    /* The FATFS VFS turns "0 bytes fit" into -1 with ENOSPC, and returns a short
       count when only part of the data fit; both mean the volume is full. */
    check(sd_card_status_classify_write_result(-1, 512, ENOSPC) == SD_CARD_STATUS_FULL, "ENOSPC -> FULL");
    check(sd_card_status_classify_write_result(200, 512, 0) == SD_CARD_STATUS_FULL, "short write -> FULL");
    check(sd_card_status_classify_write_result(0, 512, 0) == SD_CARD_STATUS_FULL,
          "nothing written without an errno is still full, not a success");
    check(sd_card_status_classify_write_result(-1, 512, EIO) == SD_CARD_STATUS_FAULT, "EIO -> FAULT");
    check(sd_card_status_classify_write_result(-1, 512, EROFS) == SD_CARD_STATUS_FAULT, "EROFS -> FAULT");
    /* An old errno must not turn a failed write into a success or a "full". */
    check(sd_card_status_classify_write_result(-1, 512, 0) == SD_CARD_STATUS_FAULT,
          "failure with no errno -> FAULT");
}

static void test_full_threshold(void)
{
    const uint64_t kb = 1024U;

    check(sd_card_status_is_full(0, 1024U * kb), "no space at all is full");
    check(sd_card_status_is_full(1023U * kb, 1024U * kb), "just under the limit is full");
    check(!sd_card_status_is_full(1024U * kb, 1024U * kb), "exactly the limit is not full");
    check(!sd_card_status_is_full(8U * 1024U * 1024U * kb, 1024U * kb), "plenty of space is not full");
    /* A limit of 0 still has to catch a card with nothing left. */
    check(sd_card_status_is_full(0, 0), "limit 0: an empty card is full");
    check(!sd_card_status_is_full(1, 0), "limit 0: one byte free is not full");
}

static void test_retry_backoff(void)
{
    uint32_t wait = 0;
    uint32_t previous = 0;

    wait = sd_card_status_next_retry_ms(0, 5000, 60000);
    check(wait == 5000, "first retry waits the initial delay");

    wait = sd_card_status_next_retry_ms(wait, 5000, 60000);
    check(wait == 10000, "second retry doubles");
    wait = sd_card_status_next_retry_ms(wait, 5000, 60000);
    check(wait == 20000, "third retry doubles");
    wait = sd_card_status_next_retry_ms(wait, 5000, 60000);
    check(wait == 40000, "fourth retry doubles");
    wait = sd_card_status_next_retry_ms(wait, 5000, 60000);
    check(wait == 60000, "capped at the maximum, not 80000");

    previous = wait;
    for (int i = 0; i < 50; ++i) {
        wait = sd_card_status_next_retry_ms(wait, 5000, 60000);
    }
    check(wait == 60000 && previous == 60000, "stays at the maximum");

    /* No overflow near the top of the range, and a misconfigured initial delay
       above the cap must not escape it. */
    check(sd_card_status_next_retry_ms(0xF0000000U, 5000, 0xFFFFFFF0U) == 0xFFFFFFF0U,
          "no overflow with a huge maximum");
    check(sd_card_status_next_retry_ms(0, 90000, 60000) == 60000, "initial delay is clamped to the maximum");
    check(sd_card_status_next_retry_ms(7, 5000, 60000) == 5000, "a delay below the initial one restarts at it");
}

static void test_names_and_problems(void)
{
    check(strcmp(sd_card_status_state_name(SD_CARD_STATUS_OK), "OK") == 0, "OK has a name");
    check(strcmp(sd_card_status_state_name(SD_CARD_STATUS_NO_FILESYSTEM), "NO_FILESYSTEM") == 0,
          "NO_FILESYSTEM has a name");
    check(strcmp(sd_card_status_state_name((sd_card_status_state_t)99), "?") == 0, "unknown state is named, not crashed on");

    check(!sd_card_status_state_is_problem(SD_CARD_STATUS_DISABLED), "DISABLED shows no icon");
    check(!sd_card_status_state_is_problem(SD_CARD_STATUS_OK), "OK shows no icon");
    check(sd_card_status_state_is_problem(SD_CARD_STATUS_NO_CARD), "NO_CARD shows an icon");
    check(sd_card_status_state_is_problem(SD_CARD_STATUS_NO_FILESYSTEM), "NO_FILESYSTEM shows an icon");
    check(sd_card_status_state_is_problem(SD_CARD_STATUS_FULL), "FULL shows an icon");
    check(sd_card_status_state_is_problem(SD_CARD_STATUS_FAULT), "FAULT shows an icon");
}

int main(void)
{
    test_mount_error_classification();
    test_write_result_classification();
    test_full_threshold();
    test_retry_backoff();
    test_names_and_problems();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
