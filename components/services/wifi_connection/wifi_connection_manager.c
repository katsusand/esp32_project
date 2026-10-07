#include <stdbool.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/portmacro.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "app_stack_monitor.h"
#include "error_log_store.h"
#include "esp32_wifi_sta.h"
#include "wifi_connection.h"
#include "wifi_connection_internal.h"

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

/* Mirrors state == CONNECTED; set and cleared only in wifi_connection_set_state(). */
#define WIFI_CONNECTION_CONNECTED_BIT BIT0

/* A few waiting callers plus STA events that arrive while an attempt is set up. */
#define WIFI_CONNECTION_QUEUE_LENGTH 12

#define TAG "wifi_connection"

/*
 * The manager task is the only place that operates the STA.
 *
 * English contract: requests from other tasks and STA events from the ESP-IDF
 * event task all arrive through one queue, and this task handles them one at a
 * time. Nothing else writes the state below. Earlier, the setup UI drove the
 * STA from the app_shell task and the event task reconnected on its own, and
 * flags kept the three apart; a missed case let a connect attempt restart the
 * STA under the setup scan (review item #4). With one owner there is nothing
 * left to keep apart.
 */

typedef enum {
    WIFI_CONNECTION_CMD_ACQUIRE = 0,
    WIFI_CONNECTION_CMD_RELEASE,
    WIFI_CONNECTION_CMD_RETRY,
    WIFI_CONNECTION_CMD_BEGIN_SETUP,
    WIFI_CONNECTION_CMD_SETUP_SCAN,
    WIFI_CONNECTION_CMD_SETUP_CONNECT,
    WIFI_CONNECTION_CMD_COMPLETE_SETUP,
    WIFI_CONNECTION_CMD_STA_EVENT,
} wifi_connection_cmd_type_t;

typedef struct {
    SemaphoreHandle_t done;
    esp_err_t result;
} wifi_connection_completion_t;

typedef struct {
    wifi_connection_cmd_type_t type;
    /* The waiting caller's, on its stack; NULL for STA events. */
    wifi_connection_completion_t *completion;
    union {
        wifi_connection_user_t user;
        bool setup_connected;
        esp32_wifi_sta_event_t sta_event;
        struct {
            esp32_wifi_sta_scan_record_t *records;
            size_t capacity;
            size_t *count;
        } scan;
        struct {
            const char *ssid;
            const char *password;
            wifi_auth_mode_t authmode;
            TickType_t wait_ticks;
            esp32_wifi_sta_failure_reason_t *failure_reason;
        } connect;
    } arg;
} wifi_connection_cmd_t;

typedef struct {
    TaskHandle_t task_handle;
    QueueHandle_t queue;
    EventGroupHandle_t event_group;
    volatile wifi_connection_state_t state;
    volatile esp32_wifi_sta_failure_reason_t last_failure_reason;
    volatile uint32_t active_users;
    volatile wifi_connection_user_t last_user;
    volatile int64_t connected_since_us;
    volatile uint32_t connected_duration_high_water_seconds;
    volatile bool setup_requested_explicitly;
    volatile bool setup_requested_on_start;
    volatile bool ssid_configured;
    volatile bool last_connection_succeeded;
    volatile bool connection_result_initialized;
    /* Manager task only. */
    bool connect_wanted;
    bool stop_when_unused;
    bool has_deferred;
    wifi_connection_cmd_t deferred;
    wifi_connection_connected_callback_t connected_callback;
    void *connected_callback_ctx;
    wifi_connection_connectivity_callback_t connectivity_callback;
    void *connectivity_callback_ctx;
} wifi_connection_context_t;

typedef enum {
    WIFI_CONNECTION_WAIT_TIMEOUT = 0,
    WIFI_CONNECTION_WAIT_STA_EVENT,
    WIFI_CONNECTION_WAIT_ABORTED,
} wifi_connection_wait_result_t;

static wifi_connection_context_t s_wifi_connection = {
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

    if (state == WIFI_CONNECTION_STATE_CONNECTED) {
        xEventGroupSetBits(s_wifi_connection.event_group, WIFI_CONNECTION_CONNECTED_BIT);
    } else {
        xEventGroupClearBits(s_wifi_connection.event_group, WIFI_CONNECTION_CONNECTED_BIT);
    }
}

static bool wifi_connection_state_is_setup(wifi_connection_state_t state)
{
    return state == WIFI_CONNECTION_STATE_SETUP_REQUIRED ||
           state == WIFI_CONNECTION_STATE_SETUP_RUNNING;
}

static bool wifi_connection_has_active_users(void)
{
    portENTER_CRITICAL(&s_wifi_connection_lock);
    bool has_active_users = s_wifi_connection.active_users != 0;
    portEXIT_CRITICAL(&s_wifi_connection_lock);
    return has_active_users;
}

static bool wifi_connection_on_manager_task(void)
{
    return s_wifi_connection.task_handle != NULL &&
           xTaskGetCurrentTaskHandle() == s_wifi_connection.task_handle;
}

static void wifi_connection_notify_connected(void)
{
    wifi_connection_connected_callback_t callback = s_wifi_connection.connected_callback;

    if (callback != NULL) {
        callback(s_wifi_connection.connected_callback_ctx);
    }
}

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
    ESP_LOGW(TAG, "Wi-Fi setup required");
}

static void wifi_connection_log_stack_usage(void)
{
    APP_STACK_MONITOR_CHECK(TAG, "wifi_connection", CONFIG_WIFI_CONNECTION_STACK_LOG_INTERVAL_MS);
}

static const char *wifi_connection_failure_reason_text(esp32_wifi_sta_failure_reason_t reason)
{
    switch (reason) {
    case ESP32_WIFI_STA_FAILURE_NO_SAVED_PROFILE:
        return "no saved profile";
    case ESP32_WIFI_STA_FAILURE_NO_AP_IN_RANGE:
        return "AP not found";
    case ESP32_WIFI_STA_FAILURE_AUTH:
        return "auth failed";
    case ESP32_WIFI_STA_FAILURE_TIMEOUT:
        return "timeout";
    case ESP32_WIFI_STA_FAILURE_CONNECT:
        return "connect failed";
    case ESP32_WIFI_STA_FAILURE_NONE:
    default:
        return "unknown";
    }
}

/*
 * One error log line per failed connection sequence, never per attempt, with
 * the reason ("auth failed" usually means a wrong password). No SSID: it can
 * tell where the device is installed.
 *
 * English contract: a macro, not a function, so that the log line names the
 * function and line that recorded the failure rather than this helper.
 */
#define wifi_connection_record_failure(what, err, reason)                                   \
    ERROR_LOG((err),                                                                         \
              "%s (%s, reason=%d)",                                                          \
              (what),                                                                        \
              wifi_connection_failure_reason_text(reason),                                   \
              (int)(reason))

/* The main loop runs an attempt with the saved profiles next. */
static void wifi_connection_request_connect(wifi_connection_state_t state)
{
    s_wifi_connection.connect_wanted = true;
    wifi_connection_set_state(state);
}

static esp_err_t wifi_connection_stop_sta(void)
{
    esp32_wifi_sta_status_t status = { 0 };
    esp_err_t err = esp32_wifi_sta_get_status(&status);

    if (err == ESP_ERR_INVALID_STATE) {
        return ESP_OK; /* never initialized, so nothing runs */
    }
    ESP_RETURN_ON_ERROR(err, TAG, "Wi-Fi STA status failed");
    return esp32_wifi_sta_stop();
}

/* OFF even when the stop fails: every attempt stops the STA first anyway. */
static esp_err_t wifi_connection_turn_off(void)
{
    esp_err_t err = wifi_connection_stop_sta();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi STA stop failed: %s", esp_err_to_name(err));
    }
    s_wifi_connection.last_failure_reason = ESP32_WIFI_STA_FAILURE_NONE;
    wifi_connection_set_state(WIFI_CONNECTION_STATE_OFF);
    ESP_LOGI(TAG, "Wi-Fi connection off");
    return err;
}

static void wifi_connection_complete(const wifi_connection_cmd_t *cmd, esp_err_t result)
{
    if (cmd->completion != NULL) {
        cmd->completion->result = result;
        /* The caller may return as soon as this is given: nothing after it
           may touch the completion. */
        xSemaphoreGive(cmd->completion->done);
    }
}

/* Requests that operate the STA. One arriving while a procedure runs
   interrupts it; see wifi_connection_wait_until(). */
static bool wifi_connection_cmd_needs_sta(wifi_connection_cmd_type_t type)
{
    return type == WIFI_CONNECTION_CMD_BEGIN_SETUP ||
           type == WIFI_CONNECTION_CMD_SETUP_SCAN ||
           type == WIFI_CONNECTION_CMD_SETUP_CONNECT ||
           type == WIFI_CONNECTION_CMD_COMPLETE_SETUP;
}

static void wifi_connection_handle_acquire(const wifi_connection_cmd_t *cmd)
{
    wifi_connection_user_t user = cmd->arg.user;

    portENTER_CRITICAL(&s_wifi_connection_lock);
    s_wifi_connection.last_user = user;
    s_wifi_connection.active_users |= (uint32_t)user;
    portEXIT_CRITICAL(&s_wifi_connection_lock);
    s_wifi_connection.stop_when_unused = false;

    wifi_connection_state_t state = s_wifi_connection.state;
    if (state == WIFI_CONNECTION_STATE_INIT ||
        state == WIFI_CONNECTION_STATE_OFF ||
        state == WIFI_CONNECTION_STATE_FAILED) {
        wifi_connection_request_connect(WIFI_CONNECTION_STATE_CONNECTING);
    }
    wifi_connection_complete(cmd, ESP_OK);
}

static void wifi_connection_handle_release(const wifi_connection_cmd_t *cmd)
{
    wifi_connection_user_t user = cmd->arg.user;

    portENTER_CRITICAL(&s_wifi_connection_lock);
    if ((s_wifi_connection.active_users & (uint32_t)user) == 0) {
        portEXIT_CRITICAL(&s_wifi_connection_lock);
        ESP_LOGW(TAG, "Wi-Fi user release without acquire: 0x%02x", (unsigned)user);
        wifi_connection_complete(cmd, ESP_ERR_INVALID_STATE);
        return;
    }
    s_wifi_connection.active_users &= ~((uint32_t)user);
    bool has_active_users = s_wifi_connection.active_users != 0;
    portEXIT_CRITICAL(&s_wifi_connection_lock);

    if (!has_active_users && !wifi_connection_state_is_setup(s_wifi_connection.state)) {
        s_wifi_connection.stop_when_unused = true;
    }
    wifi_connection_complete(cmd, ESP_OK);
}

static void wifi_connection_handle_retry(const wifi_connection_cmd_t *cmd)
{
    wifi_connection_state_t state = s_wifi_connection.state;

    if (state == WIFI_CONNECTION_STATE_CONNECTED) {
        wifi_connection_complete(cmd, ESP_OK);
        return;
    }
    if (state != WIFI_CONNECTION_STATE_INIT &&
        state != WIFI_CONNECTION_STATE_OFF &&
        state != WIFI_CONNECTION_STATE_FAILED) {
        wifi_connection_complete(cmd, ESP_ERR_INVALID_STATE);
        return;
    }
    wifi_connection_request_connect(WIFI_CONNECTION_STATE_CONNECTING);
    wifi_connection_complete(cmd, ESP_OK);
}

/* Requests that never touch the STA; answered even while a procedure runs. */
static void wifi_connection_handle_quick(const wifi_connection_cmd_t *cmd)
{
    switch (cmd->type) {
    case WIFI_CONNECTION_CMD_ACQUIRE:
        wifi_connection_handle_acquire(cmd);
        break;
    case WIFI_CONNECTION_CMD_RELEASE:
        wifi_connection_handle_release(cmd);
        break;
    case WIFI_CONNECTION_CMD_RETRY:
        wifi_connection_handle_retry(cmd);
        break;
    default:
        wifi_connection_complete(cmd, ESP_ERR_INVALID_ARG);
        break;
    }
}

/*
 * Waits until `deadline`, answering requests meanwhile.
 *
 * English contract: runs only inside a procedure. acquire, release and retry
 * are handled here, so their callers are not held up by a long attempt. A
 * request that needs the STA is kept for the main loop and ends the wait as
 * ABORTED. An STA event ends the wait when `event` is non-NULL; outside an
 * attempt it belongs to one that already ended and is dropped.
 */
static wifi_connection_wait_result_t wifi_connection_wait_until(TickType_t deadline,
                                                                esp32_wifi_sta_event_t *event)
{
    while (!s_wifi_connection.has_deferred) {
        int32_t remaining = (int32_t)(deadline - xTaskGetTickCount());
        wifi_connection_cmd_t cmd;

        if (xQueueReceive(s_wifi_connection.queue, &cmd, remaining > 0 ? (TickType_t)remaining : 0) != pdTRUE) {
            return WIFI_CONNECTION_WAIT_TIMEOUT;
        }
        if (cmd.type == WIFI_CONNECTION_CMD_STA_EVENT) {
            if (event != NULL) {
                *event = cmd.arg.sta_event;
                return WIFI_CONNECTION_WAIT_STA_EVENT;
            }
            continue;
        }
        if (wifi_connection_cmd_needs_sta(cmd.type)) {
            s_wifi_connection.deferred = cmd;
            s_wifi_connection.has_deferred = true;
            break;
        }
        wifi_connection_handle_quick(&cmd);
    }
    return WIFI_CONNECTION_WAIT_ABORTED;
}

bool wifi_connection_manager_aborted(void)
{
    return s_wifi_connection.has_deferred;
}

bool wifi_connection_manager_delay(TickType_t delay_ticks)
{
    return wifi_connection_wait_until(xTaskGetTickCount() + delay_ticks, NULL) != WIFI_CONNECTION_WAIT_ABORTED;
}

esp_err_t wifi_connection_manager_wait_attempt(TickType_t wait_ticks,
                                               esp32_wifi_sta_failure_reason_t *failure_reason)
{
    esp32_wifi_sta_event_t event = { 0 };

    switch (wifi_connection_wait_until(xTaskGetTickCount() + wait_ticks, &event)) {
    case WIFI_CONNECTION_WAIT_STA_EVENT:
        if (event.type == ESP32_WIFI_STA_EVENT_CONNECTED) {
            return ESP_OK;
        }
        *failure_reason = event.failure_reason;
        return ESP_FAIL;
    case WIFI_CONNECTION_WAIT_ABORTED:
        return ESP_ERR_INVALID_STATE;
    case WIFI_CONNECTION_WAIT_TIMEOUT:
    default:
        /* The event is lost if the queue was full; the driver still knows. */
        if (esp32_wifi_sta_is_connected()) {
            return ESP_OK;
        }
        *failure_reason = ESP32_WIFI_STA_FAILURE_TIMEOUT;
        return ESP_ERR_TIMEOUT;
    }
}

/* Connects with the saved profiles, rescanning while none of them is in range.
   The state is already CONNECTING or RECONNECTING. */
static void wifi_connection_run_auto_connect(void)
{
    esp32_wifi_sta_failure_reason_t failure_reason = ESP32_WIFI_STA_FAILURE_NONE;
    esp_err_t err = ESP_FAIL;

    s_wifi_connection.last_failure_reason = ESP32_WIFI_STA_FAILURE_NONE;
    for (uint32_t attempt = 1; attempt <= CONFIG_WIFI_CONNECTION_SCAN_RETRY_ATTEMPTS; ++attempt) {
        err = wifi_connection_run_saved_profiles(pdMS_TO_TICKS(CONFIG_ESP32_WIFI_STA_CONNECT_TIMEOUT_MS),
                                                 &failure_reason);
        if (err == ESP_OK || wifi_connection_manager_aborted()) {
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
        if (!wifi_connection_manager_delay(pdMS_TO_TICKS(CONFIG_WIFI_CONNECTION_SCAN_RETRY_DELAY_MS))) {
            break;
        }
    }

    if (wifi_connection_manager_aborted()) {
        /* The waiting request runs next and normally takes the STA (setup).
           If it leaves the STA alone after all, the attempt starts over. */
        ESP_LOGI(TAG, "Wi-Fi connect interrupted");
        s_wifi_connection.connect_wanted = true;
        return;
    }

    if (err == ESP_OK) {
        wifi_connection_set_connection_result(true, true);
        s_wifi_connection.last_failure_reason = ESP32_WIFI_STA_FAILURE_NONE;
        wifi_connection_set_state(WIFI_CONNECTION_STATE_CONNECTED);
        ESP_LOGI(TAG, "Wi-Fi connection connected");
        wifi_connection_notify_connected();
        return;
    }

    s_wifi_connection.last_failure_reason = failure_reason;
    wifi_connection_set_connection_result(esp32_wifi_sta_has_configured_ssid(), false);
    wifi_connection_set_state(WIFI_CONNECTION_STATE_FAILED);
    wifi_connection_record_failure("Wi-Fi connection connect failed", err, failure_reason);
}

static void wifi_connection_handle_begin_setup(const wifi_connection_cmd_t *cmd)
{
    esp_err_t err = wifi_connection_stop_sta();
    if (err != ESP_OK) {
        /* Setup does not start; an interrupted attempt resumes. */
        ESP_LOGE(TAG, "stop Wi-Fi before setup failed: %s", esp_err_to_name(err));
        wifi_connection_complete(cmd, err);
        return;
    }

    s_wifi_connection.connect_wanted = false;
    s_wifi_connection.stop_when_unused = false;
    s_wifi_connection.setup_requested_explicitly = false;
    s_wifi_connection.last_failure_reason = ESP32_WIFI_STA_FAILURE_NONE;
    wifi_connection_set_state(WIFI_CONNECTION_STATE_SETUP_RUNNING);
    ESP_LOGI(TAG, "Wi-Fi setup started");
    wifi_connection_complete(cmd, ESP_OK);
}

static void wifi_connection_handle_setup_scan(const wifi_connection_cmd_t *cmd)
{
    if (s_wifi_connection.state != WIFI_CONNECTION_STATE_SETUP_RUNNING) {
        wifi_connection_complete(cmd, ESP_ERR_INVALID_STATE);
        return;
    }

    esp_err_t err = esp32_wifi_sta_enter_scan_mode();
    if (err == ESP_OK) {
        err = esp32_wifi_sta_get_scan_records(cmd->arg.scan.records, cmd->arg.scan.capacity, cmd->arg.scan.count);
    }
    if (err == ESP_OK && *cmd->arg.scan.count > cmd->arg.scan.capacity) {
        *cmd->arg.scan.count = cmd->arg.scan.capacity; /* report what was copied */
    }
    wifi_connection_complete(cmd, err);
}

static void wifi_connection_handle_setup_connect(const wifi_connection_cmd_t *cmd)
{
    esp32_wifi_sta_failure_reason_t failure_reason = ESP32_WIFI_STA_FAILURE_NONE;
    esp_err_t err = ESP_ERR_INVALID_STATE;

    if (s_wifi_connection.state == WIFI_CONNECTION_STATE_SETUP_RUNNING) {
        err = wifi_connection_run_connect_and_save(cmd->arg.connect.ssid,
                                                   cmd->arg.connect.password,
                                                   cmd->arg.connect.authmode,
                                                   cmd->arg.connect.wait_ticks,
                                                   &failure_reason);
        /* An interrupted test is not a failed one: another request took the STA. */
        if (err != ESP_OK && !wifi_connection_manager_aborted()) {
            wifi_connection_record_failure("Wi-Fi setup connection test failed", err, failure_reason);
        }
    }
    if (cmd->arg.connect.failure_reason != NULL) {
        *cmd->arg.connect.failure_reason = failure_reason;
    }
    wifi_connection_complete(cmd, err);
}

static void wifi_connection_handle_complete_setup(const wifi_connection_cmd_t *cmd)
{
    if (s_wifi_connection.state != WIFI_CONNECTION_STATE_SETUP_RUNNING) {
        wifi_connection_complete(cmd, ESP_ERR_INVALID_STATE);
        return;
    }

    if (cmd->arg.setup_connected) {
        /* connect_and_save left the tested profile connected. */
        wifi_connection_set_connection_result(true, true);
        s_wifi_connection.last_failure_reason = ESP32_WIFI_STA_FAILURE_NONE;
        wifi_connection_set_state(WIFI_CONNECTION_STATE_CONNECTED);
        ESP_LOGI(TAG, "Wi-Fi setup completed");
        wifi_connection_notify_connected();
        wifi_connection_complete(cmd, ESP_OK);
        return;
    }

    ESP_LOGI(TAG, "Wi-Fi setup cancelled");
    if (wifi_connection_has_active_users()) {
        /* Setup only paused its users (wait_connected answered
           ESP_ERR_NOT_FINISHED), so resume them with the saved profiles. With
           none saved, or none in range, that ends as an ordinary failure with
           its reason. */
        esp_err_t err = wifi_connection_stop_sta();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Wi-Fi STA stop failed: %s", esp_err_to_name(err));
        }
        ESP_LOGI(TAG, "resuming Wi-Fi for its users");
        wifi_connection_request_connect(WIFI_CONNECTION_STATE_CONNECTING);
        wifi_connection_complete(cmd, ESP_OK);
        return;
    }
    wifi_connection_complete(cmd, wifi_connection_turn_off());
}

static void wifi_connection_handle_sta_event(const esp32_wifi_sta_event_t *event)
{
    if (event->type == ESP32_WIFI_STA_EVENT_DISCONNECTED &&
        s_wifi_connection.state == WIFI_CONNECTION_STATE_CONNECTED) {
        ESP_LOGW(TAG, "Wi-Fi link lost (reason=%d); reconnecting", (int)event->failure_reason);
        wifi_connection_request_connect(WIFI_CONNECTION_STATE_RECONNECTING);
    }
    /* Anything else belongs to an attempt that has already ended. */
}

static void wifi_connection_handle(const wifi_connection_cmd_t *cmd)
{
    switch (cmd->type) {
    case WIFI_CONNECTION_CMD_BEGIN_SETUP:
        wifi_connection_handle_begin_setup(cmd);
        break;
    case WIFI_CONNECTION_CMD_SETUP_SCAN:
        wifi_connection_handle_setup_scan(cmd);
        break;
    case WIFI_CONNECTION_CMD_SETUP_CONNECT:
        wifi_connection_handle_setup_connect(cmd);
        break;
    case WIFI_CONNECTION_CMD_COMPLETE_SETUP:
        wifi_connection_handle_complete_setup(cmd);
        break;
    case WIFI_CONNECTION_CMD_STA_EVENT:
        wifi_connection_handle_sta_event(&cmd->arg.sta_event);
        break;
    default:
        wifi_connection_handle_quick(cmd);
        break;
    }
}

/* Polled while connected, in case the disconnect event was lost. */
static void wifi_connection_check_link(void)
{
    if (s_wifi_connection.state != WIFI_CONNECTION_STATE_CONNECTED) {
        return;
    }
    if (esp32_wifi_sta_is_connected()) {
        wifi_connection_set_connection_result(true, true);
        return;
    }
    ESP_LOGW(TAG, "Wi-Fi connection lost; reconnecting");
    wifi_connection_request_connect(WIFI_CONNECTION_STATE_RECONNECTING);
}

/* Wi-Fi goes off once the last user has released it and any attempt that was
   under way has finished. */
static void wifi_connection_stop_if_unused(void)
{
    if (!s_wifi_connection.stop_when_unused ||
        s_wifi_connection.connect_wanted ||
        s_wifi_connection.has_deferred) {
        return;
    }
    s_wifi_connection.stop_when_unused = false;

    wifi_connection_state_t state = s_wifi_connection.state;
    if (wifi_connection_has_active_users() ||
        wifi_connection_state_is_setup(state) ||
        state == WIFI_CONNECTION_STATE_OFF) {
        return;
    }
    (void)wifi_connection_turn_off();
}

/* Runs on the ESP-IDF event task, so it only queues the event. */
static void wifi_connection_on_sta_event(const esp32_wifi_sta_event_t *event, void *ctx)
{
    (void)ctx;

    wifi_connection_cmd_t cmd = {
        .type = WIFI_CONNECTION_CMD_STA_EVENT,
        .completion = NULL,
        .arg.sta_event = *event,
    };
    if (xQueueSend(s_wifi_connection.queue, &cmd, 0) != pdTRUE) {
        /* Attempts fall back to the driver state on timeout, and the link
           check polls while connected. */
        ESP_LOGW(TAG, "STA event dropped: queue full");
    }
}

static void wifi_connection_task(void *arg)
{
    (void)arg;

    esp32_wifi_sta_set_event_callback(wifi_connection_on_sta_event, NULL);
    wifi_connection_set_state(WIFI_CONNECTION_STATE_INIT);
    s_wifi_connection.last_failure_reason = ESP32_WIFI_STA_FAILURE_NONE;
    bool configured = esp32_wifi_sta_has_configured_ssid();
    wifi_connection_set_connection_result(configured, false);

    if (s_wifi_connection.setup_requested_on_start) {
        s_wifi_connection.setup_requested_on_start = false;
        wifi_connection_set_setup_required(true);
    } else if (!configured) {
        wifi_connection_set_setup_required(false);
    } else {
        (void)wifi_connection_turn_off();
    }

    while (true) {
        wifi_connection_log_stack_usage();

        if (s_wifi_connection.has_deferred) {
            /* It arrived before anything still queued. */
            wifi_connection_cmd_t cmd = s_wifi_connection.deferred;
            s_wifi_connection.has_deferred = false;
            wifi_connection_handle(&cmd);
        } else if (s_wifi_connection.connect_wanted) {
            s_wifi_connection.connect_wanted = false;
            wifi_connection_run_auto_connect();
        } else {
            wifi_connection_cmd_t cmd;
            TickType_t wait_ticks = s_wifi_connection.state == WIFI_CONNECTION_STATE_CONNECTED ?
                                    pdMS_TO_TICKS(CONFIG_WIFI_CONNECTION_MONITOR_INTERVAL_MS) :
                                    portMAX_DELAY;
            if (xQueueReceive(s_wifi_connection.queue, &cmd, wait_ticks) == pdTRUE) {
                wifi_connection_handle(&cmd);
            } else {
                wifi_connection_check_link();
            }
        }
        wifi_connection_stop_if_unused();
    }
}

/*
 * Hands a request to the manager task and waits until it has been handled.
 *
 * English contract: no timeout. The manager answers every request exactly
 * once, and `completion` lives in this stack frame, so giving up early would
 * leave the manager writing into a dead frame. The wait is bounded by the
 * work itself: one scan, or connect_and_save's attempts.
 */
static esp_err_t wifi_connection_call(wifi_connection_cmd_t *cmd)
{
    ESP_RETURN_ON_FALSE(s_wifi_connection.queue != NULL, ESP_ERR_INVALID_STATE, TAG, "manager not started");
    ESP_RETURN_ON_FALSE(!wifi_connection_on_manager_task(),
                        ESP_ERR_INVALID_STATE,
                        TAG,
                        "request from the manager task would wait on itself");

    StaticSemaphore_t done_storage;
    wifi_connection_completion_t completion = {
        .done = xSemaphoreCreateBinaryStatic(&done_storage),
        .result = ESP_FAIL,
    };
    cmd->completion = &completion;
    (void)xQueueSend(s_wifi_connection.queue, cmd, portMAX_DELAY);
    (void)xSemaphoreTake(completion.done, portMAX_DELAY);
    vSemaphoreDelete(completion.done);
    return completion.result;
}

esp_err_t wifi_connection_start(void)
{
    if (s_wifi_connection.task_handle != NULL) {
        return ESP_OK;
    }
    if (s_wifi_connection.event_group == NULL) {
        s_wifi_connection.event_group = xEventGroupCreate();
        ESP_RETURN_ON_FALSE(s_wifi_connection.event_group != NULL, ESP_ERR_NO_MEM, TAG, "event group alloc failed");
    }
    if (s_wifi_connection.queue == NULL) {
        s_wifi_connection.queue = xQueueCreate(WIFI_CONNECTION_QUEUE_LENGTH, sizeof(wifi_connection_cmd_t));
        ESP_RETURN_ON_FALSE(s_wifi_connection.queue != NULL, ESP_ERR_NO_MEM, TAG, "queue alloc failed");
    }

    BaseType_t task_ok = xTaskCreate(wifi_connection_task,
                                     "wifi_connection",
                                     CONFIG_WIFI_CONNECTION_TASK_STACK_SIZE,
                                     NULL,
                                     CONFIG_WIFI_CONNECTION_TASK_PRIORITY,
                                     &s_wifi_connection.task_handle);
    if (task_ok != pdPASS) {
        /* Without the task nobody would answer, and requests wait forever. */
        vQueueDelete(s_wifi_connection.queue);
        s_wifi_connection.queue = NULL;
        s_wifi_connection.task_handle = NULL;
        ESP_LOGE(TAG, "task create failed");
        return ESP_ERR_NO_MEM;
    }
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
    ESP_RETURN_ON_FALSE(user != 0, ESP_ERR_INVALID_ARG, TAG, "user is zero");

    wifi_connection_cmd_t cmd = {
        .type = WIFI_CONNECTION_CMD_ACQUIRE,
        .arg.user = user,
    };
    return wifi_connection_call(&cmd);
}

esp_err_t wifi_connection_release(wifi_connection_user_t user)
{
    ESP_RETURN_ON_FALSE(user != 0, ESP_ERR_INVALID_ARG, TAG, "user is zero");

    wifi_connection_cmd_t cmd = {
        .type = WIFI_CONNECTION_CMD_RELEASE,
        .arg.user = user,
    };
    return wifi_connection_call(&cmd);
}

/*
 * Retries go straight to a connection request, with no disable first. The old
 * disable waited up to 7 s on the caller's task for an OFF that never came
 * while radio_manager still held Wi-Fi, so RETRY and SYNC NOW froze the UI and
 * then failed.
 */
esp_err_t wifi_connection_retry_connection_without_setup_async(void)
{
    wifi_connection_cmd_t cmd = {
        .type = WIFI_CONNECTION_CMD_RETRY,
    };
    return wifi_connection_call(&cmd);
}

esp_err_t wifi_connection_begin_setup(void)
{
    wifi_connection_cmd_t cmd = {
        .type = WIFI_CONNECTION_CMD_BEGIN_SETUP,
    };
    return wifi_connection_call(&cmd);
}

esp_err_t wifi_connection_setup_scan(esp32_wifi_sta_scan_record_t *records,
                                     size_t record_capacity,
                                     size_t *record_count)
{
    ESP_RETURN_ON_FALSE(record_count != NULL, ESP_ERR_INVALID_ARG, TAG, "record_count is null");
    ESP_RETURN_ON_FALSE(records != NULL || record_capacity == 0, ESP_ERR_INVALID_ARG, TAG, "records is null");
    *record_count = 0;

    wifi_connection_cmd_t cmd = {
        .type = WIFI_CONNECTION_CMD_SETUP_SCAN,
        .arg.scan = {
            .records = records,
            .capacity = record_capacity,
            .count = record_count,
        },
    };
    return wifi_connection_call(&cmd);
}

esp_err_t wifi_connection_connect_and_save(const char *ssid,
                                           const char *password,
                                           wifi_auth_mode_t authmode,
                                           TickType_t wait_ticks,
                                           esp32_wifi_sta_failure_reason_t *failure_reason)
{
    ESP_RETURN_ON_FALSE(ssid != NULL && ssid[0] != '\0', ESP_ERR_INVALID_ARG, TAG, "Wi-Fi SSID is empty");
    /* The profile is saved only after a connection is seen, which needs a wait. */
    ESP_RETURN_ON_FALSE(wait_ticks > 0, ESP_ERR_INVALID_ARG, TAG, "wait_ticks must be positive");
    if (failure_reason != NULL) {
        *failure_reason = ESP32_WIFI_STA_FAILURE_NONE;
    }

    wifi_connection_cmd_t cmd = {
        .type = WIFI_CONNECTION_CMD_SETUP_CONNECT,
        .arg.connect = {
            .ssid = ssid,
            .password = password,
            .authmode = authmode,
            .wait_ticks = wait_ticks,
            .failure_reason = failure_reason,
        },
    };
    return wifi_connection_call(&cmd);
}

esp_err_t wifi_connection_complete_setup(bool connected)
{
    wifi_connection_cmd_t cmd = {
        .type = WIFI_CONNECTION_CMD_COMPLETE_SETUP,
        .arg.setup_connected = connected,
    };
    return wifi_connection_call(&cmd);
}

esp_err_t wifi_connection_wait_connected(TickType_t wait_ticks)
{
    ESP_RETURN_ON_FALSE(s_wifi_connection.event_group != NULL, ESP_ERR_INVALID_STATE, TAG, "manager not started");
    ESP_RETURN_ON_FALSE(s_wifi_connection.state != WIFI_CONNECTION_STATE_OFF,
                        ESP_ERR_INVALID_STATE,
                        TAG,
                        "Wi-Fi connection is off");

    if (s_wifi_connection.state == WIFI_CONNECTION_STATE_SETUP_RUNNING) {
        return ESP_ERR_NOT_FINISHED;
    }

    EventBits_t bits = xEventGroupWaitBits(s_wifi_connection.event_group,
                                           WIFI_CONNECTION_CONNECTED_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           wait_ticks);
    if ((bits & WIFI_CONNECTION_CONNECTED_BIT) != 0) {
        return ESP_OK;
    }
    return s_wifi_connection.state == WIFI_CONNECTION_STATE_SETUP_RUNNING ? ESP_ERR_NOT_FINISHED : ESP_ERR_TIMEOUT;
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
        wifi_connection_get_connected_duration_seconds() >= CONFIG_WIFI_CONNECTION_CONNECTED_WARNING_SECONDS) {
        return WIFI_CONNECTION_WARNING_CONNECTED_TOO_LONG;
    }

    return WIFI_CONNECTION_WARNING_NONE;
}

bool wifi_connection_is_enabled(void)
{
    wifi_connection_state_t state = s_wifi_connection.state;

    return state != WIFI_CONNECTION_STATE_STOPPED &&
           state != WIFI_CONNECTION_STATE_OFF;
}

bool wifi_connection_can_request_connection(void)
{
    wifi_connection_state_t state = s_wifi_connection.state;

    return state == WIFI_CONNECTION_STATE_INIT ||
           state == WIFI_CONNECTION_STATE_OFF ||
           state == WIFI_CONNECTION_STATE_CONNECTED ||
           state == WIFI_CONNECTION_STATE_FAILED;
}

bool wifi_connection_is_setup_active(void)
{
    return s_wifi_connection.state == WIFI_CONNECTION_STATE_SETUP_RUNNING;
}

bool wifi_connection_is_setup_requested_explicitly(void)
{
    return s_wifi_connection.state == WIFI_CONNECTION_STATE_SETUP_REQUIRED &&
           s_wifi_connection.setup_requested_explicitly;
}

esp32_wifi_sta_failure_reason_t wifi_connection_get_last_failure_reason(void)
{
    return s_wifi_connection.last_failure_reason;
}
