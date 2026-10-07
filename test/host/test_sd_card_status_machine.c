/*
 * SD card status state machine.
 *
 * There is no card detect pin, so everything the screen says about the card
 * comes from this state machine noticing things by polling: a card that is not
 * there, one that is not formatted, one that fills up, one that is pulled and
 * put back. The real source file is compiled here against a fake card, a fake
 * error log and a clock the test moves by hand, so each of those can be played
 * through without hardware.
 *
 * What these checks are for is the failure that a unit would not show until it
 * was on a wall: a state that flaps, a card that is never mounted again, a log
 * file created on every boot, a retry that never slows down.
 */

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

/* The self-test file goes through these instead of a card. Defined after the
   system headers so only the code under test is redirected. */
static int fake_open(const char *path, int flags, ...);
static ssize_t fake_write(int fd, const void *data, size_t size);
static int fake_fsync(int fd);
static int fake_close(int fd);
static int fake_unlink(const char *path);
#define open fake_open
#define write fake_write
#define fsync fake_fsync
#define close fake_close
#define unlink fake_unlink

#include "../../components/services/sd_card_status/sd_card_status.c"

#undef open
#undef write
#undef fsync
#undef close
#undef unlink

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

const char *esp_err_to_name(esp_err_t err)
{
    (void)err;
    return "ERR";
}

/* Two driver tags are enough to see whether the levels are turned off and back:
   one the mount path reports on, and the GPIO driver's, which prints a line per
   pin on every init and free. */
static esp_log_level_t g_vfs_level;
static esp_log_level_t g_gpio_level;
static int g_level_set_calls;

void esp_log_level_set(const char *tag, esp_log_level_t level)
{
    g_level_set_calls++;
    if (strcmp(tag, "vfs_fat_sdmmc") == 0) {
        g_vfs_level = level;
    } else if (strcmp(tag, "gpio") == 0) {
        g_gpio_level = level;
    }
}

esp_log_level_t esp_log_level_get(const char *tag)
{
    if (strcmp(tag, "vfs_fat_sdmmc") == 0) {
        return g_vfs_level;
    }
    if (strcmp(tag, "gpio") == 0) {
        return g_gpio_level;
    }
    return ESP_LOG_INFO;
}

/* ---- the clock ---- */

static TickType_t g_now;

TickType_t xTaskGetTickCount(void)
{
    return g_now;
}

void vTaskDelay(TickType_t ticks)
{
    g_now += ticks;
}

BaseType_t xTaskCreate(void (*task)(void *),
                       const char *name,
                       uint32_t stack_size,
                       void *arg,
                       unsigned priority,
                       TaskHandle_t *out_handle)
{
    (void)task;
    (void)name;
    (void)stack_size;
    (void)arg;
    (void)priority;
    (void)out_handle;
    return pdPASS;
}

/* ---- the fake world ---- */

#define MB (1024ULL * 1024ULL)

static struct {
    /* what sd_card_storage_init() returns while the card is "inserted" */
    esp_err_t init_result;
    esp_err_t probe_result;
    esp_err_t space_result;
    uint64_t total_bytes;
    uint64_t free_bytes;
    bool mounted;
    int init_calls;
    int deinit_calls;
    /* the driver log levels, as seen from inside each call */
    esp_log_level_t level_during_init;
    esp_log_level_t gpio_level_during_init;
    esp_log_level_t level_during_probe;
    esp_log_level_t gpio_level_during_deinit;
} card;

static struct {
    esp_err_t writer_start_result;
    esp_err_t log_start_result;
    bool started;
    bool failed;
    int writer_start_calls;
    int log_start_calls;
    int log_stop_calls;
    int lines;
    char last_line[160];
} logs;

static struct {
    int open_errno;
    int write_errno;
    /* -1: write everything; otherwise how many bytes the write reports */
    ssize_t write_reports;
    int fsync_errno;
    int open_calls;
} fs;

bool sd_card_storage_is_mounted(void)
{
    return card.mounted;
}

const char *sd_card_storage_get_mount_point(void)
{
    return "/sdcard";
}

esp_err_t sd_card_storage_init(void)
{
    card.init_calls++;
    card.level_during_init = g_vfs_level;
    card.gpio_level_during_init = g_gpio_level;
    if (card.mounted) {
        return ESP_OK;
    }
    if (card.init_result == ESP_OK) {
        card.mounted = true;
    }
    return card.init_result;
}

esp_err_t sd_card_storage_deinit(void)
{
    card.deinit_calls++;
    card.gpio_level_during_deinit = g_gpio_level;
    card.mounted = false;
    return ESP_OK;
}

esp_err_t sd_card_storage_probe(void)
{
    card.level_during_probe = g_vfs_level;
    return card.mounted ? card.probe_result : ESP_ERR_INVALID_STATE;
}

esp_err_t sd_card_storage_get_space(uint64_t *total_bytes, uint64_t *free_bytes)
{
    if (!card.mounted) {
        return ESP_ERR_INVALID_STATE;
    }
    if (card.space_result != ESP_OK) {
        return card.space_result;
    }
    *total_bytes = card.total_bytes;
    *free_bytes = card.free_bytes;
    return ESP_OK;
}

esp_err_t sd_card_writer_start(void)
{
    logs.writer_start_calls++;
    return logs.writer_start_result;
}

esp_err_t error_log_store_start(void)
{
    logs.log_start_calls++;
    if (logs.log_start_result != ESP_OK) {
        return logs.log_start_result;
    }
    logs.started = true;
    logs.failed = false;
    return ESP_OK;
}

esp_err_t error_log_store_stop(void)
{
    logs.log_stop_calls++;
    logs.started = false;
    logs.failed = false;
    return ESP_OK;
}

bool error_log_store_is_failed(void)
{
    return logs.started && logs.failed;
}

esp_err_t error_log_store_append_message(const char *tag, const char *message)
{
    (void)tag;
    /* Like the real one: with no stream running the line is dropped. */
    if (logs.started) {
        logs.lines++;
        snprintf(logs.last_line, sizeof(logs.last_line), "%s", message);
    }
    return ESP_OK;
}

static int fake_open(const char *path, int flags, ...)
{
    (void)path;
    (void)flags;
    fs.open_calls++;
    if (fs.open_errno != 0) {
        errno = fs.open_errno;
        return -1;
    }
    return 42;
}

static ssize_t fake_write(int fd, const void *data, size_t size)
{
    (void)fd;
    (void)data;
    if (fs.write_errno != 0) {
        errno = fs.write_errno;
        return -1;
    }
    return fs.write_reports >= 0 ? fs.write_reports : (ssize_t)size;
}

static int fake_fsync(int fd)
{
    (void)fd;
    if (fs.fsync_errno != 0) {
        errno = fs.fsync_errno;
        return -1;
    }
    return 0;
}

static int fake_close(int fd)
{
    (void)fd;
    return 0;
}

static int fake_unlink(const char *path)
{
    (void)path;
    return 0;
}

/* ---- scenario helpers ---- */

static void reset_world(void)
{
    memset(&card, 0, sizeof(card));
    memset(&logs, 0, sizeof(logs));
    memset(&fs, 0, sizeof(fs));
    fs.write_reports = -1;
    card.init_result = ESP_OK;
    card.probe_result = ESP_OK;
    card.space_result = ESP_OK;
    card.total_bytes = 8192ULL * MB;
    card.free_bytes = 4096ULL * MB;

    s_info = (sd_card_status_info_t){ .state = SD_CARD_STATUS_DISABLED, .last_error = ESP_OK };
    s_logging = false;
    s_no_room_for_test_write = false;
    s_card_seen = false;
    s_retry_ms = 0;
    g_vfs_level = ESP_LOG_INFO;
    g_gpio_level = ESP_LOG_INFO;
    g_level_set_calls = 0;
    s_mounted_since_tick = 0;
    g_now = 1000000;
}

/* One round of the task, then wait as long as it asked. */
static uint32_t round_trip(void)
{
    uint32_t wait_ms = sd_card_status_step();

    g_now += wait_ms;
    return wait_ms;
}

static sd_card_status_state_t state(void)
{
    return sd_card_status_get_state();
}

/* ---- scenarios ---- */

static void test_boot_with_a_working_card(void)
{
    reset_world();
    uint32_t wait = round_trip();

    check(state() == SD_CARD_STATUS_OK, "a working card mounts and reads OK");
    check(logs.writer_start_calls == 1 && logs.log_start_calls == 1 && logs.started,
          "the writer and the error log are started once");
    check(wait == CONFIG_SD_CARD_STATUS_POLL_INTERVAL_MS, "an OK card is polled, not retried");
    check(logs.lines == 0, "the first mount writes nothing, so a clean boot leaves no log file");

    sd_card_status_info_t info = { 0 };
    sd_card_status_get_info(&info);
    check(info.total_bytes == 8192ULL * MB && info.free_bytes == 4096ULL * MB,
          "the size is measured right away");
    check(info.revision == 1, "one change of state, one revision");

    for (int i = 0; i < 20; ++i) {
        (void)round_trip();
    }
    check(state() == SD_CARD_STATUS_OK && card.init_calls == 1 && logs.log_stop_calls == 0,
          "twenty healthy polls change nothing and remount nothing");
    check(sd_card_status_get_revision() == 1, "a steady state does not bump the revision (no needless redraw)");
}

static void test_no_card_then_insert(void)
{
    reset_world();
    card.init_result = ESP_ERR_TIMEOUT;

    uint32_t waits[7] = { 0 };
    for (int i = 0; i < 7; ++i) {
        waits[i] = round_trip();
    }

    check(state() == SD_CARD_STATUS_NO_CARD, "an empty slot reads NO_CARD");
    check(!logs.started && logs.writer_start_calls == 0 && logs.log_start_calls == 0,
          "nothing is started on a card that is not there");
    check(card.deinit_calls == 0, "a failed init leaves nothing to release");
    check(waits[0] == 5000 && waits[1] == 10000 && waits[2] == 20000 && waits[3] == 40000,
          "the retry delay doubles: 5, 10, 20, 40 seconds");
    check(waits[4] == 60000 && waits[5] == 60000 && waits[6] == 60000,
          "and stops at 60 seconds, so a unit without a card stays quiet");
    check(sd_card_status_get_revision() == 1, "repeated failures are one change, not seven");

    /* The card is put in. */
    card.init_result = ESP_OK;
    (void)round_trip();
    check(state() == SD_CARD_STATUS_OK, "a card inserted later is picked up without a reboot");
    check(logs.started && logs.log_start_calls == 1, "the error log starts then");
    check(logs.lines == 1 && strstr(logs.last_line, "NO_CARD -> OK") != NULL,
          "coming back is written to the new log, so the gap can be read from the card");
}

static void test_not_formatted(void)
{
    reset_world();
    card.init_result = ESP_FAIL;

    (void)round_trip();
    check(state() == SD_CARD_STATUS_NO_FILESYSTEM, "a card that answers but will not mount reads NO_FILESYSTEM");
    check(!logs.started, "nothing is written to it");

    /* Formatted on a PC and put back. */
    card.init_result = ESP_OK;
    (void)round_trip();
    check(state() == SD_CARD_STATUS_OK, "a formatted card is accepted at the next retry");
}

static void test_host_side_failure_is_not_no_card(void)
{
    reset_world();
    card.init_result = ESP_ERR_INVALID_STATE;
    (void)round_trip();
    check(state() == SD_CARD_STATUS_FAULT, "a bus that cannot be set up is FAULT, not 'insert a card'");
}

static void test_card_pulled_and_put_back(void)
{
    reset_world();
    (void)round_trip();
    check(state() == SD_CARD_STATUS_OK, "mounted");

    /* Pulled to read the log on a PC. */
    card.probe_result = ESP_ERR_TIMEOUT;
    uint32_t wait = round_trip();
    check(state() == SD_CARD_STATUS_NO_CARD, "a card that stops answering reads NO_CARD");
    check(logs.log_stop_calls == 1 && !logs.started, "the error log is closed before the card is released");
    check(card.deinit_calls == 1 && !card.mounted, "the card is unmounted so it can be mounted again");
    check(wait == 3000, "the first retry comes in 3 seconds");

    /* Still out: it keeps trying at the same short interval, so the icon goes
       away soon after the card is back, however long it was out. */
    card.init_result = ESP_ERR_TIMEOUT;
    bool steady = true;
    for (int i = 0; i < 15; ++i) {
        steady = steady && round_trip() == 3000 && state() == SD_CARD_STATUS_NO_CARD;
    }
    check(steady, "while it is out, the retries stay at 3 seconds, however long it takes");

    /* Put back. */
    card.init_result = ESP_OK;
    card.probe_result = ESP_OK;
    (void)round_trip();
    check(state() == SD_CARD_STATUS_OK, "put back, it mounts again");
    check(logs.started && logs.log_start_calls == 2, "and a fresh error log stream is opened");
    check(logs.lines == 1 && strstr(logs.last_line, "NO_CARD -> OK") != NULL, "the return is the first line of the new log");
}

static void test_unit_that_never_had_a_card_stays_quiet(void)
{
    reset_world();
    card.init_result = ESP_ERR_TIMEOUT;
    uint32_t wait = 0;
    for (int i = 0; i < 12; ++i) {
        wait = round_trip();
    }
    check(wait == 60000, "a unit that never saw a card settles at one attempt a minute");
    check(!s_card_seen, "and has not been marked as having had one");
}

static void test_write_failures_back_off_whatever_the_history(void)
{
    reset_world();
    (void)round_trip();
    check(state() == SD_CARD_STATUS_OK, "mounted");

    /* The card mounts, and the log stream fails soon after, again and again. A
       card that was seen does not get the short interval for this: it is a card
       that will not take writes, not a card that is out. */
    uint32_t waits[5] = { 0 };
    for (int i = 0; i < 5; ++i) {
        logs.failed = true;
        waits[i] = round_trip();
        check(state() == SD_CARD_STATUS_FAULT, "the failed stream reads FAULT");
        (void)round_trip(); /* mounts again at once, and is up for one poll only */
    }
    check(waits[0] == 5000 && waits[1] == 10000 && waits[2] == 20000 && waits[3] == 40000 && waits[4] == 60000,
          "the wait after each quick failure grows: 5, 10, 20, 40, 60 seconds");
}

static void test_write_failure_after_a_long_good_stretch(void)
{
    reset_world();
    (void)round_trip();
    check(state() == SD_CARD_STATUS_OK, "mounted");

    /* Two quick failures first, so the delay has grown. */
    logs.failed = true;
    (void)round_trip();
    (void)round_trip();
    logs.failed = true;
    uint32_t grown = round_trip();
    (void)round_trip();
    check(grown == 10000, "the delay has grown");

    g_now += 10U * 60U * 1000U; /* fine for ten minutes */
    logs.failed = true;
    check(round_trip() == 5000, "a failure after ten good minutes starts from the short delay again");
}

static void test_flapping_contact_does_not_get_slower_retries(void)
{
    reset_world();
    (void)round_trip();

    /* A loose connector: it answers, then does not, again and again. It is
       retried at the short interval each time, never given up on. */
    bool fast = true;
    for (int i = 0; i < 6; ++i) {
        card.probe_result = ESP_ERR_TIMEOUT;
        fast = fast && round_trip() == 3000;
        card.probe_result = ESP_OK;
        (void)round_trip();
        fast = fast && state() == SD_CARD_STATUS_OK;
    }
    check(fast, "a card that comes and goes is picked up every time, within 3 seconds of being put back");
}

static void test_repeat_attempts_are_quiet(void)
{
    reset_world();
    (void)round_trip();
    card.probe_result = ESP_ERR_TIMEOUT;
    card.init_result = ESP_ERR_TIMEOUT;
    (void)round_trip(); /* the card goes away: the first failure is reported */
    check(state() == SD_CARD_STATUS_NO_CARD, "card gone");

    g_level_set_calls = 0;
    (void)round_trip(); /* a repeat */
    check(card.level_during_init == ESP_LOG_NONE, "a repeat attempt runs with the driver logs off");
    check(card.gpio_level_during_init == ESP_LOG_NONE,
          "including the GPIO driver, which prints a line per pin on every init");
    check(g_vfs_level == ESP_LOG_INFO && g_gpio_level == ESP_LOG_INFO, "and turns them back on afterwards");
    check(g_level_set_calls > 0, "(the levels were really touched)");

    /* A different card in the slot. */
    card.init_result = ESP_OK;
    card.probe_result = ESP_OK;
    (void)round_trip();
    check(state() == SD_CARD_STATUS_OK && g_vfs_level == ESP_LOG_INFO && g_gpio_level == ESP_LOG_INFO,
          "after it mounts the levels are as they were");
}

static void test_pulling_the_card_is_quiet(void)
{
    reset_world();
    (void)round_trip();
    check(state() == SD_CARD_STATUS_OK && card.level_during_probe == ESP_LOG_NONE,
          "the presence probe runs with the driver error line turned off");

    card.probe_result = ESP_ERR_TIMEOUT;
    (void)round_trip();
    check(state() == SD_CARD_STATUS_NO_CARD, "pulled");
    check(card.gpio_level_during_deinit == ESP_LOG_NONE, "releasing the card does not print a line per pin");
    check(g_vfs_level == ESP_LOG_INFO && g_gpio_level == ESP_LOG_INFO, "the levels are back afterwards");
}

static void test_first_attempt_is_loud(void)
{
    reset_world();
    card.init_result = ESP_ERR_TIMEOUT;
    (void)round_trip();
    check(card.level_during_init == ESP_LOG_INFO && card.gpio_level_during_init == ESP_LOG_INFO,
          "the first failure keeps the driver logs on: it says why the card is missing");
}

static void test_card_fills_up(void)
{
    reset_world();
    card.free_bytes = 500ULL * 1024ULL; /* below the 1 MB limit */

    (void)round_trip();
    check(state() == SD_CARD_STATUS_FULL, "a card with too little space reads FULL right after mounting");
    check(card.mounted && card.deinit_calls == 0, "it stays mounted, so it can be read as it is");
    check(logs.log_stop_calls == 1 && !logs.started, "logging stops before it can run into a write error");
    check(logs.log_start_calls == 1, "it was started once for the mount, and not restarted");

    for (int i = 0; i < 10; ++i) {
        (void)round_trip();
    }
    check(state() == SD_CARD_STATUS_FULL && logs.log_stop_calls == 1 && logs.log_start_calls == 1 && card.init_calls == 1,
          "a full card does not flap: no restarts, no remounts");

    /* Space is freed (card cleaned on a PC and put back, or files removed). */
    card.free_bytes = 2048ULL * MB;
    (void)round_trip();
    check(state() == SD_CARD_STATUS_OK, "with space again it reads OK");
    check(logs.started && logs.log_start_calls == 2, "and logging resumes");
    check(logs.lines == 2 && strstr(logs.last_line, "FULL -> OK") != NULL,
          "the resume is recorded in the log, after the line that said why it stopped");
}

static void test_card_fills_up_while_running(void)
{
    reset_world();
    (void)round_trip();
    check(state() == SD_CARD_STATUS_OK, "mounted with plenty of room");

    card.free_bytes = 100ULL * 1024ULL;
    (void)round_trip();
    check(state() == SD_CARD_STATUS_FULL, "it turns FULL when the space runs low");
    check(logs.log_stop_calls == 1, "the log is stopped");
    check(logs.lines == 1 && strstr(logs.last_line, "OK -> FULL") != NULL,
          "and says why as its last line, while it still could");
}

static void test_card_with_no_room_for_a_test_write(void)
{
    reset_world();
    fs.write_errno = ENOSPC;

    (void)round_trip();
    check(state() == SD_CARD_STATUS_FULL, "no room even for the test write reads FULL");
    check(card.mounted, "still mounted");
    check(logs.writer_start_calls == 0 && logs.log_start_calls == 0, "nothing that writes is started on it");

    /* The free-space count is cached and may still claim room. A write that just
       failed outranks it, or the state would flip to OK and back. */
    for (int i = 0; i < 10; ++i) {
        (void)round_trip();
    }
    check(state() == SD_CARD_STATUS_FULL && logs.log_start_calls == 0 && card.init_calls == 1,
          "a stale free-space count does not talk it back to OK, and nothing flaps");

    /* The card is swapped for one with room. */
    card.probe_result = ESP_ERR_TIMEOUT;
    (void)round_trip();
    check(state() == SD_CARD_STATUS_NO_CARD, "the swap is noticed");
    card.probe_result = ESP_OK;
    fs.write_errno = 0;
    (void)round_trip();
    check(state() == SD_CARD_STATUS_OK && logs.started, "and the new card is used");
}

static void test_card_that_mounts_but_cannot_be_written(void)
{
    reset_world();
    fs.write_errno = EIO;

    uint32_t wait = round_trip();
    check(state() == SD_CARD_STATUS_FAULT, "a card that mounts and refuses a write reads FAULT");
    check(card.deinit_calls == 1 && !card.mounted, "it is released again");
    check(logs.log_start_calls == 0, "the error log is never started on it");
    check(wait == 5000, "and it is retried");

    fs.write_errno = 0;
    (void)round_trip();
    check(state() == SD_CARD_STATUS_OK, "it recovers once writing works");
}

static void test_card_that_cannot_create_the_test_file(void)
{
    reset_world();
    fs.open_errno = EROFS;
    (void)round_trip();
    check(state() == SD_CARD_STATUS_FAULT, "a read-only card reads FAULT");
}

static void test_log_stream_failure(void)
{
    reset_world();
    (void)round_trip();
    check(state() == SD_CARD_STATUS_OK, "mounted");

    /* The writer gave up on the stream (a write error). */
    logs.failed = true;
    (void)round_trip();
    check(state() == SD_CARD_STATUS_FAULT, "a failed error log stream reads FAULT");
    check(logs.log_stop_calls == 1 && card.deinit_calls == 1, "the stream is closed and the card released");

    (void)round_trip();
    check(state() == SD_CARD_STATUS_OK && logs.started && !logs.failed,
          "the next try mounts again with a fresh stream");
}

static void test_writer_cannot_start(void)
{
    reset_world();
    logs.writer_start_result = ESP_ERR_NO_MEM;
    (void)round_trip();
    check(state() == SD_CARD_STATUS_FAULT, "a writer that cannot start reads FAULT");
    check(card.deinit_calls == 1, "the card is released again");

    logs.writer_start_result = ESP_OK;
    (void)round_trip();
    check(state() == SD_CARD_STATUS_OK, "and it is tried again");
}

static void test_space_cannot_be_read(void)
{
    reset_world();
    card.space_result = ESP_FAIL;
    (void)round_trip();
    check(state() == SD_CARD_STATUS_FAULT, "free space that cannot be read reads FAULT");
    check(card.deinit_calls == 1 && logs.log_stop_calls == 1, "the card and the log are released");
}

static void test_state_before_start_is_quiet(void)
{
    reset_world();
    check(state() == SD_CARD_STATUS_DISABLED, "before the first mount there is nothing to show");
    check(!sd_card_status_state_is_problem(state()), "and no icon");
}

int main(void)
{
    test_state_before_start_is_quiet();
    test_boot_with_a_working_card();
    test_no_card_then_insert();
    test_not_formatted();
    test_host_side_failure_is_not_no_card();
    test_card_pulled_and_put_back();
    test_unit_that_never_had_a_card_stays_quiet();
    test_write_failures_back_off_whatever_the_history();
    test_write_failure_after_a_long_good_stretch();
    test_flapping_contact_does_not_get_slower_retries();
    test_repeat_attempts_are_quiet();
    test_pulling_the_card_is_quiet();
    test_first_attempt_is_loud();
    test_card_fills_up();
    test_card_fills_up_while_running();
    test_card_with_no_room_for_a_test_write();
    test_card_that_mounts_but_cannot_be_written();
    test_card_that_cannot_create_the_test_file();
    test_log_stream_failure();
    test_writer_cannot_start();
    test_space_cannot_be_read();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
