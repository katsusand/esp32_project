#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "app_stack_monitor.h"
#include "sd_card_storage.h"
#include "sd_card_writer.h"
#include "sd_card_writer_internal.h"

#ifndef CONFIG_SD_CARD_WRITER_TASK_STACK_SIZE
#define CONFIG_SD_CARD_WRITER_TASK_STACK_SIZE 4096
#endif
#ifndef CONFIG_SD_CARD_WRITER_TASK_PRIORITY
#define CONFIG_SD_CARD_WRITER_TASK_PRIORITY 5
#endif
#ifndef CONFIG_SD_CARD_WRITER_MAX_STREAMS
#define CONFIG_SD_CARD_WRITER_MAX_STREAMS 4
#endif
#ifndef CONFIG_SD_CARD_WRITER_CHUNK_SIZE
#define CONFIG_SD_CARD_WRITER_CHUNK_SIZE 8192
#endif
#ifndef CONFIG_SD_CARD_WRITER_TICK_MS
#define CONFIG_SD_CARD_WRITER_TICK_MS 100
#endif

#define TAG "sd_card_writer"
/* SD cards address 512-byte sectors, whatever FATFS is configured for. */
#define SD_CARD_WRITER_SECTOR_SIZE 512U
#define SD_CARD_WRITER_PATH_MAX 64
#define SD_CARD_WRITER_NAME_MAX 15
#define SD_CARD_WRITER_MIN_BUFFER_SIZE 256U
#define SD_CARD_WRITER_ROTATION_SLOTS 4U
#define SD_CARD_WRITER_CMD_QUEUE_LENGTH 8
#define SD_CARD_WRITER_STACK_LOG_INTERVAL_MS 30000

_Static_assert(CONFIG_SD_CARD_WRITER_CHUNK_SIZE % SD_CARD_WRITER_SECTOR_SIZE == 0,
               "SD_CARD_WRITER_CHUNK_SIZE must be whole sectors");

struct sd_card_writer_stream {
    /* Fixed once the stream is registered. */
    char name[SD_CARD_WRITER_NAME_MAX + 1];
    char path[SD_CARD_WRITER_PATH_MAX];
    bool truncate;
    sd_card_writer_path_fn_t path_fn;
    void *path_ctx;
    size_t buffer_size;
    uint32_t flush_interval_ms;
    uint32_t max_file_size;
    StreamBufferHandle_t buffer;
    /* Serializes producers, so the stream buffer sees one writer at a time. */
    SemaphoreHandle_t producer_mutex;
    /* Given by the writer after it made room for a waiting producer. */
    SemaphoreHandle_t space_sem;

    /* Shared between producers and the writer, under `lock`. */
    portMUX_TYPE lock;
    volatile bool failed;
    volatile bool space_wanted;
    /* Bytes ever queued, and the producers' view of the current file size.
       A rotation point is the queued total at which the next file starts. */
    uint64_t enqueued_total;
    uint64_t producer_file_bytes;
    uint64_t rotation_points[SD_CARD_WRITER_ROTATION_SLOTS];
    uint8_t rotation_head;
    uint8_t rotation_count;
    sd_card_writer_stats_t stats;

    /* Writer task only. */
    int fd;
    uint64_t drained_total;
    uint64_t file_pos;
    bool dirty;
    TickType_t last_sync_tick;
};

typedef enum {
    SD_CARD_WRITER_CMD_OPEN = 0,
    SD_CARD_WRITER_CMD_FLUSH,
    SD_CARD_WRITER_CMD_CLOSE,
} sd_card_writer_cmd_type_t;

typedef struct {
    SemaphoreHandle_t done;
    esp_err_t result;
} sd_card_writer_completion_t;

typedef struct {
    sd_card_writer_cmd_type_t type;
    sd_card_writer_stream_t *stream;
    /* The waiting caller's, on its stack. */
    sd_card_writer_completion_t *completion;
} sd_card_writer_cmd_t;

static TaskHandle_t s_writer_task;
static QueueHandle_t s_cmd_queue;
/* DMA-capable and word aligned, so whole sectors go from here to the card
   without the SPI driver allocating a bounce buffer. Writer task only. */
static uint8_t *s_chunk;
static sd_card_writer_stream_t *s_streams[CONFIG_SD_CARD_WRITER_MAX_STREAMS];
static size_t s_stream_count;

static bool sd_card_writer_on_writer_task(void)
{
    return s_writer_task != NULL && xTaskGetCurrentTaskHandle() == s_writer_task;
}

static void sd_card_writer_wake(void)
{
    TaskHandle_t task = s_writer_task;

    if (task != NULL) {
        xTaskNotifyGive(task);
    }
}

static void sd_card_writer_count_dropped(sd_card_writer_stream_t *stream, size_t bytes, uint32_t records)
{
    portENTER_CRITICAL(&stream->lock);
    stream->stats.bytes_dropped += bytes;
    stream->stats.records_dropped += records;
    portEXIT_CRITICAL(&stream->lock);
}

static void sd_card_writer_release_space(sd_card_writer_stream_t *stream)
{
    if (stream->space_wanted) {
        stream->space_wanted = false;
        xSemaphoreGive(stream->space_sem);
    }
}

static bool sd_card_writer_peek_rotation(sd_card_writer_stream_t *stream, uint64_t *point)
{
    portENTER_CRITICAL(&stream->lock);
    bool has_point = stream->rotation_count > 0;
    if (has_point) {
        *point = stream->rotation_points[stream->rotation_head];
    }
    portEXIT_CRITICAL(&stream->lock);
    return has_point;
}

static void sd_card_writer_pop_rotation(sd_card_writer_stream_t *stream)
{
    portENTER_CRITICAL(&stream->lock);
    if (stream->rotation_count > 0) {
        stream->rotation_head = (uint8_t)((stream->rotation_head + 1U) % SD_CARD_WRITER_ROTATION_SLOTS);
        --stream->rotation_count;
    }
    portEXIT_CRITICAL(&stream->lock);
}

/* Drops whatever is buffered: used once a stream can no longer write. */
static void sd_card_writer_discard(sd_card_writer_stream_t *stream)
{
    size_t discarded = 0;
    size_t received = 0;

    while ((received = xStreamBufferReceive(stream->buffer, s_chunk, CONFIG_SD_CARD_WRITER_CHUNK_SIZE, 0)) > 0) {
        discarded += received;
    }
    if (discarded > 0) {
        sd_card_writer_count_dropped(stream, discarded, 0);
    }
    sd_card_writer_release_space(stream);
}

/*
 * No retries: without card detection there is no telling when the card is
 * usable again, and a half-failed card would keep every producer waiting.
 * The stream drops its data from here on, and says so once.
 */
static void sd_card_writer_fail(sd_card_writer_stream_t *stream, const char *what, int err_no)
{
    if (!stream->failed) {
        ESP_LOGE(TAG, "%s: %s failed (errno %d); dropping its data from now on", stream->name, what, err_no);
    }
    stream->failed = true;
    portENTER_CRITICAL(&stream->lock);
    stream->stats.failed = true;
    portEXIT_CRITICAL(&stream->lock);

    if (stream->fd >= 0) {
        (void)close(stream->fd);
        stream->fd = -1;
    }
    stream->dirty = false;
    sd_card_writer_discard(stream);
}

static bool sd_card_writer_make_parent_dirs(char *full_path)
{
    size_t skip = strlen(sd_card_storage_get_mount_point()) + 1U;

    for (char *p = full_path + skip; *p != '\0'; ++p) {
        if (*p != '/') {
            continue;
        }
        *p = '\0';
        int rc = mkdir(full_path, 0775);
        int saved_errno = errno;
        *p = '/';
        if (rc != 0 && saved_errno != EEXIST) {
            errno = saved_errno;
            return false;
        }
    }
    return true;
}

static bool sd_card_writer_open_file(sd_card_writer_stream_t *stream)
{
    char relative[SD_CARD_WRITER_PATH_MAX];
    char full_path[SD_CARD_WRITER_PATH_MAX + 16];
    int flags = O_WRONLY | O_CREAT;
    bool append_existing = false;

    if (stream->path_fn != NULL) {
        esp_err_t err = stream->path_fn(relative, sizeof(relative), stream->path_ctx);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "%s: no file to open: %s", stream->name, esp_err_to_name(err));
            sd_card_writer_fail(stream, "pick file", 0);
            return false;
        }
        /* A picked file must be new; never append to or truncate another's. */
        flags |= O_EXCL;
    } else {
        strlcpy(relative, stream->path, sizeof(relative));
        if (stream->truncate) {
            flags |= O_TRUNC;
        } else {
            append_existing = true;
        }
    }

    const char *rel = relative;
    while (*rel == '/') {
        ++rel;
    }
    int written = snprintf(full_path, sizeof(full_path), "%s/%s", sd_card_storage_get_mount_point(), rel);
    if (written <= 0 || (size_t)written >= sizeof(full_path)) {
        sd_card_writer_fail(stream, "path too long", 0);
        return false;
    }
    if (!sd_card_writer_make_parent_dirs(full_path)) {
        sd_card_writer_fail(stream, "create directory", errno);
        return false;
    }

    int fd = open(full_path, flags, 0666);
    if (fd < 0) {
        sd_card_writer_fail(stream, "open", errno);
        return false;
    }
    off_t end = 0;
    if (append_existing) {
        /* One seek now instead of O_APPEND, which the FATFS VFS turns into a
           seek before every write. Nothing else writes this file. */
        end = lseek(fd, 0, SEEK_END);
        if (end < 0) {
            int saved_errno = errno;
            (void)close(fd);
            sd_card_writer_fail(stream, "seek", saved_errno);
            return false;
        }
    }

    stream->fd = fd;
    stream->file_pos = end > 0 ? (uint64_t)end : 0U;
    stream->dirty = false;
    stream->last_sync_tick = xTaskGetTickCount();
    portENTER_CRITICAL(&stream->lock);
    ++stream->stats.files_opened;
    portEXIT_CRITICAL(&stream->lock);
    ESP_LOGI(TAG, "%s: writing %s", stream->name, full_path);
    return true;
}

static bool sd_card_writer_sync(sd_card_writer_stream_t *stream)
{
    if (stream->fd < 0 || !stream->dirty) {
        return true;
    }
    if (fsync(stream->fd) != 0) {
        sd_card_writer_fail(stream, "sync", errno);
        return false;
    }
    stream->dirty = false;
    stream->last_sync_tick = xTaskGetTickCount();
    return true;
}

static void sd_card_writer_close_file(sd_card_writer_stream_t *stream)
{
    if (stream->fd < 0) {
        return;
    }
    if (!sd_card_writer_sync(stream)) {
        return; /* the failure already closed it */
    }
    if (close(stream->fd) != 0) {
        ESP_LOGW(TAG, "%s: close failed (errno %d)", stream->name, errno);
    }
    stream->fd = -1;
}

/*
 * How much to hand to one write() call.
 *
 * English contract: whole sectors are written only at sector-aligned file
 * offsets and always from the start of s_chunk, so FATFS sends them straight
 * from that aligned DMA buffer to the card. A part that fills up an already
 * started sector, or a tail shorter than a sector, goes through FATFS' own
 * per-file sector cache and reaches the card when the sector completes or at
 * the next sync.
 */
static size_t sd_card_writer_next_chunk(const sd_card_writer_stream_t *stream, uint64_t limit)
{
    uint64_t head = (SD_CARD_WRITER_SECTOR_SIZE - (stream->file_pos % SD_CARD_WRITER_SECTOR_SIZE)) %
                    SD_CARD_WRITER_SECTOR_SIZE;
    uint64_t size = 0;

    if (head > 0) {
        size = limit < head ? limit : head;
    } else {
        size = limit < CONFIG_SD_CARD_WRITER_CHUNK_SIZE ? limit : CONFIG_SD_CARD_WRITER_CHUNK_SIZE;
        if (size > SD_CARD_WRITER_SECTOR_SIZE) {
            size -= size % SD_CARD_WRITER_SECTOR_SIZE;
        }
    }
    return (size_t)size;
}

/* Moves what is buffered right now to the card, then syncs if due. */
static void sd_card_writer_service(sd_card_writer_stream_t *stream, bool force_sync)
{
    if (stream->failed) {
        sd_card_writer_discard(stream);
        return;
    }

    size_t available = xStreamBufferBytesAvailable(stream->buffer);
    while (available > 0) {
        uint64_t limit = available;
        uint64_t rotation_point = 0;

        if (sd_card_writer_peek_rotation(stream, &rotation_point)) {
            if (rotation_point <= stream->drained_total) {
                /* The next record starts a new file; open it lazily below. */
                sd_card_writer_pop_rotation(stream);
                sd_card_writer_close_file(stream);
                if (stream->failed) {
                    return;
                }
                continue;
            }
            uint64_t until_rotation = rotation_point - stream->drained_total;
            limit = until_rotation < limit ? until_rotation : limit;
        }
        if (stream->fd < 0 && !sd_card_writer_open_file(stream)) {
            return;
        }

        size_t size = sd_card_writer_next_chunk(stream, limit);
        size_t received = xStreamBufferReceive(stream->buffer, s_chunk, size, 0);
        if (received == 0) {
            break;
        }
        ssize_t written = write(stream->fd, s_chunk, received);
        if (written != (ssize_t)received) {
            sd_card_writer_fail(stream, "write", errno);
            return;
        }

        stream->file_pos += received;
        stream->drained_total += received;
        stream->dirty = true;
        available -= received;
        portENTER_CRITICAL(&stream->lock);
        stream->stats.bytes_written += received;
        portEXIT_CRITICAL(&stream->lock);
        sd_card_writer_release_space(stream);
    }

    if (stream->dirty) {
        bool due = force_sync ||
                   stream->flush_interval_ms == 0 ||
                   xTaskGetTickCount() - stream->last_sync_tick >= pdMS_TO_TICKS(stream->flush_interval_ms);
        if (due) {
            (void)sd_card_writer_sync(stream);
        }
    }
    sd_card_writer_release_space(stream);
}

static void sd_card_writer_complete(const sd_card_writer_cmd_t *cmd, esp_err_t result)
{
    if (cmd->completion != NULL) {
        cmd->completion->result = result;
        /* The caller may return as soon as this is given. */
        xSemaphoreGive(cmd->completion->done);
    }
}

static void sd_card_writer_handle_cmd(const sd_card_writer_cmd_t *cmd)
{
    sd_card_writer_stream_t *stream = cmd->stream;

    switch (cmd->type) {
    case SD_CARD_WRITER_CMD_OPEN:
        if (s_stream_count >= CONFIG_SD_CARD_WRITER_MAX_STREAMS) {
            ESP_LOGE(TAG, "%s: no stream slot left (max %d)", stream->name, CONFIG_SD_CARD_WRITER_MAX_STREAMS);
            sd_card_writer_complete(cmd, ESP_ERR_NO_MEM);
            return;
        }
        s_streams[s_stream_count++] = stream;
        sd_card_writer_complete(cmd, ESP_OK);
        return;
    case SD_CARD_WRITER_CMD_FLUSH:
        sd_card_writer_service(stream, true);
        sd_card_writer_complete(cmd, stream->failed ? ESP_FAIL : ESP_OK);
        return;
    case SD_CARD_WRITER_CMD_CLOSE:
        sd_card_writer_service(stream, true);
        sd_card_writer_close_file(stream);
        for (size_t i = 0; i < s_stream_count; ++i) {
            if (s_streams[i] == stream) {
                s_streams[i] = s_streams[--s_stream_count];
                s_streams[s_stream_count] = NULL;
                break;
            }
        }
        sd_card_writer_complete(cmd, stream->failed ? ESP_FAIL : ESP_OK);
        return;
    default:
        sd_card_writer_complete(cmd, ESP_ERR_INVALID_ARG);
        return;
    }
}

static void sd_card_writer_task(void *arg)
{
    (void)arg;

    while (true) {
        APP_STACK_MONITOR_CHECK(TAG, "sd_card_writer", SD_CARD_WRITER_STACK_LOG_INTERVAL_MS);
        /* Woken by producers once enough is buffered, and by requests; the
           timeout drives interval syncs of streams that went quiet. */
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(CONFIG_SD_CARD_WRITER_TICK_MS));

        sd_card_writer_cmd_t cmd;
        while (xQueueReceive(s_cmd_queue, &cmd, 0) == pdTRUE) {
            sd_card_writer_handle_cmd(&cmd);
        }
        for (size_t i = 0; i < s_stream_count; ++i) {
            sd_card_writer_service(s_streams[i], false);
        }
    }
}

/*
 * Hands a request to the writer task and waits until it has been handled.
 * No timeout: the completion lives in this frame, and the writer answers
 * every request once its file work is done.
 */
static esp_err_t sd_card_writer_call(sd_card_writer_cmd_type_t type, sd_card_writer_stream_t *stream)
{
    ESP_RETURN_ON_FALSE(s_writer_task != NULL, ESP_ERR_INVALID_STATE, TAG, "writer not started");
    ESP_RETURN_ON_FALSE(!sd_card_writer_on_writer_task(),
                        ESP_ERR_INVALID_STATE,
                        TAG,
                        "request from the writer task would wait on itself");

    StaticSemaphore_t done_storage;
    sd_card_writer_completion_t completion = {
        .done = xSemaphoreCreateBinaryStatic(&done_storage),
        .result = ESP_FAIL,
    };
    sd_card_writer_cmd_t cmd = {
        .type = type,
        .stream = stream,
        .completion = &completion,
    };
    (void)xQueueSend(s_cmd_queue, &cmd, portMAX_DELAY);
    sd_card_writer_wake();
    (void)xSemaphoreTake(completion.done, portMAX_DELAY);
    vSemaphoreDelete(completion.done);
    return completion.result;
}

static void sd_card_writer_free_stream(sd_card_writer_stream_t *stream)
{
    if (stream == NULL) {
        return;
    }
    if (stream->buffer != NULL) {
        vStreamBufferDelete(stream->buffer);
    }
    if (stream->producer_mutex != NULL) {
        vSemaphoreDelete(stream->producer_mutex);
    }
    if (stream->space_sem != NULL) {
        vSemaphoreDelete(stream->space_sem);
    }
    heap_caps_free(stream);
}

esp_err_t sd_card_writer_start(void)
{
    if (s_writer_task != NULL) {
        return ESP_OK;
    }
    ESP_RETURN_ON_FALSE(sd_card_storage_is_mounted(), ESP_ERR_INVALID_STATE, TAG, "SD card is not mounted");

    if (s_chunk == NULL) {
        s_chunk = heap_caps_malloc(CONFIG_SD_CARD_WRITER_CHUNK_SIZE, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        ESP_RETURN_ON_FALSE(s_chunk != NULL, ESP_ERR_NO_MEM, TAG, "chunk buffer alloc failed");
    }
    if (s_cmd_queue == NULL) {
        s_cmd_queue = xQueueCreate(SD_CARD_WRITER_CMD_QUEUE_LENGTH, sizeof(sd_card_writer_cmd_t));
        ESP_RETURN_ON_FALSE(s_cmd_queue != NULL, ESP_ERR_NO_MEM, TAG, "command queue alloc failed");
    }

    BaseType_t task_ok = xTaskCreate(sd_card_writer_task,
                                     "sd_card_writer",
                                     CONFIG_SD_CARD_WRITER_TASK_STACK_SIZE,
                                     NULL,
                                     CONFIG_SD_CARD_WRITER_TASK_PRIORITY,
                                     &s_writer_task);
    if (task_ok != pdPASS) {
        s_writer_task = NULL;
        ESP_LOGE(TAG, "task create failed");
        return ESP_ERR_NO_MEM;
    }

#if CONFIG_SD_CARD_WRITER_BENCHMARK
    sd_card_writer_benchmark_start();
#endif
    return ESP_OK;
}

bool sd_card_writer_is_running(void)
{
    return s_writer_task != NULL;
}

esp_err_t sd_card_writer_open_stream(const sd_card_writer_stream_config_t *config,
                                     sd_card_writer_stream_t **out_stream)
{
    ESP_RETURN_ON_FALSE(config != NULL && out_stream != NULL, ESP_ERR_INVALID_ARG, TAG, "config and out_stream required");
    *out_stream = NULL;

    bool has_path = config->path != NULL && config->path[0] != '\0';
    ESP_RETURN_ON_FALSE(has_path != (config->path_fn != NULL),
                        ESP_ERR_INVALID_ARG,
                        TAG,
                        "set exactly one of path and path_fn");
    ESP_RETURN_ON_FALSE(!has_path || strlen(config->path) < SD_CARD_WRITER_PATH_MAX,
                        ESP_ERR_INVALID_ARG,
                        TAG,
                        "path too long");
    ESP_RETURN_ON_FALSE(config->max_file_size == 0 || config->path_fn != NULL,
                        ESP_ERR_INVALID_ARG,
                        TAG,
                        "rotation needs path_fn");
    ESP_RETURN_ON_FALSE(config->buffer_size >= SD_CARD_WRITER_MIN_BUFFER_SIZE,
                        ESP_ERR_INVALID_ARG,
                        TAG,
                        "buffer_size too small");
    ESP_RETURN_ON_FALSE(s_writer_task != NULL, ESP_ERR_INVALID_STATE, TAG, "writer not started");

    sd_card_writer_stream_t *stream = heap_caps_calloc(1, sizeof(*stream), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    ESP_RETURN_ON_FALSE(stream != NULL, ESP_ERR_NO_MEM, TAG, "stream alloc failed");

    strlcpy(stream->name, config->name != NULL ? config->name : "stream", sizeof(stream->name));
    if (has_path) {
        strlcpy(stream->path, config->path, sizeof(stream->path));
    }
    stream->truncate = config->truncate;
    stream->path_fn = config->path_fn;
    stream->path_ctx = config->path_ctx;
    stream->buffer_size = config->buffer_size;
    stream->flush_interval_ms = config->flush_interval_ms;
    stream->max_file_size = config->max_file_size;
    portMUX_INITIALIZE(&stream->lock);
    stream->fd = -1;
    stream->buffer = xStreamBufferCreate(config->buffer_size, 1);
    stream->producer_mutex = xSemaphoreCreateMutex();
    stream->space_sem = xSemaphoreCreateBinary();
    if (stream->buffer == NULL || stream->producer_mutex == NULL || stream->space_sem == NULL) {
        ESP_LOGE(TAG, "%s: buffer alloc failed (%u bytes)", stream->name, (unsigned)config->buffer_size);
        sd_card_writer_free_stream(stream);
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = sd_card_writer_call(SD_CARD_WRITER_CMD_OPEN, stream);
    if (err != ESP_OK) {
        sd_card_writer_free_stream(stream);
        return err;
    }
    *out_stream = stream;
    return ESP_OK;
}

esp_err_t sd_card_writer_write(sd_card_writer_stream_t *stream,
                               const void *data,
                               size_t size,
                               TickType_t wait_ticks)
{
    if (stream == NULL || (data == NULL && size > 0)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (size == 0) {
        return ESP_OK;
    }
    if (size > stream->buffer_size) {
        sd_card_writer_count_dropped(stream, size, 1);
        return ESP_ERR_INVALID_SIZE;
    }
    if (stream->failed) {
        sd_card_writer_count_dropped(stream, size, 1);
        return ESP_ERR_INVALID_STATE;
    }

    if (sd_card_writer_on_writer_task()) {
        wait_ticks = 0; /* room is made by this very task, so waiting would stall it */
    }

    TickType_t started = xTaskGetTickCount();
    if (xSemaphoreTake(stream->producer_mutex, wait_ticks) != pdTRUE) {
        sd_card_writer_count_dropped(stream, size, 1);
        return ESP_ERR_TIMEOUT;
    }

    /* All or nothing: a record that does not fit in time is dropped whole,
       never cut, so the file keeps whole records. */
    esp_err_t err = ESP_OK;
    while (xStreamBufferSpacesAvailable(stream->buffer) < size) {
        TickType_t elapsed = xTaskGetTickCount() - started;
        if (elapsed >= wait_ticks) {
            err = ESP_ERR_TIMEOUT;
            break;
        }
        stream->space_wanted = true;
        sd_card_writer_wake();
        (void)xSemaphoreTake(stream->space_sem, wait_ticks - elapsed);
        if (stream->failed) {
            err = ESP_ERR_INVALID_STATE;
            break;
        }
    }

    size_t waiting = 0;
    if (err == ESP_OK) {
        portENTER_CRITICAL(&stream->lock);
        if (stream->max_file_size > 0 &&
            stream->producer_file_bytes > 0 &&
            stream->producer_file_bytes + size > stream->max_file_size &&
            stream->rotation_count < SD_CARD_WRITER_ROTATION_SLOTS) {
            /* This record starts the next file. With every slot in use the
               file just runs over until the writer catches up. */
            uint8_t slot = (uint8_t)((stream->rotation_head + stream->rotation_count) % SD_CARD_WRITER_ROTATION_SLOTS);
            stream->rotation_points[slot] = stream->enqueued_total;
            ++stream->rotation_count;
            stream->producer_file_bytes = 0;
        }
        stream->enqueued_total += size;
        stream->producer_file_bytes += size;
        stream->stats.bytes_accepted += size;
        portEXIT_CRITICAL(&stream->lock);

        (void)xStreamBufferSend(stream->buffer, data, size, 0);
        waiting = xStreamBufferBytesAvailable(stream->buffer);
        portENTER_CRITICAL(&stream->lock);
        if (waiting > stream->stats.buffer_high_water) {
            stream->stats.buffer_high_water = waiting;
        }
        portEXIT_CRITICAL(&stream->lock);
    } else {
        sd_card_writer_count_dropped(stream, size, 1);
    }
    xSemaphoreGive(stream->producer_mutex);

    size_t wake_level = stream->buffer_size / 2U < CONFIG_SD_CARD_WRITER_CHUNK_SIZE ?
                        stream->buffer_size / 2U :
                        CONFIG_SD_CARD_WRITER_CHUNK_SIZE;
    if (err == ESP_OK && (stream->flush_interval_ms == 0 || waiting >= wake_level)) {
        sd_card_writer_wake();
    }
    return err;
}

esp_err_t sd_card_writer_flush(sd_card_writer_stream_t *stream)
{
    ESP_RETURN_ON_FALSE(stream != NULL, ESP_ERR_INVALID_ARG, TAG, "stream required");
    return sd_card_writer_call(SD_CARD_WRITER_CMD_FLUSH, stream);
}

esp_err_t sd_card_writer_close_stream(sd_card_writer_stream_t *stream)
{
    ESP_RETURN_ON_FALSE(stream != NULL, ESP_ERR_INVALID_ARG, TAG, "stream required");
    esp_err_t err = sd_card_writer_call(SD_CARD_WRITER_CMD_CLOSE, stream);
    if (err == ESP_OK || err == ESP_FAIL) {
        /* The writer has let go of it either way. */
        sd_card_writer_free_stream(stream);
    }
    return err;
}

esp_err_t sd_card_writer_get_stats(sd_card_writer_stream_t *stream, sd_card_writer_stats_t *stats)
{
    ESP_RETURN_ON_FALSE(stream != NULL && stats != NULL, ESP_ERR_INVALID_ARG, TAG, "stream and stats required");

    portENTER_CRITICAL(&stream->lock);
    *stats = stream->stats;
    portEXIT_CRITICAL(&stream->lock);
    return ESP_OK;
}
