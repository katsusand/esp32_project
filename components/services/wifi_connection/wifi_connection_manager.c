#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "app_stack_monitor.h"
#include "error_log_store.h"
#include "esp32_wifi_sta.h"
#include "wifi_connection.h"
#include "wifi_connection.h"

#ifndef CONFIG_ESP32_WIFI_STA_CONNECT_TIMEOUT_MS
#define CONFIG_ESP32_WIFI_STA_CONNECT_TIMEOUT_MS 15000
#endif
#ifndef CONFIG_WIFI_CONNECTION_TASK_STACK_SIZE
#define CONFIG_WIFI_CONNECTION_TASK_STACK_SIZE 8192
#endif
#ifndef CONFIG_WIFI_CONNECTION_TASK_PRIORITY
#define CONFIG_WIFI_CONNECTION_TASK_PRIORITY 8
#endif
#ifndef CONFIG_WIFI_CONNECTION_MONITOR_INTERVAL_MS
#define CONFIG_WIFI_CONNECTION_MONITOR_INTERVAL_MS 2000
#endif
#ifndef CONFIG_WIFI_CONNECTION_STACK_LOG_INTERVAL_MS
#define CONFIG_WIFI_CONNECTION_STACK_LOG_INTERVAL_MS 30000
#endif
#ifndef CONFIG_WIFI_CONNECTION_SCAN_RETRY_ATTEMPTS
#define CONFIG_WIFI_CONNECTION_SCAN_RETRY_ATTEMPTS 5
#endif
#ifndef CONFIG_WIFI_CONNECTION_SCAN_RETRY_DELAY_MS
#define CONFIG_WIFI_CONNECTION_SCAN_RETRY_DELAY_MS 3000
#endif
#ifndef CONFIG_WIFI_CONNECTION_CONNECTED_WARNING_SECONDS
#define CONFIG_WIFI_CONNECTION_CONNECTED_WARNING_SECONDS 180
#endif

#define WIFI_CONNECTION_CONNECTED_BIT   BIT0
#define WIFI_CONNECTION_STOPPED_BIT     BIT1
#define WIFI_CONNECTION_ENABLE_REQ_BIT  BIT2
#define WIFI_CONNECTION_DISABLE_REQ_BIT BIT3
#define WIFI_CONNECTION_OFF_BIT         BIT4
#define WIFI_CONNECTION_NO_SETUP_BIT    BIT5

#define TAG "wifi_connection"

typedef struct {
    TaskHandle_t task_handle;
    EventGroupHandle_t event_group;
    volatile wifi_connection_state_t state;
    volatile esp32_wifi_sta_failure_reason_t last_failure_reason;
    volatile uint32_t active_users;
    volatile wifi_connection_user_t last_user;
    volatile int64_t connected_since_us;
    volatile uint32_t connected_duration_high_water_seconds;
    volatile bool setup_requested_explicitly;
    volatile bool setup_starting;
    volatile bool setup_requested_on_start;
    volatile bool ssid_configured;
    volatile bool last_connection_succeeded;
    volatile bool connection_result_initialized;
    wifi_connection_connected_callback_t connected_callback;
    void *connected_callback_ctx;
    wifi_connection_connectivity_callback_t connectivity_callback;
    void *connectivity_callback_ctx;
} wifi_connection_context_t;

static wifi_connection_context_t s_wifi_connection = {
    .task_handle = NULL,
    .event_group = NULL,
    .state = WIFI_CONNECTION_STATE_STOPPED,
};
static portMUX_TYPE s_wifi_connection_lock = portMUX_INITIALIZER_UNLOCKED;

static uint32_t wifi_connection_compute_connected_duration_seconds(void)
{
    if (s_wifi_connection.state != WIFI_CONNECTION_STATE_CONNECTED ||
        s_wifi_connection.connected_since_us <= 0) {
        return 0;
    }

    int64_t elapsed_us = esp_timer_get_time() - s_wifi_connection.connected_since_us;
    if (elapsed_us <= 0) {
        return 0;
    }

    return (uint32_t)(elapsed_us / 1000000LL);
}

static void wifi_connection_set_state(wifi_connection_state_t state)
{
    if (s_wifi_connection.state == WIFI_CONNECTION_STATE_CONNECTED &&
        state != WIFI_CONNECTION_STATE_CONNECTED) {
        uint32_t connected_seconds = wifi_connection_compute_connected_duration_seconds();
        if (connected_seconds > s_wifi_connection.connected_duration_high_water_seconds) {
            s_wifi_connection.connected_duration_high_water_seconds = connected_seconds;
        }
    }

    if (state == WIFI_CONNECTION_STATE_CONNECTED &&
        s_wifi_connection.state != WIFI_CONNECTION_STATE_CONNECTED) {
        s_wifi_connection.connected_since_us = esp_timer_get_time();
    } else if (state != WIFI_CONNECTION_STATE_CONNECTED) {
        s_wifi_connection.connected_since_us = 0;
    }
    s_wifi_connection.state = state;
}

static bool wifi_connection_has_active_users(void)
{
    portENTER_CRITICAL(&s_wifi_connection_lock);
    bool has_active_users = s_wifi_connection.active_users != 0;
    portEXIT_CRITICAL(&s_wifi_connection_lock);
    return has_active_users;
}

static void wifi_connection_notify_connected(void)
{
    wifi_connection_connected_callback_t callback = s_wifi_connection.connected_callback;

    if (callback != NULL) {
        callback(s_wifi_connection.connected_callback_ctx);
    }
}

static bool wifi_connection_state_can_disable(wifi_connection_state_t state)
{
    return state == WIFI_CONNECTION_STATE_CONNECTED ||
           state == WIFI_CONNECTION_STATE_FAILED ||
           state == WIFI_CONNECTION_STATE_OFF;
}

static bool wifi_connection_state_is_setup(wifi_connection_state_t state)
{
    return state == WIFI_CONNECTION_STATE_SETUP_REQUIRED ||
           state == WIFI_CONNECTION_STATE_SETUP_RUNNING;
}

static esp_err_t wifi_connection_request_connection_internal(TickType_t wait_ticks, bool allow_setup);
static esp_err_t wifi_connection_request_connection_async_internal(bool allow_setup);

static void wifi_connection_notify_connectivity(void)
{
    wifi_connection_connectivity_status_t status = {
        .credentials_configured = s_wifi_connection.ssid_configured,
        .last_connection_succeeded = s_wifi_connection.last_connection_succeeded,
    };
    wifi_connection_connectivity_callback_t callback = s_wifi_connection.connectivity_callback;

    if (callback != NULL) {
        callback(&status, s_wifi_connection.connectivity_callback_ctx);
    }
}

static void wifi_connection_set_connection_result(bool ssid_configured, bool connected)
{
    if (s_wifi_connection.connection_result_initialized &&
        s_wifi_connection.ssid_configured == ssid_configured &&
        s_wifi_connection.last_connection_succeeded == connected) {
        return;
    }

    s_wifi_connection.ssid_configured = ssid_configured;
    s_wifi_connection.last_connection_succeeded = connected;
    s_wifi_connection.connection_result_initialized = true;
    wifi_connection_notify_connectivity();
}

static void wifi_connection_set_setup_required(bool explicit_request)
{
    s_wifi_connection.setup_requested_explicitly = explicit_request;
    s_wifi_connection.last_failure_reason = ESP32_WIFI_STA_FAILURE_NONE;
    wifi_connection_set_state(WIFI_CONNECTION_STATE_SETUP_REQUIRED);
    xEventGroupClearBits(s_wifi_connection.event_group, WIFI_CONNECTION_CONNECTED_BIT | WIFI_CONNECTION_OFF_BIT);
    ESP_LOGW(TAG, "Wi-Fi setup required");
}

static void wifi_connection_log_stack_usage(void)
{
    APP_STACK_MONITOR_CHECK(TAG, "wifi_connection", CONFIG_WIFI_CONNECTION_STACK_LOG_INTERVAL_MS);
}

static esp_err_t wifi_connection_try_connect_once(esp32_wifi_sta_failure_reason_t *failure_reason)
{
    esp32_wifi_sta_failure_reason_t local_failure_reason = ESP32_WIFI_STA_FAILURE_NONE;

    s_wifi_connection.last_failure_reason = ESP32_WIFI_STA_FAILURE_NONE;
    xEventGroupClearBits(s_wifi_connection.event_group, WIFI_CONNECTION_CONNECTED_BIT | WIFI_CONNECTION_OFF_BIT);

    esp_err_t err = wifi_connection_connect_configured(
        pdMS_TO_TICKS(CONFIG_ESP32_WIFI_STA_CONNECT_TIMEOUT_MS),
        &local_failure_reason
    );
    if (failure_reason != NULL) {
        *failure_reason = local_failure_reason;
    }
    return err;
}

static esp_err_t wifi_connection_try_connect(wifi_connection_state_t state)
{
    esp32_wifi_sta_failure_reason_t failure_reason = ESP32_WIFI_STA_FAILURE_NONE;
    esp_err_t err = ESP_FAIL;

    wifi_connection_set_state(state);

    for (uint32_t attempt = 1; attempt <= CONFIG_WIFI_CONNECTION_SCAN_RETRY_ATTEMPTS; ++attempt) {
        err = wifi_connection_try_connect_once(&failure_reason);
        if (err == ESP_OK) {
            break;
        }

        if (failure_reason != ESP32_WIFI_STA_FAILURE_NO_AP_IN_RANGE ||
            attempt >= CONFIG_WIFI_CONNECTION_SCAN_RETRY_ATTEMPTS) {
            break;
        }

        ESP_LOGW(TAG,
                 "saved SSID not visible; rescanning in %u ms (%u/%u)",
                 (unsigned)CONFIG_WIFI_CONNECTION_SCAN_RETRY_DELAY_MS,
                 (unsigned)attempt,
                 (unsigned)CONFIG_WIFI_CONNECTION_SCAN_RETRY_ATTEMPTS);
        vTaskDelay(pdMS_TO_TICKS(CONFIG_WIFI_CONNECTION_SCAN_RETRY_DELAY_MS));
    }

    if (err == ESP_OK) {
        wifi_connection_set_connection_result(true, true);
        s_wifi_connection.last_failure_reason = ESP32_WIFI_STA_FAILURE_NONE;
        wifi_connection_set_state(WIFI_CONNECTION_STATE_CONNECTED);
        xEventGroupSetBits(s_wifi_connection.event_group, WIFI_CONNECTION_CONNECTED_BIT);
        ESP_LOGI(TAG, "Wi-Fi connection connected");
        wifi_connection_notify_connected();
    } else {
        s_wifi_connection.last_failure_reason = failure_reason;
        wifi_connection_set_connection_result(esp32_wifi_sta_has_configured_ssid(), false);
        wifi_connection_set_state(WIFI_CONNECTION_STATE_FAILED);
        xEventGroupSetBits(s_wifi_connection.event_group, WIFI_CONNECTION_OFF_BIT);
        ESP_LOGW(TAG,
                 "Wi-Fi connection connect failed: %s reason=%d",
                 esp_err_to_name(err),
                 (int)failure_reason);
        (void)error_log_store_append_esp_err(TAG, "Wi-Fi connection connect failed", err);
    }
    return err;
}

static esp_err_t wifi_connection_disable_sta(void)
{
    esp32_wifi_sta_status_t status = { 0 };
    esp_err_t status_err = esp32_wifi_sta_get_status(&status);

    if (status_err == ESP_ERR_INVALID_STATE) {
        xEventGroupClearBits(s_wifi_connection.event_group, WIFI_CONNECTION_CONNECTED_BIT);
        s_wifi_connection.last_failure_reason = ESP32_WIFI_STA_FAILURE_NONE;
        wifi_connection_set_state(WIFI_CONNECTION_STATE_OFF);
        xEventGroupSetBits(s_wifi_connection.event_group, WIFI_CONNECTION_OFF_BIT);
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(status_err, TAG, "Wi-Fi STA status failed");

    if (status.state != ESP32_WIFI_STA_STATE_STOPPED) {
        ESP_RETURN_ON_ERROR(esp32_wifi_sta_stop(), TAG, "Wi-Fi STA stop failed");
    }

    xEventGroupClearBits(s_wifi_connection.event_group, WIFI_CONNECTION_CONNECTED_BIT);
    s_wifi_connection.last_failure_reason = ESP32_WIFI_STA_FAILURE_NONE;
    wifi_connection_set_state(WIFI_CONNECTION_STATE_OFF);
    xEventGroupSetBits(s_wifi_connection.event_group, WIFI_CONNECTION_OFF_BIT);
    ESP_LOGI(TAG, "Wi-Fi connection disabled");
    return ESP_OK;
}

static bool wifi_connection_get_sta_status(esp32_wifi_sta_status_t *status)
{
    if (esp32_wifi_sta_get_status(status) != ESP_OK) {
        return false;
    }
    return true;
}

static bool wifi_connection_handle_connected_monitor(void)
{
    EventBits_t bits = xEventGroupWaitBits(s_wifi_connection.event_group,
                                           WIFI_CONNECTION_ENABLE_REQ_BIT |
                                           WIFI_CONNECTION_DISABLE_REQ_BIT |
                                           WIFI_CONNECTION_NO_SETUP_BIT,
                                           pdTRUE,
                                           pdFALSE,
                                           pdMS_TO_TICKS(CONFIG_WIFI_CONNECTION_MONITOR_INTERVAL_MS));
    if (s_wifi_connection.setup_starting || wifi_connection_state_is_setup(s_wifi_connection.state)) {
        return false;
    }
    if ((bits & WIFI_CONNECTION_DISABLE_REQ_BIT) != 0) {
        if (!wifi_connection_has_active_users()) {
            (void)wifi_connection_disable_sta();
            return false;
        }
        ESP_LOGD(TAG, "ignoring stale disable request while Wi-Fi users are active");
    }
    if ((bits & WIFI_CONNECTION_ENABLE_REQ_BIT) != 0) {
        xEventGroupSetBits(s_wifi_connection.event_group, WIFI_CONNECTION_CONNECTED_BIT);
    }

    esp32_wifi_sta_status_t status = { 0 };
    if (wifi_connection_get_sta_status(&status) &&
        status.state == ESP32_WIFI_STA_STATE_CONNECTED &&
        status.has_ip) {
        wifi_connection_set_connection_result(true, true);
        wifi_connection_set_state(WIFI_CONNECTION_STATE_CONNECTED);
        return true;
    }

    if (wifi_connection_get_sta_status(&status) &&
        status.state == ESP32_WIFI_STA_STATE_CONNECTING) {
        wifi_connection_set_state(WIFI_CONNECTION_STATE_RECONNECTING);
        return true;
    }

    ESP_LOGW(TAG, "Wi-Fi connection lost; reconnecting");
    if (wifi_connection_try_connect(WIFI_CONNECTION_STATE_RECONNECTING) == ESP_OK) {
        return true;
    }

    return false;
}

static void wifi_connection_task(void *arg)
{
    (void)arg;

    wifi_connection_set_state(WIFI_CONNECTION_STATE_INIT);
    s_wifi_connection.last_failure_reason = ESP32_WIFI_STA_FAILURE_NONE;
    wifi_connection_set_connection_result(esp32_wifi_sta_has_configured_ssid(), false);
    xEventGroupClearBits(s_wifi_connection.event_group,
                         WIFI_CONNECTION_CONNECTED_BIT | WIFI_CONNECTION_STOPPED_BIT |
                         WIFI_CONNECTION_ENABLE_REQ_BIT | WIFI_CONNECTION_DISABLE_REQ_BIT |
                         WIFI_CONNECTION_OFF_BIT | WIFI_CONNECTION_NO_SETUP_BIT);

    bool setup_requested = s_wifi_connection.setup_requested_on_start;
    s_wifi_connection.setup_requested_on_start = false;
    if (!setup_requested && !esp32_wifi_sta_has_configured_ssid()) {
        wifi_connection_set_setup_required(false);
    } else if (!setup_requested) {
        (void)wifi_connection_disable_sta();
        if (wifi_connection_has_active_users()) {
            xEventGroupSetBits(s_wifi_connection.event_group, WIFI_CONNECTION_ENABLE_REQ_BIT);
        }
    }

    while (true) {
        wifi_connection_log_stack_usage();
        if (setup_requested) {
            wifi_connection_set_setup_required(true);
            setup_requested = false;
        } else if (s_wifi_connection.state == WIFI_CONNECTION_STATE_CONNECTED) {
            (void)wifi_connection_handle_connected_monitor();
        } else if (wifi_connection_state_is_setup(s_wifi_connection.state)) {
            xEventGroupClearBits(s_wifi_connection.event_group,
                                 WIFI_CONNECTION_ENABLE_REQ_BIT | WIFI_CONNECTION_NO_SETUP_BIT);
            vTaskDelay(pdMS_TO_TICKS(CONFIG_WIFI_CONNECTION_MONITOR_INTERVAL_MS));
        } else {
            EventBits_t bits = xEventGroupWaitBits(s_wifi_connection.event_group,
                                                   WIFI_CONNECTION_ENABLE_REQ_BIT |
                                                   WIFI_CONNECTION_DISABLE_REQ_BIT |
                                                   WIFI_CONNECTION_NO_SETUP_BIT,
                                                   pdTRUE,
                                                   pdFALSE,
                                                   portMAX_DELAY);
            if (s_wifi_connection.state == WIFI_CONNECTION_STATE_CONNECTED ||
                wifi_connection_state_is_setup(s_wifi_connection.state)) {
                continue;
            }
            if ((bits & WIFI_CONNECTION_DISABLE_REQ_BIT) != 0) {
                if (!wifi_connection_has_active_users()) {
                    (void)wifi_connection_disable_sta();
                } else {
                    ESP_LOGD(TAG, "ignoring stale disable request while Wi-Fi users are active");
                }
            }
            if ((bits & WIFI_CONNECTION_ENABLE_REQ_BIT) != 0 &&
                wifi_connection_try_connect(WIFI_CONNECTION_STATE_CONNECTING) != ESP_OK) {
                if ((bits & WIFI_CONNECTION_NO_SETUP_BIT) != 0) {
                    ESP_LOGW(TAG, "Wi-Fi connect failed; waiting for explicit retry or setup");
                }
            }
        }
    }

    wifi_connection_set_state(WIFI_CONNECTION_STATE_STOPPED);
    xEventGroupSetBits(s_wifi_connection.event_group, WIFI_CONNECTION_STOPPED_BIT);
    s_wifi_connection.task_handle = NULL;
    vTaskDelete(NULL);
}

esp_err_t wifi_connection_start(void)
{
    if (s_wifi_connection.event_group == NULL) {
        s_wifi_connection.event_group = xEventGroupCreate();
        ESP_RETURN_ON_FALSE(s_wifi_connection.event_group != NULL, ESP_ERR_NO_MEM, TAG, "event group alloc failed");
    }
    if (s_wifi_connection.task_handle != NULL) {
        return ESP_OK;
    }

    BaseType_t task_ok = xTaskCreate(wifi_connection_task,
                                     "wifi_connection",
                                     CONFIG_WIFI_CONNECTION_TASK_STACK_SIZE,
                                     NULL,
                                     CONFIG_WIFI_CONNECTION_TASK_PRIORITY,
                                     &s_wifi_connection.task_handle);
    ESP_RETURN_ON_FALSE(task_ok == pdPASS, ESP_ERR_NO_MEM, TAG, "task create failed");
    return ESP_OK;
}

void wifi_connection_request_setup_on_start(void)
{
    s_wifi_connection.setup_requested_on_start = true;
}

void wifi_connection_set_connected_callback(wifi_connection_connected_callback_t callback, void *ctx)
{
    s_wifi_connection.connected_callback = callback;
    s_wifi_connection.connected_callback_ctx = ctx;
}

void wifi_connection_set_connectivity_callback(wifi_connection_connectivity_callback_t callback, void *ctx)
{
    s_wifi_connection.connectivity_callback = callback;
    s_wifi_connection.connectivity_callback_ctx = ctx;
    if (callback != NULL && s_wifi_connection.connection_result_initialized) {
        wifi_connection_notify_connectivity();
    }
}

esp_err_t wifi_connection_get_connectivity_status(wifi_connection_connectivity_status_t *status)
{
    ESP_RETURN_ON_FALSE(status != NULL, ESP_ERR_INVALID_ARG, TAG, "connectivity status is null");

    status->credentials_configured = s_wifi_connection.ssid_configured;
    status->last_connection_succeeded = s_wifi_connection.last_connection_succeeded;
    return ESP_OK;
}

esp_err_t wifi_connection_acquire(wifi_connection_user_t user)
{
    ESP_RETURN_ON_FALSE(s_wifi_connection.event_group != NULL, ESP_ERR_INVALID_STATE, TAG, "manager not started");
    ESP_RETURN_ON_FALSE(user != 0, ESP_ERR_INVALID_ARG, TAG, "user is zero");

    portENTER_CRITICAL(&s_wifi_connection_lock);
    s_wifi_connection.last_user = user;
    s_wifi_connection.active_users |= (uint32_t)user;
    wifi_connection_state_t state = s_wifi_connection.state;
    portEXIT_CRITICAL(&s_wifi_connection_lock);

    if (!wifi_connection_state_is_setup(state) &&
        state != WIFI_CONNECTION_STATE_CONNECTED) {
        xEventGroupClearBits(s_wifi_connection.event_group, WIFI_CONNECTION_OFF_BIT);
        xEventGroupSetBits(s_wifi_connection.event_group, WIFI_CONNECTION_ENABLE_REQ_BIT);
    }

    return ESP_OK;
}

esp_err_t wifi_connection_release(wifi_connection_user_t user)
{
    ESP_RETURN_ON_FALSE(s_wifi_connection.event_group != NULL, ESP_ERR_INVALID_STATE, TAG, "manager not started");
    ESP_RETURN_ON_FALSE(user != 0, ESP_ERR_INVALID_ARG, TAG, "user is zero");

    portENTER_CRITICAL(&s_wifi_connection_lock);
    if ((s_wifi_connection.active_users & (uint32_t)user) == 0) {
        portEXIT_CRITICAL(&s_wifi_connection_lock);
        ESP_LOGW(TAG, "Wi-Fi user release without acquire: 0x%02x", (unsigned)user);
        return ESP_ERR_INVALID_STATE;
    }

    s_wifi_connection.active_users &= ~((uint32_t)user);
    bool has_active_users = s_wifi_connection.active_users != 0;
    wifi_connection_state_t state = s_wifi_connection.state;
    portEXIT_CRITICAL(&s_wifi_connection_lock);

    if (!has_active_users &&
        !wifi_connection_state_is_setup(state)) {
        xEventGroupSetBits(s_wifi_connection.event_group, WIFI_CONNECTION_DISABLE_REQ_BIT);
    }

    return ESP_OK;
}

esp_err_t wifi_connection_enable(void)
{
    ESP_RETURN_ON_FALSE(s_wifi_connection.event_group != NULL, ESP_ERR_INVALID_STATE, TAG, "manager not started");

    wifi_connection_state_t state = s_wifi_connection.state;
    if (state == WIFI_CONNECTION_STATE_CONNECTED) {
        xEventGroupSetBits(s_wifi_connection.event_group, WIFI_CONNECTION_CONNECTED_BIT);
        return ESP_OK;
    }
    if (state != WIFI_CONNECTION_STATE_OFF &&
        state != WIFI_CONNECTION_STATE_INIT &&
        state != WIFI_CONNECTION_STATE_FAILED) {
        return ESP_OK;
    }

    xEventGroupClearBits(s_wifi_connection.event_group, WIFI_CONNECTION_OFF_BIT);
    xEventGroupSetBits(s_wifi_connection.event_group, WIFI_CONNECTION_ENABLE_REQ_BIT);
    return ESP_OK;
}

esp_err_t wifi_connection_disable(void)
{
    ESP_RETURN_ON_FALSE(s_wifi_connection.event_group != NULL, ESP_ERR_INVALID_STATE, TAG, "manager not started");

    wifi_connection_state_t state = s_wifi_connection.state;
    ESP_RETURN_ON_FALSE(wifi_connection_state_can_disable(state),
                        ESP_ERR_INVALID_STATE,
                        TAG,
                        "Wi-Fi connection is busy");

    if (state == WIFI_CONNECTION_STATE_OFF) {
        return ESP_OK;
    }

    xEventGroupClearBits(s_wifi_connection.event_group, WIFI_CONNECTION_OFF_BIT);
    xEventGroupSetBits(s_wifi_connection.event_group, WIFI_CONNECTION_DISABLE_REQ_BIT);

    EventBits_t bits = xEventGroupWaitBits(s_wifi_connection.event_group,
                                           WIFI_CONNECTION_OFF_BIT | WIFI_CONNECTION_STOPPED_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           pdMS_TO_TICKS(CONFIG_WIFI_CONNECTION_MONITOR_INTERVAL_MS + 5000));
    if ((bits & WIFI_CONNECTION_OFF_BIT) != 0) {
        return ESP_OK;
    }
    if ((bits & WIFI_CONNECTION_STOPPED_BIT) != 0) {
        return ESP_FAIL;
    }
    return ESP_ERR_TIMEOUT;
}

esp_err_t wifi_connection_request_connection(TickType_t wait_ticks)
{
    return wifi_connection_request_connection_internal(wait_ticks, true);
}

esp_err_t wifi_connection_request_connection_without_setup(TickType_t wait_ticks)
{
    return wifi_connection_request_connection_internal(wait_ticks, false);
}

esp_err_t wifi_connection_request_connection_without_setup_async(void)
{
    return wifi_connection_request_connection_async_internal(false);
}

esp_err_t wifi_connection_begin_setup(void)
{
    ESP_RETURN_ON_FALSE(s_wifi_connection.event_group != NULL, ESP_ERR_INVALID_STATE, TAG, "manager not started");

    xEventGroupClearBits(s_wifi_connection.event_group,
                         WIFI_CONNECTION_CONNECTED_BIT |
                         WIFI_CONNECTION_ENABLE_REQ_BIT |
                         WIFI_CONNECTION_DISABLE_REQ_BIT |
                         WIFI_CONNECTION_OFF_BIT |
                         WIFI_CONNECTION_NO_SETUP_BIT);

    /* Gate the monitor before stopping STA so an in-flight monitor pass cannot reconnect it. */
    s_wifi_connection.setup_starting = true;
    esp_err_t err = wifi_connection_disable_sta();
    if (err != ESP_OK) {
        s_wifi_connection.setup_starting = false;
        ESP_LOGE(TAG, "stop Wi-Fi before setup failed: %s", esp_err_to_name(err));
        return err;
    }

    wifi_connection_set_state(WIFI_CONNECTION_STATE_SETUP_RUNNING);
    s_wifi_connection.setup_requested_explicitly = false;
    s_wifi_connection.last_failure_reason = ESP32_WIFI_STA_FAILURE_NONE;
    s_wifi_connection.setup_starting = false;
    xEventGroupClearBits(s_wifi_connection.event_group, WIFI_CONNECTION_OFF_BIT);
    ESP_LOGI(TAG, "Wi-Fi setup started");
    return ESP_OK;
}

esp_err_t wifi_connection_complete_setup(bool connected)
{
    ESP_RETURN_ON_FALSE(s_wifi_connection.event_group != NULL, ESP_ERR_INVALID_STATE, TAG, "manager not started");

    if (connected) {
        wifi_connection_set_connection_result(true, true);
        s_wifi_connection.last_failure_reason = ESP32_WIFI_STA_FAILURE_NONE;
        wifi_connection_set_state(WIFI_CONNECTION_STATE_CONNECTED);
        xEventGroupClearBits(s_wifi_connection.event_group, WIFI_CONNECTION_OFF_BIT);
        xEventGroupSetBits(s_wifi_connection.event_group,
                           WIFI_CONNECTION_CONNECTED_BIT | WIFI_CONNECTION_ENABLE_REQ_BIT);
        ESP_LOGI(TAG, "Wi-Fi setup completed");
        wifi_connection_notify_connected();
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Wi-Fi setup cancelled");
    return wifi_connection_disable_sta();
}

static esp_err_t wifi_connection_request_connection_internal(TickType_t wait_ticks, bool allow_setup)
{
    ESP_RETURN_ON_FALSE(s_wifi_connection.event_group != NULL, ESP_ERR_INVALID_STATE, TAG, "manager not started");

    wifi_connection_state_t state = s_wifi_connection.state;
    if (state == WIFI_CONNECTION_STATE_CONNECTED) {
        return ESP_OK;
    }
    if (!wifi_connection_can_request_connection()) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_RETURN_ON_ERROR(wifi_connection_request_connection_async_internal(allow_setup),
                        TAG,
                        "Wi-Fi request failed");

    EventBits_t bits = xEventGroupWaitBits(s_wifi_connection.event_group,
                                           WIFI_CONNECTION_CONNECTED_BIT | WIFI_CONNECTION_OFF_BIT | WIFI_CONNECTION_STOPPED_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           wait_ticks);
    if ((bits & WIFI_CONNECTION_CONNECTED_BIT) != 0) {
        return ESP_OK;
    }
    if ((bits & WIFI_CONNECTION_STOPPED_BIT) != 0) {
        return ESP_FAIL;
    }
    if ((bits & WIFI_CONNECTION_OFF_BIT) != 0) {
        return ESP_FAIL;
    }
    return ESP_ERR_TIMEOUT;
}

static esp_err_t wifi_connection_request_connection_async_internal(bool allow_setup)
{
    ESP_RETURN_ON_FALSE(s_wifi_connection.event_group != NULL, ESP_ERR_INVALID_STATE, TAG, "manager not started");
    ESP_RETURN_ON_FALSE(wifi_connection_can_request_connection(), ESP_ERR_INVALID_STATE, TAG, "Wi-Fi cannot connect now");

    xEventGroupClearBits(s_wifi_connection.event_group,
                         WIFI_CONNECTION_CONNECTED_BIT | WIFI_CONNECTION_OFF_BIT | WIFI_CONNECTION_NO_SETUP_BIT);
    xEventGroupSetBits(s_wifi_connection.event_group,
                       WIFI_CONNECTION_ENABLE_REQ_BIT | (allow_setup ? 0 : WIFI_CONNECTION_NO_SETUP_BIT));
    return ESP_OK;
}

esp_err_t wifi_connection_wait_connected(TickType_t wait_ticks)
{
    ESP_RETURN_ON_FALSE(s_wifi_connection.event_group != NULL, ESP_ERR_INVALID_STATE, TAG, "manager not started");
    ESP_RETURN_ON_FALSE(s_wifi_connection.state != WIFI_CONNECTION_STATE_OFF,
                        ESP_ERR_INVALID_STATE,
                        TAG,
                        "Wi-Fi connection is off");

    EventBits_t bits = xEventGroupWaitBits(s_wifi_connection.event_group,
                                           WIFI_CONNECTION_CONNECTED_BIT | WIFI_CONNECTION_STOPPED_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           wait_ticks);
    if ((bits & WIFI_CONNECTION_CONNECTED_BIT) != 0) {
        return ESP_OK;
    }
    if ((bits & WIFI_CONNECTION_STOPPED_BIT) != 0) {
        return ESP_FAIL;
    }
    return ESP_ERR_TIMEOUT;
}

esp_err_t wifi_connection_get_state(wifi_connection_state_t *state)
{
    ESP_RETURN_ON_FALSE(state != NULL, ESP_ERR_INVALID_ARG, TAG, "state is null");

    *state = s_wifi_connection.state;
    return ESP_OK;
}

uint32_t wifi_connection_get_active_users(void)
{
    portENTER_CRITICAL(&s_wifi_connection_lock);
    uint32_t active_users = s_wifi_connection.active_users;
    portEXIT_CRITICAL(&s_wifi_connection_lock);
    return active_users;
}

wifi_connection_user_t wifi_connection_get_last_user(void)
{
    portENTER_CRITICAL(&s_wifi_connection_lock);
    wifi_connection_user_t last_user = s_wifi_connection.last_user;
    portEXIT_CRITICAL(&s_wifi_connection_lock);
    return last_user;
}

uint32_t wifi_connection_get_connected_duration_seconds(void)
{
    return wifi_connection_compute_connected_duration_seconds();
}

uint32_t wifi_connection_get_connected_duration_high_water_seconds(void)
{
    uint32_t high_water = s_wifi_connection.connected_duration_high_water_seconds;
    uint32_t connected_seconds = wifi_connection_compute_connected_duration_seconds();

    if (connected_seconds > high_water) {
        return connected_seconds;
    }

    return high_water;
}

wifi_connection_warning_t wifi_connection_get_warning(void)
{
    if (s_wifi_connection.state == WIFI_CONNECTION_STATE_CONNECTED &&
        !wifi_connection_state_is_setup(s_wifi_connection.state) &&
        wifi_connection_get_connected_duration_seconds() >= CONFIG_WIFI_CONNECTION_CONNECTED_WARNING_SECONDS) {
        return WIFI_CONNECTION_WARNING_CONNECTED_TOO_LONG;
    }

    return WIFI_CONNECTION_WARNING_NONE;
}

bool wifi_connection_is_enabled(void)
{
    wifi_connection_state_t state = WIFI_CONNECTION_STATE_STOPPED;

    if (wifi_connection_get_state(&state) != ESP_OK) {
        return false;
    }
    return state != WIFI_CONNECTION_STATE_STOPPED &&
           state != WIFI_CONNECTION_STATE_OFF;
}

bool wifi_connection_can_request_connection(void)
{
    wifi_connection_state_t state = WIFI_CONNECTION_STATE_STOPPED;

    if (wifi_connection_get_state(&state) != ESP_OK) {
        return false;
    }
    return state == WIFI_CONNECTION_STATE_INIT ||
           state == WIFI_CONNECTION_STATE_OFF ||
           state == WIFI_CONNECTION_STATE_CONNECTED ||
           state == WIFI_CONNECTION_STATE_FAILED;
}

bool wifi_connection_is_setup_active(void)
{
    wifi_connection_state_t state = WIFI_CONNECTION_STATE_STOPPED;

    if (wifi_connection_get_state(&state) != ESP_OK) {
        return false;
    }
    return state == WIFI_CONNECTION_STATE_SETUP_RUNNING;
}

bool wifi_connection_is_setup_requested_explicitly(void)
{
    wifi_connection_state_t state = WIFI_CONNECTION_STATE_STOPPED;

    if (wifi_connection_get_state(&state) != ESP_OK) {
        return false;
    }
    return state == WIFI_CONNECTION_STATE_SETUP_REQUIRED &&
           s_wifi_connection.setup_requested_explicitly;
}

esp32_wifi_sta_failure_reason_t wifi_connection_get_last_failure_reason(void)
{
    return s_wifi_connection.last_failure_reason;
}

esp_err_t wifi_connection_retry_connection_without_setup(TickType_t wait_ticks)
{
    ESP_RETURN_ON_FALSE(s_wifi_connection.event_group != NULL, ESP_ERR_INVALID_STATE, TAG, "manager not started");

    wifi_connection_state_t state = s_wifi_connection.state;
    if (state == WIFI_CONNECTION_STATE_FAILED) {
        ESP_RETURN_ON_ERROR(wifi_connection_disable(), TAG, "Wi-Fi connection disable before retry failed");
    }

    return wifi_connection_request_connection_without_setup(wait_ticks);
}

esp_err_t wifi_connection_retry_connection_without_setup_async(void)
{
    ESP_RETURN_ON_FALSE(s_wifi_connection.event_group != NULL, ESP_ERR_INVALID_STATE, TAG, "manager not started");

    wifi_connection_state_t state = s_wifi_connection.state;
    if (state == WIFI_CONNECTION_STATE_FAILED) {
        ESP_RETURN_ON_ERROR(wifi_connection_disable(), TAG, "Wi-Fi connection disable before retry failed");
    }

    return wifi_connection_request_connection_without_setup_async();
}
