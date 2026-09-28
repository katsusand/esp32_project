#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * One task writes everything that streams to the SD card.
 *
 * English contract: producers hand bytes to a stream and return without
 * touching the card. The writer task owns every stream's file and moves
 * buffered bytes to the card in sector-aligned chunks, syncing at each
 * stream's flush interval. So a producer is never held up by card latency,
 * two producers of one file cannot interleave inside a record, and small
 * appends no longer cost a file open, write and close each.
 *
 * sd_card_writer_write() is all-or-nothing per call: a call is one record, it
 * is never split across files at rotation, and a call that does not fit in
 * time is dropped whole and counted. Call it from tasks, not from ISRs.
 * Producers of one stream may run on different tasks.
 */

typedef struct sd_card_writer_stream sd_card_writer_stream_t;

/*
 * Picks a new file for a stream: runs on the writer task when the stream first
 * needs a file and again at each rotation. Writes a path relative to the mount
 * point (8.3 names) into `path`; the file must not exist yet.
 */
typedef esp_err_t (*sd_card_writer_path_fn_t)(char *path, size_t path_size, void *ctx);

typedef struct {
    /* Short name for logs and diagnostics. */
    const char *name;
    /* Fixed file relative to the mount point, or NULL when path_fn is set.
       Missing parent directories are created. */
    const char *path;
    /* With a fixed path: start the file empty instead of appending. */
    bool truncate;
    /* Picks each new file instead of a fixed path. Required for rotation. */
    sd_card_writer_path_fn_t path_fn;
    void *path_ctx;
    /* RAM that holds bytes until they reach the card. See the sizing note in
       docs/sd_card_writer.md: it has to ride out a card stall at full rate. */
    size_t buffer_size;
    /* Longest time written bytes may wait for a sync. 0 syncs as soon as
       they are written, for rare but important lines such as errors. */
    uint32_t flush_interval_ms;
    /* With path_fn: start a new file before one would grow past this. 0 never
       rotates. A record is never split, so a file can exceed it by one record. */
    uint32_t max_file_size;
    /* Leaves this stream out of trouble reports (sd_card_writer_set_report_fn).
       Set it on the stream that carries the reports, so it never reports
       about itself. */
    bool silent;
} sd_card_writer_stream_config_t;

typedef struct {
    uint64_t bytes_accepted;
    uint64_t bytes_written;
    uint64_t bytes_dropped;
    uint32_t records_dropped;
    uint32_t files_opened;
    /* Most bytes that were waiting in RAM at once. */
    size_t buffer_high_water;
    bool failed;
} sd_card_writer_stats_t;

/*
 * Receives one line about a stream in trouble: records dropped for lack of
 * room (at most one line per stream every
 * CONFIG_SD_CARD_WRITER_DROP_REPORT_INTERVAL_MS, summing the drops in between),
 * or the failure that stopped a stream. Runs on the writer task, so it must
 * not block. error_log_store_start() points it at the error log.
 */
typedef void (*sd_card_writer_report_fn_t)(const char *line, void *ctx);

/* Starts the writer task. The card must be mounted (sd_card_storage). */
esp_err_t sd_card_writer_start(void);
bool sd_card_writer_is_running(void);
void sd_card_writer_set_report_fn(sd_card_writer_report_fn_t fn, void *ctx);

/*
 * Registers a stream. Its buffer is allocated here; the file itself is opened
 * by the writer task when the first bytes arrive, so a stream that never
 * receives data never creates a file.
 */
esp_err_t sd_card_writer_open_stream(const sd_card_writer_stream_config_t *config,
                                     sd_card_writer_stream_t **out_stream);

/*
 * Queues `size` bytes as one record. Waits up to `wait_ticks` for room when
 * the buffer is full; 0 never waits. ESP_ERR_TIMEOUT: dropped for lack of
 * room. ESP_ERR_INVALID_STATE: the stream has failed (card error) and drops
 * everything. ESP_ERR_INVALID_SIZE: larger than the whole buffer.
 */
esp_err_t sd_card_writer_write(sd_card_writer_stream_t *stream,
                               const void *data,
                               size_t size,
                               TickType_t wait_ticks);

/* Writes out and syncs everything queued so far, and waits until it is on the card. */
esp_err_t sd_card_writer_flush(sd_card_writer_stream_t *stream);

/* Flushes, closes the file and frees the stream. No producer may use it afterwards. */
esp_err_t sd_card_writer_close_stream(sd_card_writer_stream_t *stream);

esp_err_t sd_card_writer_get_stats(sd_card_writer_stream_t *stream, sd_card_writer_stats_t *stats);

#ifdef __cplusplus
}
#endif
