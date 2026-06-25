#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "error_log_store.h"
#include "sd_card_files.h"

static const char *ERROR_LOG_TAG = "error_log_store";
static const char *ERROR_LOG_PATH = "error.log";
static const size_t ERROR_LOG_LINE_MAX = 256;
static bool s_sink_warning_emitted = false;

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

static esp_err_t error_log_store_write_line(const char *line)
{
    esp_err_t err = sd_card_files_append_existing_text(ERROR_LOG_PATH, line);
    if (err == ESP_ERR_NOT_FOUND) {
        err = sd_card_files_write_new_text(ERROR_LOG_PATH, line);
    }
    if (err != ESP_OK) {
        error_log_store_warn_sink_failure_once(err, "write_line failed");
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
    return error_log_store_write_line(line);
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
    return error_log_store_write_line(line);
}
