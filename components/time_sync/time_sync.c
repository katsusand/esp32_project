#include <stdbool.h>
#include <stdlib.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_random.h"
#include "sdkconfig.h"
#include "app_stack_monitor.h"
#include "time_sync.h"
#include "wifi_manager.h"

#ifndef CONFIG_TIME_SYNC_ENABLED
#define CONFIG_TIME_SYNC_ENABLED 1
#endif
#ifndef CONFIG_TIME_SYNC_TASK_STACK_SIZE
#define CONFIG_TIME_SYNC_TASK_STACK_SIZE 4096
#endif
#ifndef CONFIG_TIME_SYNC_TASK_PRIORITY
#define CONFIG_TIME_SYNC_TASK_PRIORITY 5
#endif
#ifndef CONFIG_TIME_SYNC_SERVER
#define CONFIG_TIME_SYNC_SERVER "ntp.nict.jp"
#endif
#ifndef CONFIG_TIME_SYNC_INTERVAL_MINUTES
#define CONFIG_TIME_SYNC_INTERVAL_MINUTES 120
#endif
#ifndef CONFIG_TIME_SYNC_JITTER_MINUTES
#define CONFIG_TIME_SYNC_JITTER_MINUTES 10
#endif
#ifndef CONFIG_TIME_SYNC_WAIT_TIMEOUT_MS
#define CONFIG_TIME_SYNC_WAIT_TIMEOUT_MS 15000
#endif
#ifndef CONFIG_TIME_SYNC_RETRY_DELAY_SECONDS
#define CONFIG_TIME_SYNC_RETRY_DELAY_SECONDS 10
#endif
#ifndef CONFIG_TIME_SYNC_RETRY_ATTEMPTS
#define CONFIG_TIME_SYNC_RETRY_ATTEMPTS 5
#endif
#ifndef CONFIG_TIME_SYNC_TIMEZONE
#define CONFIG_TIME_SYNC_TIMEZONE "JST-9"
#endif

#define TIME_SYNC_WIFI_WAIT_MS 60000

static const char *TAG = "time_sync";
static TaskHandle_t s_time_sync_task_handle;
static portMUX_TYPE s_time_sync_status_lock = portMUX_INITIALIZER_UNLOCKED;
static time_sync_state_t s_time_sync_state = TIME_SYNC_STATE_STOPPED;
static bool s_time_sync_has_last_success;
static bool s_time_sync_has_last_attempt;
static time_t s_time_sync_last_success_at;
static esp_err_t s_time_sync_last_attempt_status;

static void time_sync_set_state(time_sync_state_t state)
{
    portENTER_CRITICAL(&s_time_sync_status_lock);
    s_time_sync_state = state;
    portEXIT_CRITICAL(&s_time_sync_status_lock);
}

static void time_sync_record_attempt_status(esp_err_t status)
{
    time_t success_at = 0;

    if (status == ESP_OK) {
        time(&success_at);
    }

    portENTER_CRITICAL(&s_time_sync_status_lock);
    s_time_sync_has_last_attempt = true;
    s_time_sync_last_attempt_status = status;
    if (status == ESP_OK) {
        s_time_sync_last_success_at = success_at;
        s_time_sync_has_last_success = true;
    }
    portEXIT_CRITICAL(&s_time_sync_status_lock);
}

static bool time_sync_has_success(void)
{
    portENTER_CRITICAL(&s_time_sync_status_lock);
    bool has_last_success = s_time_sync_has_last_success;
    portEXIT_CRITICAL(&s_time_sync_status_lock);
    return has_last_success;
}

static void time_sync_apply_timezone_once(void)
{
    static bool s_timezone_applied;

    if (s_timezone_applied) {
        return;
    }

    if (CONFIG_TIME_SYNC_TIMEZONE[0] != '\0') {
        setenv("TZ", CONFIG_TIME_SYNC_TIMEZONE, 1);
        tzset();
    }
    s_timezone_applied = true;
}

static uint32_t time_sync_next_delay_seconds(void)
{
    const uint32_t base_seconds = CONFIG_TIME_SYNC_INTERVAL_MINUTES * 60U;
    const uint32_t jitter_seconds = CONFIG_TIME_SYNC_JITTER_MINUTES * 60U;

    if (jitter_seconds == 0) {
        return base_seconds;
    }

    uint32_t spread = jitter_seconds * 2U + 1U;
    uint32_t offset = esp_random() % spread;

    if (offset < jitter_seconds) {
        uint32_t subtract = jitter_seconds - offset;
        return base_seconds > subtract ? base_seconds - subtract : 1U;
    }
    return base_seconds + (offset - jitter_seconds);
}

static void time_sync_delay_seconds(uint32_t seconds)
{
    uint32_t remaining_ms = seconds * 1000U;

    while (remaining_ms > 0) {
        APP_STACK_MONITOR_CHECK(TAG, "time_sync", 30000);

        if (ulTaskNotifyTake(pdTRUE, 0) > 0) {
            return;
        }

        uint32_t chunk_ms = remaining_ms > 60000U ? 60000U : remaining_ms;
        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(chunk_ms)) > 0) {
            return;
        }
        remaining_ms -= chunk_ms;
    }
}

static esp_err_t time_sync_once(void)
{
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_TIME_SYNC_SERVER);

    ESP_RETURN_ON_ERROR(esp_netif_sntp_init(&config), TAG, "SNTP init failed");
    esp_err_t err = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(CONFIG_TIME_SYNC_WAIT_TIMEOUT_MS));
    esp_netif_sntp_deinit();

    if (err != ESP_OK) {
        return err;
    }

    time_t now = 0;
    struct tm local_time = { 0 };
    char time_text[32] = { 0 };

    time(&now);
    localtime_r(&now, &local_time);
    strftime(time_text, sizeof(time_text), "%Y-%m-%d %H:%M:%S", &local_time);
    ESP_LOGI(TAG, "time synchronized via %s: %s", CONFIG_TIME_SYNC_SERVER, time_text);
    return ESP_OK;
}

static esp_err_t time_sync_try_when_wifi_ready(void)
{
    time_sync_set_state(TIME_SYNC_STATE_WAITING_WIFI);

    if (!wifi_manager_can_request_connection()) {
        ESP_LOGI(TAG, "time sync skipped: Wi-Fi is not available for connection requests");
        time_sync_set_state(TIME_SYNC_STATE_IDLE);
        return ESP_ERR_INVALID_STATE;
    }

    bool suppress_setup = time_sync_has_success();
    esp_err_t wifi_err = suppress_setup ?
                         wifi_manager_request_connection_without_setup(pdMS_TO_TICKS(TIME_SYNC_WIFI_WAIT_MS)) :
                         wifi_manager_request_connection(pdMS_TO_TICKS(TIME_SYNC_WIFI_WAIT_MS));
    if (wifi_err != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi not ready for time sync: %s", esp_err_to_name(wifi_err));
        time_sync_record_attempt_status(wifi_err);
        time_sync_set_state(TIME_SYNC_STATE_IDLE);
        return wifi_err;
    }

    time_sync_set_state(TIME_SYNC_STATE_SYNCING);
    esp_err_t sync_err = time_sync_once();
    time_sync_record_attempt_status(sync_err);
    if (sync_err != ESP_OK) {
        ESP_LOGW(TAG, "time sync failed: %s", esp_err_to_name(sync_err));
    }
    esp_err_t disable_err = wifi_manager_disable();
    if (disable_err != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi disable after time sync returned %s", esp_err_to_name(disable_err));
    }
    time_sync_set_state(TIME_SYNC_STATE_IDLE);
    return sync_err;
}

static void time_sync_retry_after_failure(void)
{
    for (uint32_t attempt = 1; attempt <= CONFIG_TIME_SYNC_RETRY_ATTEMPTS; ++attempt) {
        ESP_LOGI(TAG,
                 "retry time sync in %u seconds (attempt %u/%u)",
                 (unsigned)CONFIG_TIME_SYNC_RETRY_DELAY_SECONDS,
                 (unsigned)attempt,
                 (unsigned)CONFIG_TIME_SYNC_RETRY_ATTEMPTS);
        time_sync_set_state(TIME_SYNC_STATE_RETRY_WAIT);
        time_sync_delay_seconds(CONFIG_TIME_SYNC_RETRY_DELAY_SECONDS);

        if (time_sync_try_when_wifi_ready() == ESP_OK) {
            return;
        }
    }

    ESP_LOGW(TAG, "time sync retry limit reached; returning to normal interval");
}

static void time_sync_task(void *arg)
{
    (void)arg;

    while (true) {
        APP_STACK_MONITOR_CHECK(TAG, "time_sync", 30000);

        esp_err_t sync_err = time_sync_try_when_wifi_ready();
        if (sync_err != ESP_OK && sync_err != ESP_ERR_INVALID_STATE) {
            time_sync_retry_after_failure();
        }

        uint32_t delay_seconds = time_sync_next_delay_seconds();
        ESP_LOGI(TAG,
                 "next time sync in %u seconds (%u min %u sec)",
                 (unsigned)delay_seconds,
                 (unsigned)(delay_seconds / 60U),
                 (unsigned)(delay_seconds % 60U));
        time_sync_set_state(TIME_SYNC_STATE_IDLE);
        time_sync_delay_seconds(delay_seconds);
    }
}

esp_err_t time_sync_start(void)
{
#if CONFIG_TIME_SYNC_ENABLED
    time_sync_apply_timezone_once();

    if (s_time_sync_task_handle != NULL) {
        return ESP_OK;
    }

    time_sync_set_state(TIME_SYNC_STATE_IDLE);
    BaseType_t task_ok = xTaskCreate(time_sync_task,
                                     "time_sync",
                                     CONFIG_TIME_SYNC_TASK_STACK_SIZE,
                                     NULL,
                                     CONFIG_TIME_SYNC_TASK_PRIORITY,
                                     &s_time_sync_task_handle);
    if (task_ok != pdPASS) {
        time_sync_set_state(TIME_SYNC_STATE_STOPPED);
        ESP_RETURN_ON_FALSE(false, ESP_ERR_NO_MEM, TAG, "task create failed");
    }
    return ESP_OK;
#else
    return ESP_OK;
#endif
}

void time_sync_request_soon(void)
{
#if CONFIG_TIME_SYNC_ENABLED
    if (s_time_sync_task_handle != NULL) {
        time_sync_set_state(TIME_SYNC_STATE_WAITING_WIFI);
        xTaskNotifyGive(s_time_sync_task_handle);
    }
#endif
}

time_sync_state_t time_sync_get_state(void)
{
#if CONFIG_TIME_SYNC_ENABLED
    portENTER_CRITICAL(&s_time_sync_status_lock);
    time_sync_state_t state = s_time_sync_state;
    portEXIT_CRITICAL(&s_time_sync_status_lock);
    return state;
#else
    return TIME_SYNC_STATE_STOPPED;
#endif
}

bool time_sync_is_busy(void)
{
    time_sync_state_t state = time_sync_get_state();

    return state == TIME_SYNC_STATE_WAITING_WIFI ||
           state == TIME_SYNC_STATE_SYNCING ||
           state == TIME_SYNC_STATE_RETRY_WAIT;
}

bool time_sync_get_last_success_at(time_t *sync_time)
{
    if (sync_time == NULL) {
        return false;
    }

    portENTER_CRITICAL(&s_time_sync_status_lock);
    bool has_last_success = s_time_sync_has_last_success;
    time_t last_success_at = s_time_sync_last_success_at;
    portEXIT_CRITICAL(&s_time_sync_status_lock);

    if (!has_last_success) {
        return false;
    }

    *sync_time = last_success_at;
    return true;
}

bool time_sync_get_last_attempt_status(esp_err_t *status)
{
    if (status == NULL) {
        return false;
    }

    portENTER_CRITICAL(&s_time_sync_status_lock);
    bool has_last_attempt = s_time_sync_has_last_attempt;
    esp_err_t last_attempt_status = s_time_sync_last_attempt_status;
    portEXIT_CRITICAL(&s_time_sync_status_lock);

    if (!has_last_attempt) {
        return false;
    }

    *status = last_attempt_status;
    return true;
}
