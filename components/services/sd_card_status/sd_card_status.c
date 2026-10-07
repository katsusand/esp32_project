#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "app_stack_monitor.h"
#include "error_log_store.h"
#include "sd_card_status.h"
#include "sd_card_storage.h"
#include "sd_card_writer.h"

#ifndef CONFIG_SD_CARD_STATUS_POLL_INTERVAL_MS
#define CONFIG_SD_CARD_STATUS_POLL_INTERVAL_MS 5000
#endif
#ifndef CONFIG_SD_CARD_STATUS_RETRY_INITIAL_MS
#define CONFIG_SD_CARD_STATUS_RETRY_INITIAL_MS 5000
#endif
#ifndef CONFIG_SD_CARD_STATUS_RETRY_SEEN_MS
#define CONFIG_SD_CARD_STATUS_RETRY_SEEN_MS 3000
#endif
#ifndef CONFIG_SD_CARD_STATUS_RETRY_MAX_MS
#define CONFIG_SD_CARD_STATUS_RETRY_MAX_MS 60000
#endif
#ifndef CONFIG_SD_CARD_STATUS_MIN_FREE_KB
#define CONFIG_SD_CARD_STATUS_MIN_FREE_KB 1024
#endif
#ifndef CONFIG_SD_CARD_STATUS_TASK_STACK_SIZE
#define CONFIG_SD_CARD_STATUS_TASK_STACK_SIZE 6144
#endif
#ifndef CONFIG_SD_CARD_STATUS_TASK_PRIORITY
#define CONFIG_SD_CARD_STATUS_TASK_PRIORITY 3
#endif

#define TAG "sd_card_status"
/* 8.3 name: the FATFS here has no long file name support. */
#define SD_CARD_STATUS_SELFTEST_FILE "SDCHK.TMP"
#define SD_CARD_STATUS_SELFTEST_BYTES 512U
/*
 * A card that stayed fine this long counts as having recovered; a failure after
 * that starts the retry delay over. A failure sooner (a card that mounts and
 * then fails its first write, say) keeps lengthening the delay instead, so a
 * bad card cannot turn this task into a mount loop.
 */
#define SD_CARD_STATUS_STABLE_MS 60000U
#define SD_CARD_STATUS_MIN_FREE_BYTES ((uint64_t)CONFIG_SD_CARD_STATUS_MIN_FREE_KB * 1024U)

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static sd_card_status_info_t s_info = { .state = SD_CARD_STATUS_DISABLED, .last_error = ESP_OK };
static TaskHandle_t s_task;

/* Monitor task only, from here on. */
static bool s_logging;
/*
 * The test write found no room on this mount, whatever the free-space count says.
 *
 * English contract: that count comes from FATFS' cached view (the FAT32 info
 * sector) and can be stale. Trusting it over a write that just failed would flip
 * the state back to OK, start logging, fail the first line, and go round again.
 * The flag lasts until the card is released, so only a swap can end it.
 */
static bool s_no_room_for_test_write;
static uint32_t s_retry_ms;
/* A mount has succeeded at least once since boot. */
static bool s_card_seen;
static TickType_t s_mounted_since_tick;

static void sd_card_status_publish(sd_card_status_state_t state, esp_err_t last_error)
{
    portENTER_CRITICAL(&s_lock);
    if (s_info.state != state) {
        ++s_info.revision;
    }
    s_info.state = state;
    s_info.last_error = last_error;
    if (state != SD_CARD_STATUS_OK && state != SD_CARD_STATUS_FULL) {
        s_info.total_bytes = 0;
        s_info.free_bytes = 0;
    }
    portEXIT_CRITICAL(&s_lock);
}

static void sd_card_status_publish_space(uint64_t total_bytes, uint64_t free_bytes)
{
    portENTER_CRITICAL(&s_lock);
    s_info.total_bytes = total_bytes;
    s_info.free_bytes = free_bytes;
    portEXIT_CRITICAL(&s_lock);
}

/*
 * Says what changed, once per change, on serial and (when it can) in the log.
 *
 * English contract: the very first mount is not written to the log. The error
 * log creates its file on the first line, and "an error-free boot leaves no file
 * behind" is the point of that: a line here on every boot would add one file per
 * boot until the 9999 names run out. Only a change after the card has been seen
 * is a line worth keeping (it went away, filled up, or came back).
 */
static void sd_card_status_note(sd_card_status_state_t previous, sd_card_status_state_t now, const char *detail)
{
    char message[96];

    if (previous == now) {
        return;
    }
    snprintf(message, sizeof(message), "SD card %s -> %s: %s",
             sd_card_status_state_name(previous), sd_card_status_state_name(now), detail);
    if (now == SD_CARD_STATUS_OK) {
        ESP_LOGI(TAG, "%s", message);
    } else {
        ESP_LOGW(TAG, "%s", message);
    }
    if (previous != SD_CARD_STATUS_DISABLED) {
        /* Dropped when the log is not running, which is the point of the state. */
        (void)error_log_store_append_message(TAG, message);
    }
}

static uint32_t sd_card_status_ms_since(TickType_t tick)
{
    return (uint32_t)((xTaskGetTickCount() - tick) * portTICK_PERIOD_MS);
}

/*
 * Quiet while the card is being brought up or released.
 *
 * English contract: the ESP-IDF drivers talk a lot here. A failed bring-up is an
 * error line per driver, and every SDSPI init and free prints one "GPIO[n]|"
 * line per pin from the GPIO driver. The first failure is worth reading - it
 * says why the card is missing - but the same dozen lines every few seconds
 * while a card stays out bury everything else on the serial log. So the first
 * attempt of a problem is loud, and repeats, the release of a card and the
 * presence probe run with these tags turned off and put back afterwards. The
 * "gpio" tag is shared with the rest of the firmware; what it would say on
 * another task in those few hundred milliseconds is lost.
 */
static const char *const s_driver_log_tags[] = {
    "vfs_fat_sdmmc", "sdmmc_common", "sdmmc_init", "sdmmc_cmd", "sdspi_transaction", "sdspi_host", "gpio",
};
#define SD_CARD_STATUS_DRIVER_TAG_COUNT (sizeof(s_driver_log_tags) / sizeof(s_driver_log_tags[0]))

typedef struct {
    esp_log_level_t levels[SD_CARD_STATUS_DRIVER_TAG_COUNT];
} sd_card_status_log_guard_t;

static void sd_card_status_quiet_begin(sd_card_status_log_guard_t *guard)
{
    for (size_t i = 0; i < SD_CARD_STATUS_DRIVER_TAG_COUNT; ++i) {
        guard->levels[i] = esp_log_level_get(s_driver_log_tags[i]);
        esp_log_level_set(s_driver_log_tags[i], ESP_LOG_NONE);
    }
}

static void sd_card_status_quiet_end(const sd_card_status_log_guard_t *guard)
{
    for (size_t i = 0; i < SD_CARD_STATUS_DRIVER_TAG_COUNT; ++i) {
        esp_log_level_set(s_driver_log_tags[i], guard->levels[i]);
    }
}

/* Closes the log and releases the card, so the next mount starts clean. */
static void sd_card_status_teardown(void)
{
    esp_err_t err = error_log_store_stop();
    if (err != ESP_OK) {
        /* Expected after a pulled card: its last writes could not be flushed. */
        ESP_LOGW(TAG, "error log closed with %s", esp_err_to_name(err));
    }
    s_logging = false;
    sd_card_status_log_guard_t guard;
    sd_card_status_quiet_begin(&guard);
    err = sd_card_storage_deinit();
    sd_card_status_quiet_end(&guard);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "card released with %s", esp_err_to_name(err));
    }
}

/*
 * Enters a state that needs a retry, and picks the delay before it.
 *
 * English contract, two kinds of wait:
 *  - A card that has worked since boot and is now missing or unformatted is
 *    retried at a short fixed interval. That is somebody swapping the card, and
 *    they are waiting for the icon to go away.
 *  - Everything else backs off: a unit that never had a card, and a card that
 *    mounts but will not take a write. Those can go on for ever and must not
 *    keep the SPI bus and the serial log busy. A card that was mounted for a
 *    while starts again from the short end of that range.
 */
static void sd_card_status_enter_problem(sd_card_status_state_t state, esp_err_t err, const char *detail)
{
    sd_card_status_state_t previous = s_info.state;
    bool was_mounted = previous == SD_CARD_STATUS_OK || previous == SD_CARD_STATUS_FULL;
    bool was_stable = was_mounted && sd_card_status_ms_since(s_mounted_since_tick) >= SD_CARD_STATUS_STABLE_MS;
    bool card_swap = s_card_seen && (state == SD_CARD_STATUS_NO_CARD || state == SD_CARD_STATUS_NO_FILESYSTEM);

    if (card_swap) {
        s_retry_ms = CONFIG_SD_CARD_STATUS_RETRY_SEEN_MS;
    } else {
        s_retry_ms = was_stable ? CONFIG_SD_CARD_STATUS_RETRY_INITIAL_MS
                                : sd_card_status_next_retry_ms(s_retry_ms,
                                                               CONFIG_SD_CARD_STATUS_RETRY_INITIAL_MS,
                                                               CONFIG_SD_CARD_STATUS_RETRY_MAX_MS);
    }
    sd_card_status_publish(state, err);
    sd_card_status_note(previous, state, detail);
}

/*
 * Proves the card takes a write before it is trusted. A write-protected, worn
 * or full card mounts without complaint and only fails on its first real write,
 * which would otherwise be the first error line - long after anyone is looking.
 */
static sd_card_status_state_t sd_card_status_selftest_write(void)
{
    char path[48];
    uint8_t block[SD_CARD_STATUS_SELFTEST_BYTES];
    int written_path = snprintf(path, sizeof(path), "%s/%s",
                                sd_card_storage_get_mount_point(), SD_CARD_STATUS_SELFTEST_FILE);

    if (written_path <= 0 || (size_t)written_path >= sizeof(path)) {
        return SD_CARD_STATUS_FAULT;
    }
    memset(block, 0xA5, sizeof(block));

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) {
        int open_errno = errno;
        ESP_LOGW(TAG, "self-test: open failed (errno %d)", open_errno);
        return sd_card_status_classify_write_result(-1, sizeof(block), open_errno);
    }
    ssize_t written = write(fd, block, sizeof(block));
    int write_errno = errno;
    sd_card_status_state_t result = sd_card_status_classify_write_result(written, sizeof(block), write_errno);
    if (result == SD_CARD_STATUS_OK && fsync(fd) != 0) {
        write_errno = errno;
        result = sd_card_status_classify_write_result(-1, sizeof(block), write_errno);
    }
    (void)close(fd);
    (void)unlink(path);
    if (result != SD_CARD_STATUS_OK) {
        ESP_LOGW(TAG, "self-test: write failed (wrote %d of %u, errno %d)",
                 (int)written, (unsigned)sizeof(block), write_errno);
    }
    return result;
}

static void sd_card_status_try_mount(void)
{
    /* A repeat of a failure that is already on screen and in the log. */
    bool repeat = s_info.state == SD_CARD_STATUS_NO_CARD || s_info.state == SD_CARD_STATUS_NO_FILESYSTEM;
    sd_card_status_log_guard_t guard;

    if (repeat) {
        sd_card_status_quiet_begin(&guard);
    }
    esp_err_t err = sd_card_storage_init();
    if (repeat) {
        sd_card_status_quiet_end(&guard);
    }
    if (err == ESP_OK) {
        s_card_seen = true;
    }
    if (err != ESP_OK) {
        /* Nothing is left allocated after a failed init. */
        sd_card_status_enter_problem(sd_card_status_classify_mount_error(err), err, esp_err_to_name(err));
        return;
    }

    sd_card_status_state_t write_state = sd_card_status_selftest_write();
    s_no_room_for_test_write = write_state == SD_CARD_STATUS_FULL;
    if (write_state == SD_CARD_STATUS_FAULT) {
        sd_card_status_teardown();
        sd_card_status_enter_problem(SD_CARD_STATUS_FAULT, ESP_FAIL, "mounted, but a test write failed");
        return;
    }
    if (write_state == SD_CARD_STATUS_FULL) {
        /* No room even for the test file. Stay mounted so the card can still be
           read, but start nothing that would write: FULL means mounted. */
        sd_card_status_state_t previous = s_info.state;
        s_mounted_since_tick = xTaskGetTickCount();
        sd_card_status_publish(SD_CARD_STATUS_FULL, ESP_FAIL);
        sd_card_status_note(previous, SD_CARD_STATUS_FULL, "no room for a test write; logging off");
        return;
    }

    err = sd_card_writer_start();
    if (err == ESP_OK) {
        err = error_log_store_start();
    }
    if (err != ESP_OK) {
        sd_card_status_teardown();
        sd_card_status_enter_problem(SD_CARD_STATUS_FAULT, err, "could not start the error log");
        return;
    }

    s_logging = true;
    s_mounted_since_tick = xTaskGetTickCount();
    sd_card_status_state_t previous = s_info.state;
    sd_card_status_publish(SD_CARD_STATUS_OK, ESP_OK);
    sd_card_status_note(previous, SD_CARD_STATUS_OK, "mounted, logging");
}

/* The card is mounted: is it still there, still writable, still roomy? */
static void sd_card_status_check_mounted(void)
{
    /* A pulled card is the expected way for this to fail, so the driver's own
       error line is left out; the error name goes into ours instead. */
    sd_card_status_log_guard_t guard;
    sd_card_status_quiet_begin(&guard);
    esp_err_t err = sd_card_storage_probe();
    sd_card_status_quiet_end(&guard);
    if (err != ESP_OK) {
        char detail[64];
        snprintf(detail, sizeof(detail), "card stopped answering (%s)", esp_err_to_name(err));
        sd_card_status_teardown();
        sd_card_status_enter_problem(SD_CARD_STATUS_NO_CARD, err, detail);
        return;
    }
    if (s_logging && error_log_store_is_failed()) {
        sd_card_status_teardown();
        sd_card_status_enter_problem(SD_CARD_STATUS_FAULT, ESP_FAIL, "writing to the card failed");
        return;
    }

    uint64_t total_bytes = 0;
    uint64_t free_bytes = 0;
    err = sd_card_storage_get_space(&total_bytes, &free_bytes);
    if (err != ESP_OK) {
        sd_card_status_teardown();
        sd_card_status_enter_problem(SD_CARD_STATUS_FAULT, err, "could not read the free space");
        return;
    }
    sd_card_status_publish_space(total_bytes, free_bytes);

    bool full = s_no_room_for_test_write ||
                sd_card_status_is_full(free_bytes, SD_CARD_STATUS_MIN_FREE_BYTES);
    sd_card_status_state_t state = s_info.state;
    if (full && state == SD_CARD_STATUS_OK) {
        /* Say so while the log still works, then stop it before it runs into a
           write error. The card stays mounted so it can be read in place. */
        char detail[64];
        snprintf(detail, sizeof(detail), "%" PRIu64 " KB free; logging stopped", free_bytes / 1024U);
        sd_card_status_publish(SD_CARD_STATUS_FULL, ESP_FAIL);
        sd_card_status_note(SD_CARD_STATUS_OK, SD_CARD_STATUS_FULL, detail);
        (void)error_log_store_stop();
        s_logging = false;
    } else if (!full && state == SD_CARD_STATUS_FULL) {
        err = error_log_store_start();
        if (err != ESP_OK) {
            sd_card_status_teardown();
            sd_card_status_enter_problem(SD_CARD_STATUS_FAULT, err, "could not restart the error log");
            return;
        }
        s_logging = true;
        sd_card_status_publish(SD_CARD_STATUS_OK, ESP_OK);
        sd_card_status_note(SD_CARD_STATUS_FULL, SD_CARD_STATUS_OK, "space available again, logging");
    }
}

/* OK and FULL both mean "the card is mounted". */
static bool sd_card_status_is_mounted_state(sd_card_status_state_t state)
{
    return state == SD_CARD_STATUS_OK || state == SD_CARD_STATUS_FULL;
}

/*
 * One round of work. Returns how long to wait before the next one: the poll
 * interval while the card is mounted, the retry delay while it is not.
 *
 * English contract: split from the task so a host test can drive the state
 * machine with a fake card and a fake clock.
 */
static uint32_t sd_card_status_step(void)
{
    if (!sd_card_status_is_mounted_state(s_info.state)) {
        sd_card_status_try_mount();
    }
    /* Also right after a mount, so a card that is already full says so now
       instead of after the first poll interval. */
    if (sd_card_status_is_mounted_state(s_info.state)) {
        sd_card_status_check_mounted();
    }

    return sd_card_status_is_mounted_state(s_info.state) ? (uint32_t)CONFIG_SD_CARD_STATUS_POLL_INTERVAL_MS
                                                         : s_retry_ms;
}

static void sd_card_status_task(void *arg)
{
    (void)arg;

    while (true) {
        APP_STACK_MONITOR_CHECK(TAG, "sd_card_status", 30000);
        vTaskDelay(pdMS_TO_TICKS(sd_card_status_step()));
    }
}

esp_err_t sd_card_status_start(void)
{
#if CONFIG_SD_CARD_STORAGE_ENABLED
    if (s_task != NULL) {
        return ESP_OK;
    }
    BaseType_t task_ok = xTaskCreate(sd_card_status_task,
                                     "sd_card_status",
                                     CONFIG_SD_CARD_STATUS_TASK_STACK_SIZE,
                                     NULL,
                                     CONFIG_SD_CARD_STATUS_TASK_PRIORITY,
                                     &s_task);
    if (task_ok != pdPASS) {
        s_task = NULL;
        ESP_LOGE(TAG, "task create failed");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
#else
    return ESP_OK;
#endif
}

sd_card_status_state_t sd_card_status_get_state(void)
{
    portENTER_CRITICAL(&s_lock);
    sd_card_status_state_t state = s_info.state;
    portEXIT_CRITICAL(&s_lock);
    return state;
}

uint32_t sd_card_status_get_revision(void)
{
    portENTER_CRITICAL(&s_lock);
    uint32_t revision = s_info.revision;
    portEXIT_CRITICAL(&s_lock);
    return revision;
}

void sd_card_status_get_info(sd_card_status_info_t *info)
{
    if (info == NULL) {
        return;
    }
    portENTER_CRITICAL(&s_lock);
    *info = s_info;
    portEXIT_CRITICAL(&s_lock);
}
