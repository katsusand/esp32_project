#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "error_log_store.h"
#include "sd_card_storage.h"
#include "sd_card_writer.h"

static const char *ERROR_LOG_TAG = "error_log_store";
static const size_t ERROR_LOG_LINE_MAX = 256;
/*
 * 8.3 on purpose. The project builds FATFS with CONFIG_FATFS_LFN_NONE (the
 * ESP-IDF default), where FatFs rejects a longer name with FR_INVALID_NAME; the
 * old "error_0000.log" had 10 characters before the dot, so no log file could
 * ever be created. Upper case matches what readdir() reports for a short name,
 * which the index scan relies on.
 */
#define ERROR_LOG_FILE_FORMAT "ERR_%04u.LOG"
#define ERROR_LOG_FILE_SCAN_FORMAT "ERR_%4u.LOG"
#define ERROR_LOG_MAX_INDEX 9999U
/* Roughly 4000 lines. A line is never split, so a file ends on a whole line. */
#define ERROR_LOG_MAX_FILE_SIZE (256U * 1024U)
/* Error lines are rare; this only has to ride out a slow card. */
#define ERROR_LOG_BUFFER_SIZE 4096U

/*
 * Lines go to the SD card writer, which owns the file.
 *
 * English contract: logging never blocks the caller on the card. The line is
 * queued and the writer writes and syncs it right away (flush interval 0).
 * Before error_log_store_start(), or without a card, lines are dropped and
 * only the serial log has them.
 */
static sd_card_writer_stream_t *volatile s_stream;
/*
 * Producers currently inside sd_card_writer_write() on s_stream.
 *
 * English contract: error_log_store_stop() closes and frees the stream while any
 * task may be logging. It first unpublishes s_stream, so no new producer can
 * pick it up, then waits for the ones already holding it to leave. That wait is
 * short because the write below never waits for room (wait_ticks 0).
 */
static portMUX_TYPE s_stream_lock = portMUX_INITIALIZER_UNLOCKED;
static volatile uint32_t s_producers_in_flight;
static bool s_sink_warning_emitted = false;

static void error_log_store_warn_sink_failure_once(esp_err_t err, const char *detail)
{
    if (s_sink_warning_emitted) {
        return;
    }

    s_sink_warning_emitted = true;
    ESP_LOGW(ERROR_LOG_TAG,
             "SD error log unavailable; further failures are not reported: %s (%s)",
             detail != NULL ? detail : "unknown",
             esp_err_to_name(err));
}

static esp_err_t error_log_store_find_next_index(uint16_t *out_next_index)
{
    char mount_root[96];
    const char *mount_point = sd_card_storage_get_mount_point();
    DIR *dir = NULL;
    struct dirent *entry;
    bool found = false;
    unsigned int max_index = 0;

    ESP_RETURN_ON_FALSE(out_next_index != NULL, ESP_ERR_INVALID_ARG, ERROR_LOG_TAG, "next index output is null");
    ESP_RETURN_ON_FALSE(sd_card_storage_is_mounted(), ESP_ERR_INVALID_STATE, ERROR_LOG_TAG, "sd card is not mounted");
    ESP_RETURN_ON_FALSE(mount_point != NULL, ESP_ERR_INVALID_STATE, ERROR_LOG_TAG, "sd mount point unavailable");

    int written = snprintf(mount_root, sizeof(mount_root), "%s", mount_point);
    ESP_RETURN_ON_FALSE(written > 0 && (size_t)written < sizeof(mount_root),
                        ESP_ERR_INVALID_SIZE,
                        ERROR_LOG_TAG,
                        "mount path is too long");

    dir = opendir(mount_root);
    ESP_RETURN_ON_FALSE(dir != NULL, ESP_FAIL, ERROR_LOG_TAG, "open mount root failed");

    while ((entry = readdir(dir)) != NULL) {
        unsigned int index = 0;

        if (sscanf(entry->d_name, ERROR_LOG_FILE_SCAN_FORMAT, &index) == 1 && index > max_index) {
            max_index = index;
            found = true;
        } else if (sscanf(entry->d_name, ERROR_LOG_FILE_SCAN_FORMAT, &index) == 1) {
            found = true;
        }
    }

    closedir(dir);
    if (!found) {
        *out_next_index = 0;
        return ESP_OK;
    }

    ESP_RETURN_ON_FALSE(max_index < ERROR_LOG_MAX_INDEX, ESP_ERR_INVALID_STATE, ERROR_LOG_TAG, "error log index exhausted");
    *out_next_index = (uint16_t)(max_index + 1U);
    return ESP_OK;
}

/* Runs on the writer task whenever the stream needs a new file. */
static esp_err_t error_log_store_pick_path(char *path, size_t path_size, void *ctx)
{
    (void)ctx;
    uint16_t next_index = 0;

    ESP_RETURN_ON_ERROR(error_log_store_find_next_index(&next_index),
                        ERROR_LOG_TAG,
                        "find next error log index failed");
    int written = snprintf(path, path_size, ERROR_LOG_FILE_FORMAT, (unsigned)next_index);
    ESP_RETURN_ON_FALSE(written > 0 && (size_t)written < path_size,
                        ESP_ERR_INVALID_SIZE,
                        ERROR_LOG_TAG,
                        "error log path is too long");
    return ESP_OK;
}

/* SD writer trouble (dropped records, failed streams) goes into this log. */
static void error_log_store_on_sd_report(const char *line, void *ctx)
{
    (void)ctx;
    (void)error_log_store_append_message("sd_card_writer", line);
}

esp_err_t error_log_store_start(void)
{
    if (s_stream != NULL) {
        return ESP_OK;
    }

    const sd_card_writer_stream_config_t config = {
        .name = "error_log",
        .path_fn = error_log_store_pick_path,
        .buffer_size = ERROR_LOG_BUFFER_SIZE,
        .flush_interval_ms = 0,
        .max_file_size = ERROR_LOG_MAX_FILE_SIZE,
        /* It carries the reports, so it must not report about itself. */
        .silent = true,
    };
    sd_card_writer_stream_t *stream = NULL;
    ESP_RETURN_ON_ERROR(sd_card_writer_open_stream(&config, &stream), ERROR_LOG_TAG, "error log stream failed");
    /* A new stream is a new chance to say why logging stopped. */
    s_sink_warning_emitted = false;
    portENTER_CRITICAL(&s_stream_lock);
    s_stream = stream;
    portEXIT_CRITICAL(&s_stream_lock);
    sd_card_writer_set_report_fn(error_log_store_on_sd_report, NULL);
    return ESP_OK;
}

esp_err_t error_log_store_stop(void)
{
    portENTER_CRITICAL(&s_stream_lock);
    sd_card_writer_stream_t *stream = s_stream;
    s_stream = NULL;
    portEXIT_CRITICAL(&s_stream_lock);
    if (stream == NULL) {
        return ESP_OK;
    }

    while (s_producers_in_flight > 0) {
        vTaskDelay(1);
    }
    /* ESP_FAIL: the stream had already failed, or failed while closing. It is
       closed and freed either way. */
    return sd_card_writer_close_stream(stream);
}

bool error_log_store_is_failed(void)
{
    sd_card_writer_stream_t *stream = s_stream;
    sd_card_writer_stats_t stats = { 0 };

    if (stream == NULL || sd_card_writer_get_stats(stream, &stats) != ESP_OK) {
        return false;
    }
    return stats.failed;
}

esp_err_t error_log_store_write_error_log(const char *line)
{
    ESP_RETURN_ON_FALSE(line != NULL, ESP_ERR_INVALID_ARG, ERROR_LOG_TAG, "line is null");

    portENTER_CRITICAL(&s_stream_lock);
    sd_card_writer_stream_t *stream = s_stream;
    if (stream != NULL) {
        ++s_producers_in_flight;
    }
    portEXIT_CRITICAL(&s_stream_lock);
    if (stream == NULL) {
        /* Not started (early boot, or no card): the caller's own serial log
           line is all there is. */
        return ESP_OK;
    }

    esp_err_t err = sd_card_writer_write(stream, line, strlen(line), 0);
    portENTER_CRITICAL(&s_stream_lock);
    --s_producers_in_flight;
    portEXIT_CRITICAL(&s_stream_lock);
    if (err != ESP_OK) {
        error_log_store_warn_sink_failure_once(err, "error log line dropped");
    }
    /* Logging an error must never fail the caller. */
    return ESP_OK;
}

esp_err_t error_log_store_append_message(const char *tag, const char *message)
{
    char line[ERROR_LOG_LINE_MAX];
    const char *safe_tag = tag != NULL ? tag : "?";
    const char *safe_message = message != NULL ? message : "(null)";
    uint64_t uptime_ms = (uint64_t)(esp_timer_get_time() / 1000LL);

    int written = snprintf(line,
                           sizeof(line),
                           "[%llu ms] %s: %s\n",
                           (unsigned long long)uptime_ms,
                           safe_tag,
                           safe_message);
    ESP_RETURN_ON_FALSE(written > 0 && (size_t)written < sizeof(line),
                        ESP_ERR_INVALID_SIZE,
                        ERROR_LOG_TAG,
                        "error log line too long");
    return error_log_store_write_error_log(line);
}

esp_err_t error_log_store_append_esp_err(const char *tag, const char *message, esp_err_t err)
{
    char line[ERROR_LOG_LINE_MAX];
    const char *safe_tag = tag != NULL ? tag : "?";
    const char *safe_message = message != NULL ? message : "(null)";
    uint64_t uptime_ms = (uint64_t)(esp_timer_get_time() / 1000LL);

    int written = snprintf(line,
                           sizeof(line),
                           "[%llu ms] %s: %s: %s\n",
                           (unsigned long long)uptime_ms,
                           safe_tag,
                           safe_message,
                           esp_err_to_name(err));
    ESP_RETURN_ON_FALSE(written > 0 && (size_t)written < sizeof(line),
                        ESP_ERR_INVALID_SIZE,
                        ERROR_LOG_TAG,
                        "error log line too long");
    return error_log_store_write_error_log(line);
}
