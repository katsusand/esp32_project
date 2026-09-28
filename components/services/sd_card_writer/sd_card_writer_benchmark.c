#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "sd_card_writer.h"
#include "sd_card_writer_internal.h"

#if CONFIG_SD_CARD_WRITER_BENCHMARK

#define TAG "sd_card_bench"
#define SD_CARD_BENCH_PERIOD_MS 10U

/*
 * Writes CONFIG_SD_CARD_WRITER_BENCHMARK_RATE_KBPS to BENCH.BIN for a while
 * and logs what reached the card. The file holds consecutive little-endian
 * uint32 values, so a jump in the sequence shows where records were dropped.
 */
static void sd_card_writer_benchmark_task(void *arg)
{
    (void)arg;

    const sd_card_writer_stream_config_t config = {
        .name = "bench",
        .path = "BENCH.BIN",
        .truncate = true,
        .buffer_size = (size_t)CONFIG_SD_CARD_WRITER_BENCHMARK_BUFFER_KB * 1024U,
        .flush_interval_ms = 1000,
    };
    const size_t block_bytes = (size_t)CONFIG_SD_CARD_WRITER_BENCHMARK_BLOCK_BYTES & ~(size_t)3U;
    const uint32_t bytes_per_period = (uint32_t)CONFIG_SD_CARD_WRITER_BENCHMARK_RATE_KBPS * 1024U *
                                      SD_CARD_BENCH_PERIOD_MS / 1000U;
    static uint32_t s_block[CONFIG_SD_CARD_WRITER_BENCHMARK_BLOCK_BYTES / sizeof(uint32_t)];
    sd_card_writer_stream_t *stream = NULL;
    uint32_t counter = 0;

    if (sd_card_writer_open_stream(&config, &stream) != ESP_OK) {
        ESP_LOGE(TAG, "benchmark stream unavailable");
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG,
             "writing %u KB/s in %u-byte records for %u s (buffer %u KB)",
             (unsigned)CONFIG_SD_CARD_WRITER_BENCHMARK_RATE_KBPS,
             (unsigned)block_bytes,
             (unsigned)CONFIG_SD_CARD_WRITER_BENCHMARK_SECONDS,
             (unsigned)CONFIG_SD_CARD_WRITER_BENCHMARK_BUFFER_KB);

    const TickType_t started = xTaskGetTickCount();
    const TickType_t duration = pdMS_TO_TICKS(CONFIG_SD_CARD_WRITER_BENCHMARK_SECONDS * 1000U);
    TickType_t last_wake = started;
    TickType_t last_report = started;
    uint32_t owed = 0;

    while (xTaskGetTickCount() - started < duration) {
        owed += bytes_per_period;
        while (owed >= block_bytes) {
            for (size_t i = 0; i < block_bytes / sizeof(uint32_t); ++i) {
                s_block[i] = counter++;
            }
            (void)sd_card_writer_write(stream, s_block, block_bytes, 0);
            owed -= (uint32_t)block_bytes;
        }

        if (xTaskGetTickCount() - last_report >= pdMS_TO_TICKS(1000)) {
            sd_card_writer_stats_t stats = { 0 };
            (void)sd_card_writer_get_stats(stream, &stats);
            ESP_LOGI(TAG,
                     "accepted=%u KB written=%u KB dropped=%u KB (%u records) waiting max=%u KB",
                     (unsigned)(stats.bytes_accepted / 1024U),
                     (unsigned)(stats.bytes_written / 1024U),
                     (unsigned)(stats.bytes_dropped / 1024U),
                     (unsigned)stats.records_dropped,
                     (unsigned)(stats.buffer_high_water / 1024U));
            last_report = xTaskGetTickCount();
        }
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(SD_CARD_BENCH_PERIOD_MS) > 0 ? pdMS_TO_TICKS(SD_CARD_BENCH_PERIOD_MS) : 1);
    }

    (void)sd_card_writer_flush(stream);
    uint32_t elapsed_ms = (uint32_t)((xTaskGetTickCount() - started) * portTICK_PERIOD_MS);
    sd_card_writer_stats_t stats = { 0 };
    (void)sd_card_writer_get_stats(stream, &stats);
    ESP_LOGI(TAG,
             "done: %u KB written in %u ms (%u KB/s), dropped %u KB in %u records, waiting max %u KB%s",
             (unsigned)(stats.bytes_written / 1024U),
             (unsigned)elapsed_ms,
             (unsigned)(elapsed_ms > 0 ? stats.bytes_written * 1000U / 1024U / elapsed_ms : 0U),
             (unsigned)(stats.bytes_dropped / 1024U),
             (unsigned)stats.records_dropped,
             (unsigned)(stats.buffer_high_water / 1024U),
             stats.failed ? ", stream FAILED" : "");
    (void)sd_card_writer_close_stream(stream);
    vTaskDelete(NULL);
}

void sd_card_writer_benchmark_start(void)
{
    if (xTaskCreate(sd_card_writer_benchmark_task, "sd_card_bench", 3072, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "benchmark task create failed");
    }
}

#endif
