#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_random.h"
#include "nvs.h"
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

#define TIME_SYNC_WIFI_WAIT_POLL_MS 1000U
#define TIME_SYNC_MIN_INTERVAL_MINUTES 1U
#define TIME_SYNC_MAX_INTERVAL_MINUTES 1440U

static const char *TAG = "time_sync";
static const char *NVS_NAMESPACE = "time_sync";
static const char *NVS_INTERVAL_MINUTES_KEY = "interval_min";
static const char *NVS_TIMEZONE_KEY = "timezone";
#define TIME_SYNC_TIMEZONE_MAX_LEN 31U
static TaskHandle_t s_time_sync_task_handle;
static portMUX_TYPE s_time_sync_status_lock = portMUX_INITIALIZER_UNLOCKED;
static time_sync_state_t s_time_sync_state = TIME_SYNC_STATE_STOPPED;
static bool s_time_sync_has_last_success;
static bool s_time_sync_has_last_attempt;
static time_t s_time_sync_last_success_at;
static esp_err_t s_time_sync_last_attempt_status;
static bool s_time_sync_reschedule_requested;
static bool s_time_sync_requested;
static uint16_t s_time_sync_interval_minutes = CONFIG_TIME_SYNC_INTERVAL_MINUTES;
static char s_time_sync_timezone[TIME_SYNC_TIMEZONE_MAX_LEN + 1] = CONFIG_TIME_SYNC_TIMEZONE;

static uint16_t time_sync_clamp_interval_minutes(uint16_t interval_minutes)
{
    if (interval_minutes < TIME_SYNC_MIN_INTERVAL_MINUTES) {
        return TIME_SYNC_MIN_INTERVAL_MINUTES;
    }
    if (interval_minutes > TIME_SYNC_MAX_INTERVAL_MINUTES) {
        return TIME_SYNC_MAX_INTERVAL_MINUTES;
    }
    return interval_minutes;
}

static esp_err_t time_sync_load_saved_interval_minutes(uint16_t *interval_minutes)
{
    nvs_handle_t nvs_handle;
    uint16_t stored_interval_minutes = 0;

    ESP_RETURN_ON_FALSE(interval_minutes != NULL, ESP_ERR_INVALID_ARG, TAG, "interval pointer is null");

    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_get_u16(nvs_handle, NVS_INTERVAL_MINUTES_KEY, &stored_interval_minutes);
    nvs_close(nvs_handle);
    if (err != ESP_OK) {
        return err;
    }

    *interval_minutes = time_sync_clamp_interval_minutes(stored_interval_minutes);
    return ESP_OK;
}

static esp_err_t time_sync_write_interval_minutes(uint16_t interval_minutes)
{
    nvs_handle_t nvs_handle;

    ESP_RETURN_ON_ERROR(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle), TAG, "open NVS failed");
    esp_err_t err = nvs_set_u16(nvs_handle, NVS_INTERVAL_MINUTES_KEY, time_sync_clamp_interval_minutes(interval_minutes));
    if (err == ESP_OK) {
        err = nvs_commit(nvs_handle);
    }
    nvs_close(nvs_handle);
    return err;
}

static esp_err_t time_sync_load_saved_timezone(char *timezone, size_t timezone_size)
{
    nvs_handle_t nvs_handle;
    size_t required_size = timezone_size;

    ESP_RETURN_ON_FALSE(timezone != NULL, ESP_ERR_INVALID_ARG, TAG, "timezone buffer is null");
    ESP_RETURN_ON_FALSE(timezone_size > 0, ESP_ERR_INVALID_ARG, TAG, "timezone buffer is empty");

    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_get_str(nvs_handle, NVS_TIMEZONE_KEY, timezone, &required_size);
    nvs_close(nvs_handle);
    if (err != ESP_OK) {
        return err;
    }

    timezone[timezone_size - 1] = '\0';
    return ESP_OK;
}

static esp_err_t time_sync_write_timezone(const char *timezone)
{
    nvs_handle_t nvs_handle;

    ESP_RETURN_ON_FALSE(timezone != NULL, ESP_ERR_INVALID_ARG, TAG, "timezone is null");

    ESP_RETURN_ON_ERROR(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle), TAG, "open NVS failed");
    esp_err_t err = nvs_set_str(nvs_handle, NVS_TIMEZONE_KEY, timezone);
    if (err == ESP_OK) {
        err = nvs_commit(nvs_handle);
    }
    nvs_close(nvs_handle);
    return err;
}

static uint16_t time_sync_get_interval_minutes_locked(void)
{
    return s_time_sync_interval_minutes;
}

static const char *time_sync_get_timezone_locked(void)
{
    return s_time_sync_timezone;
}

static bool time_sync_take_reschedule_request(void)
{
    portENTER_CRITICAL(&s_time_sync_status_lock);
    bool requested = s_time_sync_reschedule_requested;
    s_time_sync_reschedule_requested = false;
    portEXIT_CRITICAL(&s_time_sync_status_lock);
    return requested;
}

static void time_sync_request_locked(void)
{
    s_time_sync_requested = true;
}

static bool time_sync_has_pending_request(void)
{
    portENTER_CRITICAL(&s_time_sync_status_lock);
    bool requested = s_time_sync_requested;
    portEXIT_CRITICAL(&s_time_sync_status_lock);
    return requested;
}

static void time_sync_clear_request(void)
{
    portENTER_CRITICAL(&s_time_sync_status_lock);
    s_time_sync_requested = false;
    portEXIT_CRITICAL(&s_time_sync_status_lock);
}

static void time_sync_notify_task(void)
{
    if (s_time_sync_task_handle != NULL) {
        xTaskNotifyGive(s_time_sync_task_handle);
    }
}

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

static void time_sync_set_interval_minutes_locked(uint16_t interval_minutes)
{
    s_time_sync_interval_minutes = time_sync_clamp_interval_minutes(interval_minutes);
}

static void time_sync_set_timezone_locked(const char *timezone)
{
    if (timezone == NULL) {
        s_time_sync_timezone[0] = '\0';
        return;
    }

    snprintf(s_time_sync_timezone, sizeof(s_time_sync_timezone), "%s", timezone);
}

static void time_sync_handle_wifi_connected(void *ctx)
{
    (void)ctx;
    if (time_sync_has_pending_request()) {
        time_sync_notify_task();
    }
}

static void time_sync_apply_timezone(void)
{
    char timezone[TIME_SYNC_TIMEZONE_MAX_LEN + 1] = { 0 };

    portENTER_CRITICAL(&s_time_sync_status_lock);
    snprintf(timezone, sizeof(timezone), "%s", time_sync_get_timezone_locked());
    portEXIT_CRITICAL(&s_time_sync_status_lock);

    if (timezone[0] != '\0') {
        setenv("TZ", timezone, 1);
    } else {
        unsetenv("TZ");
    }
    tzset();
}

static uint32_t time_sync_next_delay_seconds(void)
{
    uint16_t interval_minutes = 0;
    portENTER_CRITICAL(&s_time_sync_status_lock);
    interval_minutes = time_sync_get_interval_minutes_locked();
    portEXIT_CRITICAL(&s_time_sync_status_lock);

    const uint32_t base_seconds = (uint32_t)interval_minutes * 60U;
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

static bool time_sync_delay_seconds(uint32_t seconds)
{
    uint32_t remaining_ms = seconds * 1000U;

    while (remaining_ms > 0) {
        APP_STACK_MONITOR_CHECK(TAG, "time_sync", 30000);

        if (ulTaskNotifyTake(pdTRUE, 0) > 0) {
            return true;
        }

        uint32_t chunk_ms = remaining_ms > 60000U ? 60000U : remaining_ms;
        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(chunk_ms)) > 0) {
            return true;
        }
        remaining_ms -= chunk_ms;
    }

    return false;
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

static bool time_sync_wifi_state_is_terminal(wifi_manager_state_t state, bool time_sync_user_active)
{
    return state == WIFI_MANAGER_STATE_STOPPED ||
           state == WIFI_MANAGER_STATE_FAILED ||
           state == WIFI_MANAGER_STATE_SETUP_REQUIRED ||
           (state == WIFI_MANAGER_STATE_OFF && !time_sync_user_active);
}

static void time_sync_release_wifi_user(void)
{
    if ((wifi_manager_get_active_users() & WIFI_MANAGER_USER_TIME_SYNC) == 0) {
        return;
    }

    esp_err_t release_err = wifi_manager_release(WIFI_MANAGER_USER_TIME_SYNC);
    if (release_err != ESP_OK) {
        ESP_LOGW(TAG, "time sync release failed: %s", esp_err_to_name(release_err));
    }
}

static esp_err_t time_sync_sync_with_connected_wifi(void)
{
    esp_err_t sync_err = ESP_FAIL;

    for (uint32_t attempt = 0; attempt <= CONFIG_TIME_SYNC_RETRY_ATTEMPTS; ++attempt) {
        if (attempt > 0) {
            ESP_LOGI(TAG,
                     "retry time sync in %u seconds (attempt %u/%u)",
                     (unsigned)CONFIG_TIME_SYNC_RETRY_DELAY_SECONDS,
                     (unsigned)attempt,
                     (unsigned)CONFIG_TIME_SYNC_RETRY_ATTEMPTS);
            time_sync_set_state(TIME_SYNC_STATE_RETRY_WAIT);
            if (time_sync_delay_seconds(CONFIG_TIME_SYNC_RETRY_DELAY_SECONDS) &&
                time_sync_take_reschedule_request()) {
                ESP_LOGI(TAG, "time sync interval updated during retry wait");
                return sync_err;
            }
        }

        time_sync_set_state(TIME_SYNC_STATE_SYNCING);
        sync_err = time_sync_once();
        time_sync_record_attempt_status(sync_err);
        if (sync_err == ESP_OK) {
            return ESP_OK;
        }
        ESP_LOGW(TAG, "time sync failed: %s", esp_err_to_name(sync_err));
    }

    ESP_LOGW(TAG, "time sync retry limit reached; returning to normal interval");
    return sync_err;
}

static void time_sync_task(void *arg)
{
    (void)arg;

    while (true) {
        APP_STACK_MONITOR_CHECK(TAG, "time_sync", 30000);

        if (!time_sync_has_pending_request()) {
            if (time_sync_take_reschedule_request()) {
                ESP_LOGI(TAG, "time sync interval updated; rescheduling next sync");
                continue;
            }

            uint32_t delay_seconds = time_sync_next_delay_seconds();
            ESP_LOGI(TAG,
                     "next time sync in %u seconds (%u min %u sec)",
                     (unsigned)delay_seconds,
                     (unsigned)(delay_seconds / 60U),
                     (unsigned)(delay_seconds % 60U));
            time_sync_set_state(TIME_SYNC_STATE_IDLE);
            if (time_sync_delay_seconds(delay_seconds)) {
                if (time_sync_take_reschedule_request()) {
                    ESP_LOGI(TAG, "time sync interval updated; rescheduling next sync");
                    continue;
                }
                if (time_sync_has_pending_request()) {
                    continue;
                }
            }

            portENTER_CRITICAL(&s_time_sync_status_lock);
            time_sync_request_locked();
            portEXIT_CRITICAL(&s_time_sync_status_lock);
            esp_err_t acquire_err = wifi_manager_acquire(WIFI_MANAGER_USER_TIME_SYNC);
            if (acquire_err != ESP_OK) {
                ESP_LOGW(TAG, "time sync acquire failed: %s", esp_err_to_name(acquire_err));
            }
            ESP_LOGI(TAG, "time sync due; waiting for Wi-Fi connection");
            continue;
        }

        wifi_manager_state_t wifi_state = WIFI_MANAGER_STATE_STOPPED;
        if (wifi_manager_get_state(&wifi_state) != ESP_OK) {
            ESP_LOGW(TAG, "time sync cannot read Wi-Fi state");
            time_sync_record_attempt_status(ESP_ERR_INVALID_STATE);
            time_sync_clear_request();
            time_sync_release_wifi_user();
            time_sync_set_state(TIME_SYNC_STATE_IDLE);
            continue;
        }

        if (wifi_state != WIFI_MANAGER_STATE_CONNECTED) {
            bool time_sync_user_active =
                (wifi_manager_get_active_users() & WIFI_MANAGER_USER_TIME_SYNC) != 0;
            if (time_sync_wifi_state_is_terminal(wifi_state, time_sync_user_active)) {
                ESP_LOGW(TAG, "time sync Wi-Fi unavailable: state=%d", (int)wifi_state);
                time_sync_record_attempt_status(ESP_FAIL);
                time_sync_clear_request();
                time_sync_release_wifi_user();
                time_sync_set_state(TIME_SYNC_STATE_IDLE);
                continue;
            }

            time_sync_set_state(TIME_SYNC_STATE_WAITING_WIFI);
            (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(TIME_SYNC_WIFI_WAIT_POLL_MS));
            continue;
        }

        (void)time_sync_sync_with_connected_wifi();
        time_sync_clear_request();
        time_sync_release_wifi_user();
        time_sync_set_state(TIME_SYNC_STATE_IDLE);
    }
}

esp_err_t time_sync_start(void)
{
#if CONFIG_TIME_SYNC_ENABLED
    uint16_t saved_interval_minutes = 0;
    if (time_sync_load_saved_interval_minutes(&saved_interval_minutes) == ESP_OK) {
        portENTER_CRITICAL(&s_time_sync_status_lock);
        time_sync_set_interval_minutes_locked(saved_interval_minutes);
        portEXIT_CRITICAL(&s_time_sync_status_lock);
    }

    char saved_timezone[TIME_SYNC_TIMEZONE_MAX_LEN + 1] = { 0 };
    if (time_sync_load_saved_timezone(saved_timezone, sizeof(saved_timezone)) == ESP_OK) {
        portENTER_CRITICAL(&s_time_sync_status_lock);
        time_sync_set_timezone_locked(saved_timezone);
        portEXIT_CRITICAL(&s_time_sync_status_lock);
    }

    time_sync_apply_timezone();

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

    portENTER_CRITICAL(&s_time_sync_status_lock);
    time_sync_request_locked();
    portEXIT_CRITICAL(&s_time_sync_status_lock);
    time_sync_set_state(TIME_SYNC_STATE_WAITING_WIFI);
    ESP_RETURN_ON_ERROR(wifi_manager_acquire(WIFI_MANAGER_USER_TIME_SYNC), TAG, "initial time sync acquire failed");

    wifi_manager_set_connected_callback(time_sync_handle_wifi_connected, NULL);
    time_sync_notify_task();
    return ESP_OK;
#else
    return ESP_OK;
#endif
}

void time_sync_request_soon(void)
{
#if CONFIG_TIME_SYNC_ENABLED
    portENTER_CRITICAL(&s_time_sync_status_lock);
    time_sync_request_locked();
    portEXIT_CRITICAL(&s_time_sync_status_lock);
    time_sync_set_state(TIME_SYNC_STATE_WAITING_WIFI);
    esp_err_t acquire_err = wifi_manager_acquire(WIFI_MANAGER_USER_TIME_SYNC);
    if (acquire_err != ESP_OK) {
        ESP_LOGW(TAG, "time sync acquire failed: %s", esp_err_to_name(acquire_err));
    }
    time_sync_notify_task();
#endif
}

void time_sync_request_soon_and_release_wifi(void)
{
    time_sync_request_soon();
}

esp_err_t time_sync_set_interval_minutes(uint16_t interval_minutes)
{
    uint16_t clamped_interval_minutes = time_sync_clamp_interval_minutes(interval_minutes);

    portENTER_CRITICAL(&s_time_sync_status_lock);
    time_sync_set_interval_minutes_locked(clamped_interval_minutes);
    s_time_sync_reschedule_requested = true;
    portEXIT_CRITICAL(&s_time_sync_status_lock);

#if CONFIG_TIME_SYNC_ENABLED
    if (s_time_sync_task_handle != NULL) {
        xTaskNotifyGive(s_time_sync_task_handle);
    }
#endif

    return ESP_OK;
}

esp_err_t time_sync_save_interval_minutes(void)
{
    return time_sync_write_interval_minutes(time_sync_get_interval_minutes());
}

uint16_t time_sync_get_interval_minutes(void)
{
    portENTER_CRITICAL(&s_time_sync_status_lock);
    uint16_t interval_minutes = time_sync_get_interval_minutes_locked();
    portEXIT_CRITICAL(&s_time_sync_status_lock);
    return interval_minutes;
}

esp_err_t time_sync_set_timezone(const char *timezone)
{
    ESP_RETURN_ON_FALSE(timezone != NULL, ESP_ERR_INVALID_ARG, TAG, "timezone is null");
    ESP_RETURN_ON_FALSE(strlen(timezone) <= TIME_SYNC_TIMEZONE_MAX_LEN,
                        ESP_ERR_INVALID_ARG,
                        TAG,
                        "timezone is too long");

    portENTER_CRITICAL(&s_time_sync_status_lock);
    time_sync_set_timezone_locked(timezone);
    portEXIT_CRITICAL(&s_time_sync_status_lock);

    time_sync_apply_timezone();
    return ESP_OK;
}

esp_err_t time_sync_save_timezone(void)
{
    return time_sync_write_timezone(time_sync_get_timezone());
}

const char *time_sync_get_timezone(void)
{
    return s_time_sync_timezone;
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
