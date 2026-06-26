#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "error_log_store.h"
#include "sd_card_storage.h"
#include "sd_card_files.h"

static const char *ERROR_LOG_TAG = "error_log_store";
static const size_t ERROR_LOG_LINE_MAX = 256;
#define ERROR_LOG_PATH_MAX 32
#define ERROR_LOG_MAX_LINES_PER_FILE 5000U
#define ERROR_LOG_MAX_INDEX 9999U
static bool s_sink_warning_emitted = false;
static SemaphoreHandle_t s_log_mutex = NULL;
static char s_error_log_path[ERROR_LOG_PATH_MAX];
static bool s_error_log_path_ready = false;
static uint16_t s_error_log_index = 0;
static uint32_t s_error_log_line_count = 0;
static bool s_error_log_exhausted = false;

static void error_log_store_warn_sink_failure_once(esp_err_t err, const char *detail)
{
    if (s_sink_warning_emitted) {
        return;
    }

    s_sink_warning_emitted = true;
    ESP_LOGW(ERROR_LOG_TAG,
             "SD error log sink unavailable; ignoring future log-store failures: %s (%s)",
             detail != NULL ? detail : "unknown",
             esp_err_to_name(err));
}

static esp_err_t error_log_store_build_relative_path(uint16_t file_index, char *path, size_t path_size)
{
    int written = snprintf(path, path_size, "error_%04u.log", (unsigned)file_index);
    ESP_RETURN_ON_FALSE(written > 0 && (size_t)written < path_size,
                        ESP_ERR_INVALID_SIZE,
                        ERROR_LOG_TAG,
                        "error log path is too long");
    return ESP_OK;
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

        if (sscanf(entry->d_name, "error_%4u.log", &index) == 1 && index > max_index) {
            max_index = index;
            found = true;
        } else if (sscanf(entry->d_name, "error_%4u.log", &index) == 1) {
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

static esp_err_t error_log_store_open_next_file_locked(void)
{
    uint16_t next_index = 0;

    ESP_RETURN_ON_ERROR(error_log_store_find_next_index(&next_index),
                        ERROR_LOG_TAG,
                        "find next error log index failed");
    ESP_RETURN_ON_FALSE(next_index <= ERROR_LOG_MAX_INDEX,
                        ESP_ERR_INVALID_STATE,
                        ERROR_LOG_TAG,
                        "next error log index exhausted");
    ESP_RETURN_ON_ERROR(error_log_store_build_relative_path(next_index, s_error_log_path, sizeof(s_error_log_path)),
                        ERROR_LOG_TAG,
                        "build error log path failed");
    s_error_log_index = next_index;
    s_error_log_line_count = 0;
    s_error_log_path_ready = true;
    ESP_LOGI(ERROR_LOG_TAG, "SD error logging to %s", s_error_log_path);
    return ESP_OK;
}

static esp_err_t error_log_store_ensure_path_locked(void)
{
    if (s_error_log_exhausted) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_error_log_path_ready && s_error_log_line_count < ERROR_LOG_MAX_LINES_PER_FILE) {
        return ESP_OK;
    }

    if (s_error_log_path_ready && s_error_log_line_count >= ERROR_LOG_MAX_LINES_PER_FILE) {
        ESP_RETURN_ON_FALSE(s_error_log_index < ERROR_LOG_MAX_INDEX,
                            ESP_ERR_INVALID_STATE,
                            ERROR_LOG_TAG,
                            "error log index exhausted");
        s_error_log_path_ready = false;
    }

    return error_log_store_open_next_file_locked();
}

esp_err_t error_log_store_write_error_log(const char *line)
{
    esp_err_t err = ESP_OK;

    ESP_RETURN_ON_FALSE(line != NULL, ESP_ERR_INVALID_ARG, ERROR_LOG_TAG, "line is null");

    if (s_log_mutex == NULL) {
        s_log_mutex = xSemaphoreCreateMutex();
        ESP_RETURN_ON_FALSE(s_log_mutex != NULL, ESP_ERR_NO_MEM, ERROR_LOG_TAG, "error log mutex create failed");
    }

    if (xSemaphoreTake(s_log_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_FAIL;
    }

    err = error_log_store_ensure_path_locked();
    if (err == ESP_OK) {
        err = sd_card_files_append_existing_text(s_error_log_path, line);
        if (err == ESP_ERR_NOT_FOUND) {
            err = sd_card_files_write_new_text(s_error_log_path, line);
        }
        if (err == ESP_OK) {
            s_error_log_line_count++;
        }
    }

    xSemaphoreGive(s_log_mutex);

    if (err != ESP_OK) {
        if (err == ESP_ERR_INVALID_STATE) {
            s_error_log_exhausted = true;
        }
        error_log_store_warn_sink_failure_once(err, "write_error_log failed");
        return ESP_OK;
    }

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
