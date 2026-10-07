/*
 * The error log, end to end.
 *
 * The real error_log_store.c is compiled here against a fake card writer that
 * really writes files into a temporary directory, a fake clock that can be set
 * or left unset, and timers that fire when the test moves the clock past them.
 * So what is checked is what ends up in the files: which file, in what order,
 * with what stamp and header.
 *
 * What these checks guard against is the kind of fault that only shows after
 * weeks on a wall: lines written into a file whose name guessed the date wrong,
 * a boot's lines scattered with no way to tell they belong together, a line
 * lost because the card was not ready, a storm of one error filling the card.
 */

#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static time_t fake_time(time_t *out);
#define time fake_time

#include "../../components/services/error_log_store/error_log_store.c"

#undef time

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

/* ---- fake clock and timers ---- */

static int64_t g_uptime_us;
/* wall clock = offset + uptime in seconds. 0 means the clock was never set: it reads 1970. */
static time_t g_clock_offset;

static time_t fake_time(time_t *out)
{
    time_t now = g_clock_offset + (time_t)(g_uptime_us / 1000000);

    if (out != NULL) {
        *out = now;
    }
    return now;
}

int64_t esp_timer_get_time(void)
{
    return g_uptime_us;
}

struct esp_timer {
    esp_timer_cb_t callback;
    void *arg;
    bool active;
    int64_t due_us;
};

static struct esp_timer g_timers[4];
static int g_timer_count;

esp_err_t esp_timer_create(const esp_timer_create_args_t *args, esp_timer_handle_t *out_handle)
{
    if (g_timer_count >= 4) {
        return ESP_ERR_NO_MEM;
    }
    g_timers[g_timer_count] = (struct esp_timer){ .callback = args->callback, .arg = args->arg };
    *out_handle = &g_timers[g_timer_count++];
    return ESP_OK;
}

esp_err_t esp_timer_start_once(esp_timer_handle_t timer, uint64_t timeout_us)
{
    timer->active = true;
    timer->due_us = g_uptime_us + (int64_t)timeout_us;
    return ESP_OK;
}

esp_err_t esp_timer_stop(esp_timer_handle_t timer)
{
    timer->active = false;
    return ESP_OK;
}

bool esp_timer_is_active(esp_timer_handle_t timer)
{
    return timer->active;
}

static void advance_ms(uint64_t milliseconds)
{
    int64_t target = g_uptime_us + (int64_t)milliseconds * 1000;

    /* Fire timers in the order they fall due, with the clock at their moment. */
    for (;;) {
        struct esp_timer *next = NULL;
        for (int i = 0; i < g_timer_count; ++i) {
            if (g_timers[i].active && g_timers[i].due_us <= target && (next == NULL || g_timers[i].due_us < next->due_us)) {
                next = &g_timers[i];
            }
        }
        if (next == NULL) {
            break;
        }
        if (next->due_us > g_uptime_us) {
            g_uptime_us = next->due_us;
        }
        next->active = false;
        next->callback(next->arg);
    }
    g_uptime_us = target;
}

/* Sets the wall clock to `real` as of right now. */
static void set_clock(time_t real)
{
    g_clock_offset = real - (time_t)(g_uptime_us / 1000000);
}

static time_t utc(int year, int month, int day, int hour, int minute, int second)
{
    struct tm t = { 0 };

    t.tm_year = year - 1900;
    t.tm_mon = month - 1;
    t.tm_mday = day;
    t.tm_hour = hour;
    t.tm_min = minute;
    t.tm_sec = second;
    return timegm(&t);
}

/* ---- the card and the rest of the platform ---- */

static char g_dir[160];

bool sd_card_storage_is_mounted(void)
{
    return true;
}

const char *sd_card_storage_get_mount_point(void)
{
    return g_dir;
}

const char *boot_id_get(void)
{
    return "11111111-2222-4333-8444-555555555555";
}

esp_reset_reason_t esp_reset_reason(void)
{
    return ESP_RST_SW;
}

const esp_app_desc_t *esp_app_get_description(void)
{
    static const esp_app_desc_t desc = { .version = "9.9.9" };

    return &desc;
}

const char *esp_err_to_name(esp_err_t err)
{
    switch (err) {
    case ESP_OK:
        return "ESP_OK";
    case ESP_FAIL:
        return "ESP_FAIL";
    case ESP_ERR_TIMEOUT:
        return "ESP_ERR_TIMEOUT";
    case ESP_ERR_INVALID_STATE:
        return "ESP_ERR_INVALID_STATE";
    default:
        return "ESP_ERR_OTHER";
    }
}

void vTaskDelay(TickType_t ticks)
{
    (void)ticks;
}

/* ---- a card writer that really writes ---- */

struct sd_card_writer_stream {
    sd_card_writer_stream_config_t config;
    FILE *file;
    bool failed;
    bool need_new_file;
    uint64_t file_size;
    uint64_t bytes_since_open;
    int files_opened;
    int rotations;
};

static struct sd_card_writer_stream g_stream;
static struct {
    esp_err_t open_result;
    esp_err_t write_result;
    int open_calls;
    int close_calls;
    int write_calls;
    sd_card_writer_stream_t *last_stream;
} g_writer;

static uint64_t file_size_of(const char *path)
{
    struct stat st;

    return stat(path, &st) == 0 ? (uint64_t)st.st_size : 0U;
}

static bool writer_open_file(struct sd_card_writer_stream *stream)
{
    sd_card_writer_target_t target;
    char path[320];

    memset(&target, 0, sizeof(target));
    if (stream->config.target_fn(&target, stream->config.target_ctx) != ESP_OK) {
        stream->failed = true;
        return false;
    }
    snprintf(path, sizeof(path), "%s/%s", g_dir, target.path);
    /* "x": a file that is not to be appended to must not exist, as in the real writer (O_EXCL). */
    stream->file = fopen(path, target.append ? "ab" : "wbx");
    if (stream->file == NULL) {
        stream->failed = true;
        return false;
    }
    if (target.header_len > 0) {
        fwrite(target.header, 1, target.header_len, stream->file);
        fflush(stream->file);
    }
    stream->file_size = file_size_of(path);
    stream->bytes_since_open = 0;
    stream->need_new_file = false;
    ++stream->files_opened;
    return true;
}

static void writer_close_file(struct sd_card_writer_stream *stream)
{
    if (stream->file != NULL) {
        fclose(stream->file);
        stream->file = NULL;
    }
}

esp_err_t sd_card_writer_open_stream(const sd_card_writer_stream_config_t *config, sd_card_writer_stream_t **out_stream)
{
    ++g_writer.open_calls;
    if (g_writer.open_result != ESP_OK) {
        return g_writer.open_result;
    }
    memset(&g_stream, 0, sizeof(g_stream));
    g_stream.config = *config;
    g_stream.need_new_file = true;
    *out_stream = &g_stream;
    g_writer.last_stream = &g_stream;
    return ESP_OK;
}

esp_err_t sd_card_writer_write(sd_card_writer_stream_t *stream, const void *data, size_t size, TickType_t wait_ticks)
{
    (void)wait_ticks;
    ++g_writer.write_calls;
    if (g_writer.write_result != ESP_OK) {
        return g_writer.write_result;
    }
    if (stream->failed) {
        return ESP_ERR_INVALID_STATE;
    }
    if (stream->file != NULL && stream->bytes_since_open > 0 &&
        stream->file_size + size > stream->config.max_file_size) {
        writer_close_file(stream); /* the size limit: the next record starts a new file */
        stream->need_new_file = true;
    }
    if (stream->file == NULL || stream->need_new_file) {
        writer_close_file(stream);
        if (!writer_open_file(stream)) {
            return ESP_ERR_INVALID_STATE;
        }
    }
    fwrite(data, 1, size, stream->file);
    fflush(stream->file);
    stream->file_size += size;
    stream->bytes_since_open += size;
    return ESP_OK;
}

esp_err_t sd_card_writer_rotate(sd_card_writer_stream_t *stream)
{
    ++stream->rotations;
    writer_close_file(stream);
    stream->need_new_file = true;
    return ESP_OK;
}

esp_err_t sd_card_writer_close_stream(sd_card_writer_stream_t *stream)
{
    ++g_writer.close_calls;
    writer_close_file(stream);
    return stream->failed ? ESP_FAIL : ESP_OK;
}

esp_err_t sd_card_writer_get_stats(sd_card_writer_stream_t *stream, sd_card_writer_stats_t *stats)
{
    memset(stats, 0, sizeof(*stats));
    stats->failed = stream->failed;
    return ESP_OK;
}

void sd_card_writer_set_report_fn(sd_card_writer_report_fn_t fn, void *ctx)
{
    (void)fn;
    (void)ctx;
}

/* ---- helpers ---- */

static void remove_all_files(void)
{
    DIR *dir = opendir(g_dir);
    struct dirent *entry;
    char path[320];

    if (dir == NULL) {
        return;
    }
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            snprintf(path, sizeof(path), "%s/%s", g_dir, entry->d_name);
            unlink(path);
        }
    }
    closedir(dir);
}

static int count_files(void)
{
    DIR *dir = opendir(g_dir);
    struct dirent *entry;
    int count = 0;

    if (dir == NULL) {
        return -1;
    }
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            ++count;
        }
    }
    closedir(dir);
    return count;
}

static char g_contents[262144];

/* The whole of `name` in the log directory, or "" when there is no such file. */
static const char *read_file(const char *name)
{
    char path[320];
    FILE *file;
    size_t got;

    snprintf(path, sizeof(path), "%s/%s", g_dir, name);
    file = fopen(path, "rb");
    g_contents[0] = '\0';
    if (file == NULL) {
        return g_contents;
    }
    got = fread(g_contents, 1, sizeof(g_contents) - 1U, file);
    g_contents[got] = '\0';
    fclose(file);
    return g_contents;
}

static int count_substring(const char *haystack, const char *needle)
{
    int count = 0;

    for (const char *p = strstr(haystack, needle); p != NULL; p = strstr(p + 1, needle)) {
        ++count;
    }
    return count;
}

static int count_lines(const char *text)
{
    return count_substring(text, "\n");
}

/*
 * Reading a file from the top, the +NNNNms never steps back. (Within one boot:
 * a second boot appended to the same file starts its uptime again.) The boot
 * line is stamped with the oldest line it introduces, which is what makes this
 * hold for a file written from a hold.
 */
static bool uptime_never_steps_back(const char *text)
{
    unsigned long long previous = 0;

    for (const char *line = text; *line != '\0';) {
        const char *end = strchr(line, '\n');
        const char *plus = strstr(line, " +");
        if (plus != NULL && (end == NULL || plus < end)) {
            unsigned long long value = strtoull(plus + 2, NULL, 10);
            if (value < previous) {
                return false;
            }
            previous = value;
        }
        if (end == NULL) {
            break;
        }
        line = end + 1;
    }
    return true;
}

static void setup(void)
{
    remove_all_files();
    memset(&g_writer, 0, sizeof(g_writer));
    memset(&g_stream, 0, sizeof(g_stream));
    memset(g_timers, 0, sizeof(g_timers));
    g_timer_count = 0;
    g_uptime_us = 5LL * 1000000LL;
    g_clock_offset = 0;
    error_log_store_reset_state();
}

static const char *TAG = "unit";

/* ---- scenarios ---- */

static void test_nothing_is_written_until_card_and_clock(void)
{
    setup();
    error_log_store_append_message("wifi", "first problem");
    advance_ms(3000);
    error_log_store_append_message("wifi", "second problem");

    check(count_files() == 0, "lines before the card and the clock are held: no file yet");

    check(error_log_store_start() == ESP_OK, "the card becomes ready");
    check(count_files() == 0, "a ready card is not enough while the clock question is open: still no file");
    check(g_writer.open_calls == 0, "the stream is not even opened yet");

    advance_ms(12000);
    set_clock(utc(2026, 10, 7, 12, 0, 0));
    error_log_store_notify_time_decided(ERROR_LOG_TIME_SYNCED);

    check(count_files() == 1, "once the clock is answered, one file is created");
    const char *text = read_file("ERR_2026-10-07_01.LOG");
    check(text[0] != '\0', "it is named for today's date, and the number is 01");
    check(strstr(text, "boot: id=11111111-2222-4333-8444-555555555555 part=1 fw=9.9.9 reset=SW dev=0 clock=set ntp=ok") != NULL,
          "it starts with the boot line: id, part, firmware, reset reason, mode, clock, ntp");
    check(strstr(text, "[2026-10-07 11:59:45 +5000ms] boot: id=") == text,
          "the boot line carries the time of the oldest line it introduces, not the moment the file was opened");
    check(uptime_never_steps_back(text), "so read from the top the log never steps back in time");
    check(text == strstr(text, "[") && strstr(text, "] boot:") < strchr(text, '\n'), "the boot line is the first line");

    /* Held lines get the time they really had: now is 20 s into the boot, 12:00:00. */
    check(strstr(text, "[2026-10-07 11:59:45 +5000ms] wifi: first problem\n") != NULL,
          "the first held line is stamped 15 s before the clock was set, not 1970");
    check(strstr(text, "[2026-10-07 11:59:48 +8000ms] wifi: second problem\n") != NULL,
          "the second one 12 s before, in order");
    check(strstr(text, "first problem") < strstr(text, "second problem"), "they are in the order they happened");
    check(count_lines(text) == 3, "the boot line and the two lines, nothing else");
}

static void test_clock_answered_before_the_card(void)
{
    setup();
    error_log_store_append_message("t", "early");
    set_clock(utc(2026, 10, 7, 9, 0, 0));
    error_log_store_notify_time_decided(ERROR_LOG_TIME_SYNCED);
    check(count_files() == 0, "an answered clock alone writes nothing: the card is not ready");

    advance_ms(2000);
    check(error_log_store_start() == ESP_OK, "then the card is ready");
    const char *text = read_file("ERR_2026-10-07_01.LOG");
    check(strstr(text, "early") != NULL, "the held line is written");
    check(strstr(text, "[2026-10-07 09:00:00 +5000ms] t: early") != NULL || strstr(text, "[2026-10-07 08:59:58 +5000ms] t: early") != NULL ||
              strstr(text, "+5000ms] t: early") != NULL,
          "with its own uptime");
}

static void test_waiting_ends_by_timeout(void)
{
    setup();
    error_log_store_append_message("wifi", "boot-time failure");
    error_log_store_start();
    advance_ms(100000);
    check(count_files() == 0, "100 s in, with no answer: still waiting");

    advance_ms(81000); /* past 180 s since boot: the gate timer fires */
    check(count_files() == 1, "after the wait is over, with nothing logged since, the gate timer writes the held lines");
    const char *text = read_file("ERR_1970-01-01_01.LOG");
    check(text[0] != '\0', "the clock was never set, so the file is named 1970-01-01");
    check(strstr(text, "clock=unset ntp=timeout") != NULL, "and its boot line says so");
    check(strstr(text, "[1970-01-01 00:00:05 +5000ms] wifi: boot-time failure") != NULL,
          "the held line reads as 1970, 5 s after boot");
    check(strstr(text, "[1970-01-01 00:00:05 +5000ms] boot: id=") == text, "and the boot line before it reads the same");
}

static void test_timeout_is_also_checked_when_a_line_arrives(void)
{
    setup();
    error_log_store_start(); /* 5 s into the boot: the wait ends at 180 s, not 180 s from now */
    advance_ms(170000);
    error_log_store_append_message("a", "before the end");
    check(count_files() == 0, "175 s into the boot: still held");
    g_timers[0].active = false; /* as if the timer had not fired yet */
    advance_ms(10000);
    error_log_store_append_message("a", "after the end");
    check(count_files() == 1, "a line logged after the wait, even without the timer, opens the sink");
    const char *text = read_file("ERR_1970-01-01_01.LOG");
    check(strstr(text, "before the end") != NULL && strstr(text, "after the end") != NULL, "both lines are in it");
}

static void test_clock_already_valid_writes_at_once(void)
{
    setup();
    set_clock(utc(2026, 10, 7, 8, 0, 0)); /* a soft reset: the RTC kept the time */
    error_log_store_append_message("x", "crash loop line");
    check(count_files() == 0, "the card is not ready yet");
    error_log_store_start();
    const char *text = read_file("ERR_2026-10-07_01.LOG");
    check(strstr(text, "crash loop line") != NULL, "no waiting for NTP: written as soon as the card is ready");
    check(strstr(text, "clock=set ntp=preset") != NULL, "the boot line says the clock was already right");
}

static void test_no_lines_no_file(void)
{
    setup();
    error_log_store_start();
    error_log_store_notify_time_decided(ERROR_LOG_TIME_FAILED);
    check(count_files() == 0, "a boot with no errors leaves no file behind");
    check(g_writer.last_stream != NULL && g_writer.last_stream->files_opened == 0, "the writer never opened one");
}

static void test_appends_to_todays_file(void)
{
    setup();
    set_clock(utc(2026, 10, 7, 8, 0, 0));
    FILE *old = fopen("/dev/null", "r");
    if (old != NULL) {
        fclose(old);
    }
    char path[320];
    snprintf(path, sizeof(path), "%s/ERR_2026-10-07_01.LOG", g_dir);
    FILE *file = fopen(path, "wb");
    fputs("[2026-10-07 07:00:00 +100ms] earlier: from the previous boot\n", file);
    fputs("[2026-10-07 07:00:09 +9000ms] earlier: cut off mid-li", file); /* the power went here */
    fclose(file);

    error_log_store_append_message("now", "this boot");
    error_log_store_start();

    check(count_files() == 1, "today's file is reused, not a second one");
    const char *text = read_file("ERR_2026-10-07_01.LOG");
    check(strstr(text, "from the previous boot\n") != NULL, "what was there is kept");
    check(strstr(text, "cut off mid-li\n[") != NULL, "a line cut off by a power loss is ended before the new section begins");
    check(strstr(text, "cut off mid-li\n[2026-10-07 08:00:00") != NULL || strstr(text, "mid-li\n[") != NULL,
          "the new section starts on its own line");
    check(count_substring(text, "] boot: id=") == 1, "one boot line, for this run's section");
    check(strstr(text, "this boot\n") != NULL, "and this boot's line follows it");
}

static void test_full_file_starts_the_next_number(void)
{
    setup();
    set_clock(utc(2026, 10, 7, 8, 0, 0));
    char path[320];
    snprintf(path, sizeof(path), "%s/ERR_2026-10-07_03.LOG", g_dir);
    FILE *file = fopen(path, "wb");
    for (int i = 0; i < 2700; ++i) {
        fputs("[2026-10-07 07:00:00 +1ms] filler: xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\n", file);
    }
    fclose(file);
    snprintf(path, sizeof(path), "%s/ERR_2026-10-06_09.LOG", g_dir);
    file = fopen(path, "wb");
    fputs("yesterday\n", file);
    fclose(file);
    check(file_size_of(path) > 0 && count_files() == 2, "set up: a full file for today and one for yesterday");

    error_log_store_append_message("now", "after a full file");
    error_log_store_start();
    check(count_files() == 3, "a full file is followed by a new one");
    check(read_file("ERR_2026-10-07_04.LOG")[0] != '\0', "numbered after the highest number of today, not after yesterday's");
    check(strstr(read_file("ERR_2026-10-07_04.LOG"), "after a full file") != NULL, "and the line is in it");
}

static void test_a_file_just_under_the_limit_is_not_reused(void)
{
    setup();
    set_clock(utc(2026, 10, 7, 8, 0, 0));
    /* What the writer leaves behind when the next line did not fit: a few
       hundred bytes under the limit. It has "room" only on paper. */
    char path[320];
    snprintf(path, sizeof(path), "%s/ERR_2026-10-07_01.LOG", g_dir);
    FILE *file = fopen(path, "wb");
    char chunk[1024];
    memset(chunk, 'x', sizeof(chunk));
    chunk[sizeof(chunk) - 1U] = '\n';
    for (int i = 0; i < 255; ++i) {
        fwrite(chunk, 1, sizeof(chunk), file);
    }
    fwrite(chunk, 1, 900, file); /* 256 KB minus 124 bytes, ending on a newline below */
    fputc('\n', file);
    fclose(file);
    check(file_size_of(path) > 256U * 1024U - 256U && file_size_of(path) < 256U * 1024U, "set up: a file a little under the limit");

    error_log_store_append_message("now", "goes to a fresh file");
    error_log_store_start();
    check(count_files() == 2, "a file that is nearly at the limit is not appended to");
    check(strstr(read_file("ERR_2026-10-07_02.LOG"), "goes to a fresh file") != NULL, "the next number is used");
    check(count_substring(read_file("ERR_2026-10-07_01.LOG"), "boot:") == 0, "and the nearly full file did not get a boot line");
}

static void test_size_limit_inside_a_day(void)
{
    setup();
    set_clock(utc(2026, 10, 7, 8, 0, 0));
    error_log_store_start();
    for (int i = 0; i < 3100; ++i) {
        advance_ms(20000); /* a different time each line, so nothing is folded */
        error_log_store_append_message("bulk", "a line of ordinary length to fill the file in good time ok");
    }
    check(count_files() >= 2, "a file that reaches the limit is followed by another");
    const char *second = read_file("ERR_2026-10-07_02.LOG");
    check(strstr(second, "] boot: id=11111111-2222-4333-8444-555555555555 part=2 ") != NULL,
          "the next file starts with a boot line of the same id, part 2");
}

static void test_date_change_starts_a_new_file_of_the_same_boot(void)
{
    setup();
    set_clock(utc(2026, 10, 7, 23, 59, 50));
    error_log_store_start();
    error_log_store_append_message("night", "before midnight");
    check(read_file("ERR_2026-10-07_01.LOG")[0] != '\0', "written to the 7th");

    advance_ms(15000); /* 00:00:05 on the 8th */
    error_log_store_append_message("night", "after midnight");
    check(g_stream.rotations == 1, "the first line of a new day asks for a new file");
    const char *eighth = read_file("ERR_2026-10-08_01.LOG");
    check(strstr(eighth, "after midnight") != NULL, "and lands in the 8th's file");
    check(strstr(eighth, "part=2") != NULL && strstr(eighth, "id=11111111-2222-4333-8444-555555555555") != NULL,
          "which begins with a boot line: the same id, the next part");
    check(strstr(read_file("ERR_2026-10-07_01.LOG"), "after midnight") == NULL, "the 7th's file does not get it");

    advance_ms(1000);
    error_log_store_append_message("night", "later that day");
    check(g_stream.rotations == 1, "it does not rotate again for every line of the same day");
}

static void test_clock_set_late_moves_to_a_dated_file(void)
{
    setup();
    error_log_store_start();
    advance_ms(181000); /* the wait ran out with the clock unset */
    error_log_store_append_message("net", "no ntp yet");
    check(read_file("ERR_1970-01-01_01.LOG")[0] != '\0', "the file is named 1970-01-01");

    advance_ms(30000);
    set_clock(utc(2026, 10, 7, 12, 30, 0)); /* NTP finally works */
    error_log_store_append_message("net", "ntp is back");
    const char *dated = read_file("ERR_2026-10-07_01.LOG");
    check(strstr(dated, "ntp is back") != NULL, "after the clock is set the next line goes to a file with the real date");
    check(strstr(dated, "part=2") != NULL && strstr(dated, "clock=set") != NULL, "its boot line says part 2 and clock=set");
    check(strstr(dated, "id=11111111-2222-4333-8444-555555555555") != NULL &&
              strstr(read_file("ERR_1970-01-01_01.LOG"), "id=11111111-2222-4333-8444-555555555555") != NULL,
          "both files carry the same boot id, so they are known to be one run");
    check(strstr(read_file("ERR_1970-01-01_01.LOG"), "clock=unset") != NULL, "and the first says the clock was unset");
}

static void test_lines_while_the_card_is_out_are_kept(void)
{
    setup();
    set_clock(utc(2026, 10, 7, 8, 0, 0));
    error_log_store_start();
    error_log_store_append_message("m", "before pulling");
    check(error_log_store_stop() == ESP_OK, "the card is going away");
    check(g_writer.close_calls == 1, "the stream is closed");

    advance_ms(4000);
    error_log_store_append_message("m", "while out one");
    advance_ms(3000);
    error_log_store_append_message("m", "while out two");
    const char *text = read_file("ERR_2026-10-07_01.LOG");
    check(strstr(text, "while out") == NULL, "nothing reaches the card while it is out");

    advance_ms(5000);
    check(error_log_store_start() == ESP_OK, "the card is back");
    text = read_file("ERR_2026-10-07_01.LOG");
    check(strstr(text, "while out one") != NULL && strstr(text, "while out two") != NULL, "the lines made meanwhile are written");
    check(strstr(text, "before pulling") < strstr(text, "while out one") && strstr(text, "while out one") < strstr(text, "while out two"),
          "in order");
    check(count_files() == 1, "into today's file, which was appended to");
    check(count_substring(text, "] boot: id=") == 2, "with a new section start for the new run of the stream");
    check(strstr(text, "part=2") != NULL, "part 2");
    check(strstr(text, "[2026-10-07 08:00:04 +9000ms] m: while out one") != NULL, "the lines keep the time they had, not the time they were written");
    check(strstr(text, "[2026-10-07 08:00:04 +9000ms] boot: id=") != NULL, "and the new section's boot line takes the oldest of them");
    check(uptime_never_steps_back(text), "the whole file reads forward in time, across the old section and the new one");
}

static void test_hold_overflow(void)
{
    setup();
    for (int i = 0; i < 100; ++i) {
        char message[64];
        snprintf(message, sizeof(message), "line number %03d with some padding text to take up room", i);
        advance_ms(1);
        error_log_store_append_message("burst", message);
    }
    set_clock(utc(2026, 10, 7, 8, 0, 0));
    error_log_store_start();
    const char *text = read_file("ERR_2026-10-07_01.LOG");
    check(strstr(text, "line number 000") != NULL, "the oldest line is kept");
    check(strstr(text, "line number 099") == NULL, "the newest ones were refused");
    check(strstr(text, "lines were lost while waiting to be written (hold buffer full)") != NULL, "and the loss is written down");
    check(strstr(text, "] boot:") < strstr(text, "lines were lost") && strstr(text, "lines were lost") < strstr(text, "line number 000"),
          "after the boot line, before the lines that were kept");
    check(uptime_never_steps_back(text), "and the notice is stamped where the lost lines would have been, so time still reads forward");
}

static void test_repeats_are_folded(void)
{
    setup();
    set_clock(utc(2026, 10, 7, 8, 0, 0));
    error_log_store_start();
    for (int i = 0; i < 5; ++i) {
        ERROR_LOG(ESP_ERR_TIMEOUT, "connect failed");
        advance_ms(1000);
    }
    ERROR_LOG(ESP_ERR_TIMEOUT, "a different message");
    const char *text = read_file("ERR_2026-10-07_01.LOG");
    check(count_substring(text, "connect failed") == 1, "five identical errors in a few seconds are written once");
    check(strstr(text, "a different message") != NULL, "a different one is not folded");

    advance_ms(12000);
    text = read_file("ERR_2026-10-07_01.LOG");
    check(strstr(text, "previous line repeated 4 more times") != NULL,
          "at the end of the window a line says how many were left out, with nothing else logged meanwhile");
    check(strstr(text, "unit test_repeats_are_folded:") != NULL, "naming the event");

    ERROR_LOG(ESP_ERR_TIMEOUT, "connect failed");
    text = read_file("ERR_2026-10-07_01.LOG");
    check(count_substring(text, "connect failed") == 2, "after the burst the same error is written again");
}

static void test_stop_writes_folded_counts(void)
{
    setup();
    set_clock(utc(2026, 10, 7, 8, 0, 0));
    error_log_store_start();
    for (int i = 0; i < 3; ++i) {
        ERROR_LOG_MSG("storm"); /* one line of source, so one event, however often it runs */
    }
    error_log_store_stop();
    check(strstr(read_file("ERR_2026-10-07_01.LOG"), "previous line repeated 2 more times") != NULL,
          "closing the log writes the counts that were still waiting");
}

static void test_macro_line(void)
{
    setup();
    set_clock(utc(2026, 10, 7, 8, 0, 0));
    error_log_store_start();
    ERROR_LOG(ESP_ERR_TIMEOUT, "time sync failed (%d tries)", 6);
    ERROR_LOG(ESP_FAIL, "plain failure");
    ERROR_LOG_MSG("no code here: %s", "uid-42");
    const char *text = read_file("ERR_2026-10-07_01.LOG");
    check(strstr(text, "] unit test_macro_line:") != NULL, "the file's TAG is the app name, then the function and line");
    check(strstr(text, "ESP_ERR_TIMEOUT(0x107): time sync failed (6 tries)\n") != NULL, "the code with its number, then the message");
    check(strstr(text, "ESP_FAIL(-1): plain failure\n") != NULL, "ESP_FAIL is written as -1");
    check(strstr(text, ": no code here: uid-42\n") != NULL, "ERROR_LOG_MSG has no code field");
}

static void test_legacy_entry_points(void)
{
    setup();
    set_clock(utc(2026, 10, 7, 8, 0, 0));
    error_log_store_start();
    error_log_store_append_message("old", "message only");
    error_log_store_append_esp_err("old", "with a code", ESP_ERR_INVALID_STATE);
    error_log_store_write_error_log("[raw] finished line without newline");
    error_log_store_write_error_log("[raw] finished line with newline\n");
    const char *text = read_file("ERR_2026-10-07_01.LOG");
    check(strstr(text, "] old: message only\n") != NULL, "append_message: tag and message");
    check(strstr(text, "] old ESP_ERR_INVALID_STATE(0x103): with a code\n") != NULL, "append_esp_err: the same format as the new API");
    check(strstr(text, "[raw] finished line without newline\n") != NULL, "a raw line gets its newline");
    check(strstr(text, "newline\n\n") == NULL, "and never a second one");
}

static void test_long_line_is_cut_not_lost(void)
{
    setup();
    set_clock(utc(2026, 10, 7, 8, 0, 0));
    error_log_store_start();
    char big[1000];
    memset(big, 'z', sizeof(big) - 1U);
    big[sizeof(big) - 1U] = '\0';
    ERROR_LOG(ESP_FAIL, "%s", big);
    const char *text = read_file("ERR_2026-10-07_01.LOG");
    const char *line = strstr(text, "ESP_FAIL(-1): zzz");
    check(line != NULL, "an over-long message is still recorded");
    const char *start = line;
    while (start > text && start[-1] != '\n') {
        --start;
    }
    const char *end = strchr(line, '\n');
    check(end != NULL && (size_t)(end - start) < ERROR_LOG_LINE_MAX, "as one line that fits the line limit");
    check(strncmp(end - 3, "...", 3) == 0, "marked as cut");
}

static void test_stream_failure_and_open_failure(void)
{
    setup();
    set_clock(utc(2026, 10, 7, 8, 0, 0));
    g_writer.open_result = ESP_ERR_NO_MEM;
    error_log_store_append_message("a", "survives a failed open");
    check(error_log_store_start() == ESP_ERR_NO_MEM, "a stream that cannot be opened is reported by start()");
    check(count_files() == 0, "nothing is written");

    g_writer.open_result = ESP_OK;
    check(error_log_store_start() == ESP_OK, "start() again");
    check(strstr(read_file("ERR_2026-10-07_01.LOG"), "survives a failed open") != NULL, "the line that was held is not lost");

    check(!error_log_store_is_failed(), "a healthy stream is not failed");
    g_stream.failed = true;
    check(error_log_store_is_failed(), "a stream the writer gave up on says so");
    error_log_store_stop();
    check(!error_log_store_is_failed(), "after stop there is no stream to be failed");
}

static void test_write_errors_do_not_reach_the_caller(void)
{
    setup();
    set_clock(utc(2026, 10, 7, 8, 0, 0));
    error_log_store_start();
    g_writer.write_result = ESP_ERR_TIMEOUT;
    ERROR_LOG(ESP_FAIL, "this cannot be written");
    ERROR_LOG(ESP_FAIL, "nor this");
    check(true, "logging with a writer that refuses everything returns normally");
}

static void test_numbers_used_up(void)
{
    setup();
    set_clock(utc(2026, 10, 7, 8, 0, 0));
    char path[320];
    snprintf(path, sizeof(path), "%s/ERR_2026-10-07_9999.LOG", g_dir);
    FILE *file = fopen(path, "wb");
    for (int i = 0; i < 2700; ++i) {
        fputs("[2026-10-07 07:00:00 +1ms] filler: xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\n", file);
    }
    fclose(file);
    error_log_store_start();
    error_log_store_append_message("x", "nowhere to go");
    check(count_files() == 1, "with every number for today used and the last one full, no file is created");
    check(error_log_store_is_failed(), "the stream is failed, which sd_card_status reads as a fault");
}

int main(void)
{
    const char *base = getenv("TMPDIR");
    snprintf(g_dir, sizeof(g_dir), "%s/errlogXXXXXX", base != NULL && base[0] != '\0' ? base : "/tmp");
    if (mkdtemp(g_dir) == NULL) {
        printf("cannot make a temporary directory\n");
        return 2;
    }
    setenv("TZ", "UTC0", 1);
    tzset();

    test_nothing_is_written_until_card_and_clock();
    test_clock_answered_before_the_card();
    test_waiting_ends_by_timeout();
    test_timeout_is_also_checked_when_a_line_arrives();
    test_clock_already_valid_writes_at_once();
    test_no_lines_no_file();
    test_appends_to_todays_file();
    test_full_file_starts_the_next_number();
    test_a_file_just_under_the_limit_is_not_reused();
    test_size_limit_inside_a_day();
    test_date_change_starts_a_new_file_of_the_same_boot();
    test_clock_set_late_moves_to_a_dated_file();
    test_lines_while_the_card_is_out_are_kept();
    test_hold_overflow();
    test_repeats_are_folded();
    test_stop_writes_folded_counts();
    test_macro_line();
    test_legacy_entry_points();
    test_long_line_is_cut_not_lost();
    test_stream_failure_and_open_failure();
    test_write_errors_do_not_reach_the_caller();
    test_numbers_used_up();

    remove_all_files();
    rmdir(g_dir);
    printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
