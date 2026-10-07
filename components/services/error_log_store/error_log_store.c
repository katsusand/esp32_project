#include <dirent.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_app_desc.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "boot_id.h"
#include "error_log_dedupe.h"
#include "error_log_format.h"
#include "error_log_hold.h"
#include "error_log_naming.h"
#include "error_log_store.h"
#include "sd_card_storage.h"
#include "sd_card_writer.h"

/*
 * The file names are long ("ERR_2026-10-07_01.LOG"), which the FATFS only takes
 * with long file names on. Without it every open fails with FR_INVALID_NAME and
 * the log would silently never be written, so the build stops here instead.
 */
#if CONFIG_FATFS_LFN_NONE
#error "error_log_store writes long file names: enable CONFIG_FATFS_LFN_HEAP (sdkconfig.defaults sets it, but an existing sdkconfig keeps its old value). Run `idf.py menuconfig` > Component config > FAT Filesystem support > Long filename support, or delete the sdkconfig file in the project root and reconfigure. Note that `idf.py fullclean` removes build/ only, not sdkconfig, and opening menuconfig without changing the option keeps the old value, so neither fixes this by itself."
#endif

#ifndef CONFIG_ERROR_LOG_STORE_HOLD_BYTES
#define CONFIG_ERROR_LOG_STORE_HOLD_BYTES 4096
#endif
#ifndef CONFIG_ERROR_LOG_STORE_TIME_WAIT_SECONDS
#define CONFIG_ERROR_LOG_STORE_TIME_WAIT_SECONDS 180
#endif
#ifndef CONFIG_ERROR_LOG_STORE_REPEAT_WINDOW_SECONDS
#define CONFIG_ERROR_LOG_STORE_REPEAT_WINDOW_SECONDS 10
#endif

/* Roughly 4000 lines. A line is never split, so a file ends on a whole line. */
#define ERROR_LOG_MAX_FILE_SIZE (256U * 1024U)
/*
 * A file counts as full this much before the limit when choosing where to
 * append. The writer ends a file when the next line would not fit, which leaves
 * it a little UNDER the limit; without this margin that file would still look
 * like it had room, be chosen again, and be left again at the very next line,
 * each time with another boot line written into it.
 */
#define ERROR_LOG_FILE_RESERVE_BYTES 1024U
/* Room for a full hold released at once, plus what arrives meanwhile. */
#define ERROR_LOG_BUFFER_SIZE 8192U
/* Together with the stamp these keep a line inside ERROR_LOG_LINE_MAX. */
#define ERROR_LOG_MESSAGE_MAX 160U
#define ERROR_LOG_BODY_MAX 208U
#define ERROR_LOG_STAMP_MAX 56U
/* How long releasing the hold may wait for room, per line. */
#define ERROR_LOG_RELEASE_WAIT_MS 200U
/* 2024-01-01 00:00:00 UTC: a clock before this was never set (the same rule the clock app uses). */
#define ERROR_LOG_CLOCK_VALID_EPOCH ((time_t)1704067200)
#define ERROR_LOG_TIME_WAIT_MS ((uint64_t)CONFIG_ERROR_LOG_STORE_TIME_WAIT_SECONDS * 1000U)
#define ERROR_LOG_DATE_TEXT_MAX 11U

static const char *ERROR_LOG_TAG = "error_log_store";

/*
 * Where a line goes:
 *
 *   report -> fold repeats -> sink open?  yes: written to the card writer
 *                                         no:  held in RAM
 *
 * The sink opens when the card is ready AND the clock question is answered (see
 * error_log_store_notify_time_decided()); the held lines are then written, in
 * order, and later ones follow directly.
 *
 * English contract: nothing here waits for the card. A producer either queues
 * its line in the card writer (which never blocks it) or puts it in the hold. A
 * line is never both, and never overtakes an older one that is still held: a
 * producer that finds the sink closed pushes into the hold in the same critical
 * section in which the releaser checks that the hold is empty before opening it.
 */
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
/* All guarded by s_lock: */
static sd_card_writer_stream_t *s_stream;
static bool s_open;
static bool s_releasing;
static bool s_card_ready;
static uint32_t s_in_flight;
static bool s_decided;
static const char *s_ntp_text = "pending";
static uint8_t s_hold_storage[CONFIG_ERROR_LOG_STORE_HOLD_BYTES];
static error_log_hold_t s_hold = { .storage = s_hold_storage, .capacity = sizeof(s_hold_storage) };
static error_log_dedupe_t s_dedupe = { .window_ms = (uint32_t)CONFIG_ERROR_LOG_STORE_REPEAT_WINDOW_SECONDS * 1000U };
/* The date in the name of the file being written, and the date a rotation was last asked for. */
static char s_file_date[ERROR_LOG_DATE_TEXT_MAX];
static char s_rotate_date[ERROR_LOG_DATE_TEXT_MAX];
/*
 * The uptime of the oldest line being released from the hold, for the boot line
 * of the file those lines open.
 *
 * English contract: a boot line is stamped when its file is opened, which for
 * the first file after a release is later than the held lines that follow it.
 * Read in order, the log would step back in time. The boot line takes the oldest
 * held line's time instead, so a file always starts with its oldest line. It is
 * set when the hold is released and used (and cleared) by the next file the
 * writer opens, which is the file those lines go to.
 */
static uint64_t s_header_uptime_hint_ms;
static bool s_header_uptime_hint_valid;
static uint32_t s_part;
static esp_timer_handle_t s_gate_timer;
static esp_timer_handle_t s_repeat_timer;
static bool s_repeat_timer_creating;
static bool s_sink_warning_emitted;

static uint64_t error_log_uptime_ms(void)
{
    return (uint64_t)(esp_timer_get_time() / 1000LL);
}

static bool error_log_clock_is_valid(time_t wall)
{
    return wall >= ERROR_LOG_CLOCK_VALID_EPOCH;
}

static void error_log_date_text(const struct tm *local, char *out, size_t out_size)
{
    (void)snprintf(out, out_size, "%04d-%02d-%02d", local->tm_year + 1900, local->tm_mon + 1, local->tm_mday);
}

static void error_log_warn_sink_failure_once(esp_err_t err)
{
    bool first = false;

    portENTER_CRITICAL(&s_lock);
    if (!s_sink_warning_emitted) {
        s_sink_warning_emitted = true;
        first = true;
    }
    portEXIT_CRITICAL(&s_lock);
    if (first) {
        ESP_LOGW(ERROR_LOG_TAG,
                 "SD error log unavailable; further failures are not reported: error log line dropped (%s)",
                 esp_err_to_name(err));
    }
}

/* ---- the file the writer opens ---- */

static const char *error_log_reset_reason_name(esp_reset_reason_t reason)
{
    switch (reason) {
    case ESP_RST_POWERON:
        return "POWERON";
    case ESP_RST_EXT:
        return "EXT";
    case ESP_RST_SW:
        return "SW";
    case ESP_RST_PANIC:
        return "PANIC";
    case ESP_RST_INT_WDT:
        return "INT_WDT";
    case ESP_RST_TASK_WDT:
        return "TASK_WDT";
    case ESP_RST_WDT:
        return "WDT";
    case ESP_RST_DEEPSLEEP:
        return "DEEPSLEEP";
    case ESP_RST_BROWNOUT:
        return "BROWNOUT";
    default:
        return "OTHER";
    }
}

/* True when the file has bytes and the last one is not a newline: the run that wrote it died mid-line. */
static bool error_log_file_lacks_final_newline(const char *path, uint64_t size)
{
    if (size == 0U) {
        return false;
    }
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return false;
    }
    bool lacks = false;
    if (fseek(file, -1, SEEK_END) == 0) {
        int last = fgetc(file);
        lacks = last != EOF && last != '\n';
    }
    fclose(file);
    return lacks;
}

/*
 * Runs on the writer task each time it needs a file: at the first line, when the
 * file is full, and after a rotation for a new date. Today's newest file with
 * room is appended to; otherwise the next number is created. Either way the file
 * starts (or the appended section starts) with a `boot:` line.
 */
static esp_err_t error_log_store_target(sd_card_writer_target_t *target, void *ctx)
{
    (void)ctx;

    ESP_RETURN_ON_FALSE(sd_card_storage_is_mounted(), ESP_ERR_INVALID_STATE, ERROR_LOG_TAG, "sd card is not mounted");
    const char *mount = sd_card_storage_get_mount_point();
    ESP_RETURN_ON_FALSE(mount != NULL, ESP_ERR_INVALID_STATE, ERROR_LOG_TAG, "sd mount point unavailable");

    time_t now = time(NULL);
    struct tm local = { 0 };
    localtime_r(&now, &local);
    error_log_date_t today = { .year = local.tm_year + 1900, .month = local.tm_mon + 1, .day = local.tm_mday };

    error_log_scan_t scan = { 0 };
    DIR *dir = opendir(mount);
    ESP_RETURN_ON_FALSE(dir != NULL, ESP_FAIL, ERROR_LOG_TAG, "cannot list the card");
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        error_log_date_t date;
        unsigned sequence = 0;
        if (error_log_naming_parse(entry->d_name, &date, &sequence) &&
            error_log_naming_same_date(&date, &today) &&
            (!scan.found || sequence > scan.max_sequence)) {
            scan.found = true;
            scan.max_sequence = sequence;
        }
    }
    closedir(dir);

    /*
     * Static on purpose. This runs on the card writer's task, which has a 4 KB
     * stack and is already inside the directory and FAT code here, and nothing
     * else calls it, one call at a time: these buffers cost nothing on that stack.
     */
    static char name[ERROR_LOG_NAME_MAX];
    static char full_path[96];
    if (scan.found) {
        /* FAT looks names up without regard to case, so the upper case name
           reaches a file a PC may have renamed to lower case. */
        if (error_log_naming_build(name, sizeof(name), &today, scan.max_sequence)) {
            struct stat st;
            (void)snprintf(full_path, sizeof(full_path), "%s/%s", mount, name);
            scan.max_sequence_size = stat(full_path, &st) == 0 ? (uint64_t)st.st_size : 0U;
        }
    }

    error_log_target_t chosen = { 0 };
    ESP_RETURN_ON_FALSE(error_log_naming_resolve(&scan, ERROR_LOG_MAX_FILE_SIZE - ERROR_LOG_FILE_RESERVE_BYTES, &chosen),
                        ESP_ERR_INVALID_STATE,
                        ERROR_LOG_TAG,
                        "the log file numbers for today are used up");
    ESP_RETURN_ON_FALSE(error_log_naming_build(name, sizeof(name), &today, chosen.sequence),
                        ESP_ERR_INVALID_SIZE,
                        ERROR_LOG_TAG,
                        "log file name does not fit");
    ESP_RETURN_ON_FALSE(strlen(name) < sizeof(target->path), ESP_ERR_INVALID_SIZE, ERROR_LOG_TAG, "log file path is too long");
    strlcpy(target->path, name, sizeof(target->path));
    target->append = chosen.append;

    bool needs_newline = false;
    if (chosen.append) {
        (void)snprintf(full_path, sizeof(full_path), "%s/%s", mount, name);
        needs_newline = error_log_file_lacks_final_newline(full_path, scan.max_sequence_size);
    }

    char today_text[ERROR_LOG_DATE_TEXT_MAX];
    error_log_date_text(&local, today_text, sizeof(today_text));
    uint64_t uptime = error_log_uptime_ms();
    uint64_t header_uptime = uptime;
    portENTER_CRITICAL(&s_lock);
    uint32_t part = ++s_part;
    strlcpy(s_file_date, today_text, sizeof(s_file_date));
    const char *ntp = s_ntp_text;
    if (s_header_uptime_hint_valid && s_header_uptime_hint_ms < header_uptime) {
        header_uptime = s_header_uptime_hint_ms;
    }
    s_header_uptime_hint_valid = false;
    portEXIT_CRITICAL(&s_lock);

    /* The boot line's own clock: now, or the oldest line it introduces. */
    struct tm header_local = { 0 };
    time_t header_wall = error_log_restamp(now, uptime, header_uptime);
    localtime_r(&header_wall, &header_local);

    static char body[ERROR_LOG_BODY_MAX];
    static char stamp[ERROR_LOG_STAMP_MAX];
    static char line[ERROR_LOG_LINE_MAX];
    const esp_app_desc_t *app = esp_app_get_description();
#if defined(APP_DEV) && APP_DEV
    const int dev = 1;
#else
    const int dev = 0;
#endif
    (void)snprintf(body,
                   sizeof(body),
                   "boot: id=%s part=%u fw=%s reset=%s dev=%d clock=%s ntp=%s",
                   boot_id_get(),
                   (unsigned)part,
                   app != NULL ? app->version : "?",
                   error_log_reset_reason_name(esp_reset_reason()),
                   dev,
                   error_log_clock_is_valid(now) ? "set" : "unset",
                   ntp);
    (void)error_log_format_stamp(stamp, sizeof(stamp), &header_local, header_uptime);
    size_t length = error_log_format_line(line, sizeof(line), stamp, body);

    size_t at = 0;
    if (needs_newline) {
        target->header[at++] = '\n';
    }
    if (length > sizeof(target->header) - at) {
        length = sizeof(target->header) - at;
    }
    memcpy(&target->header[at], line, length);
    target->header_len = at + length;
    return ESP_OK;
}

/* SD writer trouble (dropped records, failed streams) goes into this log. */
static void error_log_store_on_sd_report(const char *line, void *ctx)
{
    (void)ctx;
    (void)error_log_store_append_message("sd_card_writer", line);
}

static esp_err_t error_log_store_open_stream(sd_card_writer_stream_t **out)
{
    const sd_card_writer_stream_config_t config = {
        .name = "error_log",
        .target_fn = error_log_store_target,
        .buffer_size = ERROR_LOG_BUFFER_SIZE,
        .flush_interval_ms = 0,
        .max_file_size = ERROR_LOG_MAX_FILE_SIZE,
        /* It carries the reports, so it must not report about itself. */
        .silent = true,
    };

    ESP_RETURN_ON_ERROR(sd_card_writer_open_stream(&config, out), ERROR_LOG_TAG, "error log stream failed");
    sd_card_writer_set_report_fn(error_log_store_on_sd_report, NULL);
    return ESP_OK;
}

/* ---- writing one entry ---- */

/* The date moved on (the clock was set, or midnight passed): the next line starts a new file. */
static void error_log_store_rotate_if_date_moved(sd_card_writer_stream_t *stream, const struct tm *today)
{
    char today_text[ERROR_LOG_DATE_TEXT_MAX];
    bool rotate = false;

    error_log_date_text(today, today_text, sizeof(today_text));
    portENTER_CRITICAL(&s_lock);
    if (s_file_date[0] != '\0' && strcmp(s_file_date, today_text) != 0 && strcmp(s_rotate_date, today_text) != 0) {
        strlcpy(s_rotate_date, today_text, sizeof(s_rotate_date));
        rotate = true;
    }
    portEXIT_CRITICAL(&s_lock);
    if (rotate) {
        /* ESP_ERR_NO_MEM: rotations are already queued and will reach the same place. */
        (void)sd_card_writer_rotate(stream);
    }
}

/*
 * Stamps one entry with the time it was made and queues it. The wall clock is
 * worked out from the uptime it carries, so a line held while the clock was
 * unset gets the real time once the clock is set.
 */
static esp_err_t error_log_store_write_entry(sd_card_writer_stream_t *stream,
                                             uint64_t entry_uptime_ms,
                                             bool raw,
                                             const char *text,
                                             size_t length,
                                             TickType_t wait_ticks)
{
    char line[ERROR_LOG_LINE_MAX];
    size_t line_length = 0;

    if (raw) {
        /* Already a finished line with its own stamp; make sure it is one line. */
        line_length = length < sizeof(line) - 2U ? length : sizeof(line) - 2U;
        memcpy(line, text, line_length);
        while (line_length > 0 && (line[line_length - 1U] == '\n' || line[line_length - 1U] == '\r')) {
            --line_length;
        }
        line[line_length++] = '\n';
        line[line_length] = '\0';
        return sd_card_writer_write(stream, line, line_length, wait_ticks);
    }

    time_t now = time(NULL);
    uint64_t now_uptime = error_log_uptime_ms();
    struct tm entry_time = { 0 };
    struct tm today = { 0 };
    char stamp[ERROR_LOG_STAMP_MAX];
    time_t entry_wall = error_log_restamp(now, now_uptime, entry_uptime_ms);

    localtime_r(&entry_wall, &entry_time);
    localtime_r(&now, &today);
    error_log_store_rotate_if_date_moved(stream, &today);
    (void)error_log_format_stamp(stamp, sizeof(stamp), &entry_time, entry_uptime_ms);
    line_length = error_log_format_line(line, sizeof(line), stamp, text);
    return sd_card_writer_write(stream, line, line_length, wait_ticks);
}

/*
 * Caller holds s_lock. True when the clock question is answered: by an answer, by
 * the clock already being right, or by running out of time.
 *
 * English contract: `clock_valid` is read by the caller BEFORE it takes the lock.
 * time() takes a mutex inside the C library, and a mutex must not be taken inside
 * a critical section.
 */
static bool error_log_store_gate_satisfied_locked(uint64_t now_uptime_ms, bool clock_valid)
{
    if (!s_decided) {
        if (clock_valid) {
            s_decided = true;
            s_ntp_text = "preset";
        } else if (now_uptime_ms >= ERROR_LOG_TIME_WAIT_MS) {
            s_decided = true;
            s_ntp_text = "timeout";
        }
    }
    return s_decided;
}

/*
 * Opens the sink: opens the stream if it is not, writes what was held, and lets
 * later lines go straight through. ESP_OK when nothing needed doing; an error
 * only when the stream could not be opened (the lines stay held).
 */
static esp_err_t error_log_store_release(void)
{
    sd_card_writer_stream_t *stream = NULL;

    portENTER_CRITICAL(&s_lock);
    if (s_open || s_releasing || !s_card_ready) {
        portEXIT_CRITICAL(&s_lock);
        return ESP_OK;
    }
    s_releasing = true;
    stream = s_stream;
    portEXIT_CRITICAL(&s_lock);

    if (stream == NULL) {
        esp_err_t err = error_log_store_open_stream(&stream);
        if (err != ESP_OK) {
            portENTER_CRITICAL(&s_lock);
            s_releasing = false;
            portEXIT_CRITICAL(&s_lock);
            return err;
        }
        portENTER_CRITICAL(&s_lock);
        bool stopped_meanwhile = !s_card_ready;
        if (!stopped_meanwhile) {
            s_stream = stream;
        } else {
            s_releasing = false;
        }
        portEXIT_CRITICAL(&s_lock);
        if (stopped_meanwhile) {
            (void)sd_card_writer_close_stream(stream);
            return ESP_OK;
        }
    }

    uint64_t oldest_uptime = 0;
    portENTER_CRITICAL(&s_lock);
    bool has_oldest = error_log_hold_peek_uptime(&s_hold, &oldest_uptime);
    if (has_oldest) {
        s_header_uptime_hint_ms = oldest_uptime;
        s_header_uptime_hint_valid = true;
    }
    uint32_t dropped = error_log_hold_take_dropped(&s_hold);
    portEXIT_CRITICAL(&s_lock);
    if (dropped > 0) {
        char notice[ERROR_LOG_BODY_MAX];
        (void)snprintf(notice,
                       sizeof(notice),
                       "error_log_store: %u lines were lost while waiting to be written (hold buffer full)",
                       (unsigned)dropped);
        /* About the start of the hold, so it goes where the lost lines would have been. */
        (void)error_log_store_write_entry(stream,
                                          has_oldest ? oldest_uptime : error_log_uptime_ms(),
                                          false,
                                          notice,
                                          strlen(notice),
                                          pdMS_TO_TICKS(ERROR_LOG_RELEASE_WAIT_MS));
    }

    for (;;) {
        char text[ERROR_LOG_LINE_MAX];
        uint64_t entry_uptime = 0;
        bool raw = false;
        size_t length = 0;

        portENTER_CRITICAL(&s_lock);
        if (!s_card_ready) {
            /* Stopped while releasing: the rest stays held for the next start. */
            s_releasing = false;
            portEXIT_CRITICAL(&s_lock);
            return ESP_OK;
        }
        if (!error_log_hold_pop(&s_hold, &entry_uptime, &raw, text, sizeof(text), &length)) {
            /* Empty, and checked under the lock that opens the sink: no line can
               slip into the hold behind this. */
            s_open = true;
            s_releasing = false;
            portEXIT_CRITICAL(&s_lock);
            return ESP_OK;
        }
        portEXIT_CRITICAL(&s_lock);

        esp_err_t err = error_log_store_write_entry(stream,
                                                    entry_uptime,
                                                    raw,
                                                    text,
                                                    length,
                                                    pdMS_TO_TICKS(ERROR_LOG_RELEASE_WAIT_MS));
        if (err != ESP_OK) {
            error_log_warn_sink_failure_once(err);
        }
    }
}

/* Opens the sink if it can be opened now. */
static esp_err_t error_log_store_release_if_ready(void)
{
    uint64_t now_uptime = error_log_uptime_ms();
    bool clock_valid = error_log_clock_is_valid(time(NULL));

    portENTER_CRITICAL(&s_lock);
    bool ready = !s_open && !s_releasing && s_card_ready &&
                 error_log_store_gate_satisfied_locked(now_uptime, clock_valid);
    portEXIT_CRITICAL(&s_lock);
    return ready ? error_log_store_release() : ESP_OK;
}

static void error_log_store_emit(uint64_t entry_uptime_ms, bool raw, const char *text, size_t length)
{
    portENTER_CRITICAL(&s_lock);
    if (s_open) {
        sd_card_writer_stream_t *stream = s_stream;
        ++s_in_flight;
        portEXIT_CRITICAL(&s_lock);

        esp_err_t err = error_log_store_write_entry(stream, entry_uptime_ms, raw, text, length, 0);

        portENTER_CRITICAL(&s_lock);
        --s_in_flight;
        portEXIT_CRITICAL(&s_lock);
        if (err != ESP_OK) {
            error_log_warn_sink_failure_once(err);
        }
        return;
    }
    (void)error_log_hold_push(&s_hold, entry_uptime_ms, raw, text, length);
    portEXIT_CRITICAL(&s_lock);

    (void)error_log_store_release_if_ready();
}

/* ---- repeats ---- */

static void error_log_store_emit_repeat(const error_log_repeat_t *repeat, uint64_t now_uptime_ms)
{
    char body[ERROR_LOG_BODY_MAX];
    int length = snprintf(body,
                          sizeof(body),
                          "%s: previous line repeated %u more time%s",
                          repeat->label,
                          (unsigned)repeat->count,
                          repeat->count == 1U ? "" : "s");

    if (length > 0) {
        error_log_store_emit(now_uptime_ms, false, body, strlen(body));
    }
}

static void error_log_store_repeat_timer_cb(void *arg);

static void error_log_store_arm_repeat_timer(uint64_t now_uptime_ms)
{
    uint64_t due_ms = 0;
    bool create = false;
    bool pending;

    portENTER_CRITICAL(&s_lock);
    pending = error_log_dedupe_next_due_ms(&s_dedupe, now_uptime_ms, &due_ms);
    if (pending && s_repeat_timer == NULL && !s_repeat_timer_creating) {
        s_repeat_timer_creating = true;
        create = true;
    }
    portEXIT_CRITICAL(&s_lock);
    if (!pending) {
        return;
    }
    if (create) {
        esp_timer_handle_t handle = NULL;
        const esp_timer_create_args_t args = {
            .callback = error_log_store_repeat_timer_cb,
            .name = "error_log_repeat",
        };
        esp_err_t err = esp_timer_create(&args, &handle);
        portENTER_CRITICAL(&s_lock);
        s_repeat_timer = err == ESP_OK ? handle : NULL;
        s_repeat_timer_creating = false;
        portEXIT_CRITICAL(&s_lock);
    }
    if (s_repeat_timer != NULL && !esp_timer_is_active(s_repeat_timer)) {
        /* A little past the window, so that the burst has ended when it fires. */
        (void)esp_timer_start_once(s_repeat_timer, (due_ms + 50U) * 1000U);
    }
}

/* Writes a line for each burst that has ended; with `force` for every one, as the log is closed. */
static void error_log_store_flush_repeats(bool force)
{
    error_log_repeat_t repeats[4];
    uint64_t now_uptime = error_log_uptime_ms();
    size_t count;

    do {
        portENTER_CRITICAL(&s_lock);
        count = error_log_dedupe_collect(&s_dedupe, now_uptime, force, repeats, 4);
        portEXIT_CRITICAL(&s_lock);
        for (size_t i = 0; i < count; ++i) {
            error_log_store_emit_repeat(&repeats[i], now_uptime);
        }
    } while (count == 4U);

    error_log_store_arm_repeat_timer(now_uptime);
}

static void error_log_store_repeat_timer_cb(void *arg)
{
    (void)arg;
    error_log_store_flush_repeats(false);
}

/* ---- recording ---- */

static void error_log_store_record(const char *tag,
                                   const char *func,
                                   int line,
                                   bool has_code,
                                   esp_err_t err,
                                   const char *message,
                                   bool message_cut)
{
    char body[ERROR_LOG_BODY_MAX];
    char label[ERROR_LOG_DEDUPE_LABEL_MAX];
    error_log_repeat_t repeats[2];
    size_t repeat_count = 0;
    uint64_t now_uptime = error_log_uptime_ms();
    const error_log_fields_t fields = {
        .tag = tag,
        .func = func,
        .line = line,
        .has_code = has_code,
        .code = (int32_t)err,
        .code_name = has_code ? esp_err_to_name(err) : NULL,
        .message = message,
        .message_cut = message_cut,
    };
    size_t body_length = error_log_format_body(body, sizeof(body), &fields);

    if (func != NULL && func[0] != '\0') {
        (void)snprintf(label, sizeof(label), "%s %s:%d", tag != NULL ? tag : "?", func, line);
    } else {
        (void)snprintf(label, sizeof(label), "%s", tag != NULL ? tag : "?");
    }

    uint32_t key = error_log_dedupe_key(tag, func, line, has_code ? (int32_t)err : 0, message);
    portENTER_CRITICAL(&s_lock);
    bool write_it = error_log_dedupe_check(&s_dedupe, key, label, now_uptime, repeats, &repeat_count);
    portEXIT_CRITICAL(&s_lock);

    for (size_t i = 0; i < repeat_count; ++i) {
        error_log_store_emit_repeat(&repeats[i], now_uptime);
    }
    if (!write_it) {
        error_log_store_arm_repeat_timer(now_uptime);
        return;
    }
    error_log_store_emit(now_uptime, false, body, body_length);
}

void error_log_store_report(const char *tag, const char *func, int line, esp_err_t err, const char *fmt, ...)
{
    char message[ERROR_LOG_MESSAGE_MAX];
    va_list args;

    int written = 0;

    message[0] = '\0';
    if (fmt != NULL) {
        va_start(args, fmt);
        written = vsnprintf(message, sizeof(message), fmt, args);
        va_end(args);
    }
    if (written < 0) {
        message[0] = '\0';
        written = 0;
    }

    /* The serial log gets it too, so a caller does not need a line of its own. */
    const char *log_tag = tag != NULL ? tag : "error_log";
    const char *log_func = func != NULL ? func : "?";
    if (err != ESP_OK) {
        ESP_LOGE(log_tag, "%s:%d: %s: %s", log_func, line, message, esp_err_to_name(err));
    } else {
        ESP_LOGE(log_tag, "%s:%d: %s", log_func, line, message);
    }

    error_log_store_record(tag, func, line, err != ESP_OK, err, message, (size_t)written >= sizeof(message));
}

esp_err_t error_log_store_append_message(const char *tag, const char *message)
{
    error_log_store_record(tag, NULL, 0, false, ESP_OK, message != NULL ? message : "(null)", false);
    return ESP_OK;
}

esp_err_t error_log_store_append_esp_err(const char *tag, const char *message, esp_err_t err)
{
    error_log_store_record(tag, NULL, 0, true, err, message != NULL ? message : "(null)", false);
    return ESP_OK;
}

esp_err_t error_log_store_write_error_log(const char *line)
{
    ESP_RETURN_ON_FALSE(line != NULL, ESP_ERR_INVALID_ARG, ERROR_LOG_TAG, "line is null");
    error_log_store_emit(error_log_uptime_ms(), true, line, strlen(line));
    return ESP_OK;
}

/* ---- the time question, and the card ---- */

void error_log_store_notify_time_decided(error_log_time_outcome_t outcome)
{
    portENTER_CRITICAL(&s_lock);
    if (!s_decided) {
        s_decided = true;
        s_ntp_text = outcome == ERROR_LOG_TIME_SYNCED ? "ok" : (outcome == ERROR_LOG_TIME_FAILED ? "failed" : "none");
    }
    portEXIT_CRITICAL(&s_lock);
    (void)error_log_store_release_if_ready();
}

static void error_log_store_gate_timer_cb(void *arg)
{
    (void)arg;
    (void)error_log_store_release_if_ready();
}

/* When the card is ready before the clock question is answered, wake up at the end of the wait even if nothing else is logged. */
static void error_log_store_arm_gate_timer(void)
{
    uint64_t now_uptime = error_log_uptime_ms();

    if (s_gate_timer == NULL) {
        esp_timer_handle_t handle = NULL;
        const esp_timer_create_args_t args = {
            .callback = error_log_store_gate_timer_cb,
            .name = "error_log_gate",
        };
        if (esp_timer_create(&args, &handle) != ESP_OK) {
            return; /* the wait is still checked whenever a line is logged */
        }
        s_gate_timer = handle;
    }
    if (!esp_timer_is_active(s_gate_timer) && now_uptime < ERROR_LOG_TIME_WAIT_MS) {
        (void)esp_timer_start_once(s_gate_timer, (ERROR_LOG_TIME_WAIT_MS - now_uptime + 50U) * 1000U);
    }
}

esp_err_t error_log_store_start(void)
{
    portENTER_CRITICAL(&s_lock);
    s_card_ready = true;
    bool waiting_for_clock = !s_decided;
    portEXIT_CRITICAL(&s_lock);

    if (waiting_for_clock) {
        error_log_store_arm_gate_timer();
    }
    return error_log_store_release_if_ready();
}

esp_err_t error_log_store_stop(void)
{
    sd_card_writer_stream_t *stream = NULL;

    /* Folded counts are written while there is still somewhere to write them. */
    error_log_store_flush_repeats(true);

    portENTER_CRITICAL(&s_lock);
    stream = s_stream;
    s_stream = NULL;
    s_open = false;
    s_card_ready = false;
    s_header_uptime_hint_valid = false;
    portEXIT_CRITICAL(&s_lock);

    if (s_gate_timer != NULL) {
        (void)esp_timer_stop(s_gate_timer);
    }
    for (;;) {
        portENTER_CRITICAL(&s_lock);
        bool busy = s_in_flight > 0U || s_releasing;
        portEXIT_CRITICAL(&s_lock);
        if (!busy) {
            break;
        }
        vTaskDelay(1);
    }
    if (stream == NULL) {
        return ESP_OK;
    }
    /* ESP_FAIL: the stream had already failed, or failed while closing. It is
       closed and freed either way. */
    return sd_card_writer_close_stream(stream);
}

bool error_log_store_is_failed(void)
{
    sd_card_writer_stats_t stats = { 0 };

    portENTER_CRITICAL(&s_lock);
    sd_card_writer_stream_t *stream = s_stream;
    portEXIT_CRITICAL(&s_lock);
    if (stream == NULL || sd_card_writer_get_stats(stream, &stats) != ESP_OK) {
        return false;
    }
    return stats.failed;
}

/* Puts every piece of state back as at boot. For the host test, which cannot restart the program. */
__attribute__((unused)) static void error_log_store_reset_state(void)
{
    s_stream = NULL;
    s_open = false;
    s_releasing = false;
    s_card_ready = false;
    s_in_flight = 0;
    s_decided = false;
    s_ntp_text = "pending";
    error_log_hold_init(&s_hold, s_hold_storage, sizeof(s_hold_storage));
    error_log_dedupe_init(&s_dedupe, (uint32_t)CONFIG_ERROR_LOG_STORE_REPEAT_WINDOW_SECONDS * 1000U);
    s_file_date[0] = '\0';
    s_rotate_date[0] = '\0';
    s_header_uptime_hint_valid = false;
    s_header_uptime_hint_ms = 0;
    s_part = 0;
    s_gate_timer = NULL;
    s_repeat_timer = NULL;
    s_repeat_timer_creating = false;
    s_sink_warning_emitted = false;
}
