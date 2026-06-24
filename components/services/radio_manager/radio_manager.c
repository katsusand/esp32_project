#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_health.h"
#include "nvs_schema.h"
#include "sdkconfig.h"
#include "app_stack_monitor.h"
#include "radio_manager.h"
#include "wifi_connection.h"

#ifndef CONFIG_RADIO_MANAGER_TASK_STACK_SIZE
#define CONFIG_RADIO_MANAGER_TASK_STACK_SIZE 4096
#endif
#ifndef CONFIG_RADIO_MANAGER_TASK_PRIORITY
#define CONFIG_RADIO_MANAGER_TASK_PRIORITY 7
#endif
#ifndef CONFIG_RADIO_MANAGER_REQUEST_QUEUE_LEN
#define CONFIG_RADIO_MANAGER_REQUEST_QUEUE_LEN 5
#endif
#ifndef CONFIG_RADIO_MANAGER_CONTROL_QUEUE_LEN
#define CONFIG_RADIO_MANAGER_CONTROL_QUEUE_LEN 5
#endif
#ifndef CONFIG_RADIO_MANAGER_IDLE_TIMEOUT_MS
#define CONFIG_RADIO_MANAGER_IDLE_TIMEOUT_MS 30000
#endif
#define RADIO_MANAGER_NOTIFY_GRANTED 0x80000000UL
#define RADIO_MANAGER_TOKEN_MASK     0x7fffffffUL
#define RADIO_MANAGER_WIFI_WAIT_MS   1000U
#define RADIO_MANAGER_CONFIG_VERSION 1U
static const char *TAG = "radio_manager";

typedef struct {
    uint32_t version;
    uint16_t idle_timeout_seconds;
    uint16_t reserved;
} radio_manager_disk_t;

typedef struct {
    radio_manager_request_t request;
    TaskHandle_t notify_task;
    uint32_t request_id;
    TickType_t expires_at;
} radio_manager_pending_request_t;

typedef enum {
    RADIO_MANAGER_CONTROL_RELEASE = 0,
    RADIO_MANAGER_CONTROL_CANCEL,
} radio_manager_control_type_t;

typedef struct {
    radio_manager_control_type_t type;
    radio_manager_client_t client;
    uint32_t token;
} radio_manager_control_msg_t;

typedef struct {
    radio_manager_pending_request_t pending;
    uint32_t token;
    TickType_t granted_at;
    bool active;
} radio_manager_owner_t;

static QueueHandle_t s_request_queue;
static QueueHandle_t s_control_queue;
static TaskHandle_t s_task_handle;
static uint32_t s_next_request_id = 1;
static portMUX_TYPE s_request_id_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_idle_timeout_loaded;
static uint16_t s_idle_timeout_seconds = CONFIG_RADIO_MANAGER_IDLE_TIMEOUT_MS / 1000U;

static esp_err_t radio_manager_load_idle_timeout_blob(uint16_t *timeout_seconds)
{
    nvs_handle_t handle = 0;
    radio_manager_disk_t disk = { 0 };
    size_t disk_size = sizeof(disk);

    ESP_RETURN_ON_FALSE(timeout_seconds != NULL, ESP_ERR_INVALID_ARG, TAG, "timeout pointer is null");

    esp_err_t err = nvs_open_descriptor(NVS_KEY_RADIO_MANAGER_CONFIG.ns, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_get_blob(handle, NVS_KEY_RADIO_MANAGER_CONFIG.key, &disk, &disk_size);
    nvs_close(handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_NVS_INVALID_LENGTH) {
            nvs_health_report_invalid(&NVS_KEY_RADIO_MANAGER_CONFIG, err, "invalid radio manager blob length");
        }
        return err;
    }
    if (disk_size != sizeof(disk) || disk.version != RADIO_MANAGER_CONFIG_VERSION) {
        nvs_health_report_invalid(&NVS_KEY_RADIO_MANAGER_CONFIG, ESP_ERR_INVALID_VERSION, "invalid radio manager blob");
        return ESP_ERR_INVALID_VERSION;
    }

    *timeout_seconds = disk.idle_timeout_seconds;
    return ESP_OK;
}

static esp_err_t radio_manager_write_idle_timeout_blob(uint16_t timeout_seconds)
{
    nvs_handle_t handle = 0;
    radio_manager_disk_t disk = {
        .version = RADIO_MANAGER_CONFIG_VERSION,
        .idle_timeout_seconds = timeout_seconds,
    };

    ESP_RETURN_ON_ERROR(nvs_open_descriptor(NVS_KEY_RADIO_MANAGER_CONFIG.ns, NVS_READWRITE, &handle),
                        TAG,
                        "open NVS failed");
    esp_err_t err = nvs_set_blob(handle, NVS_KEY_RADIO_MANAGER_CONFIG.key, &disk, sizeof(disk));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

static esp_err_t radio_manager_load_idle_timeout_if_needed(void)
{
    portENTER_CRITICAL(&s_request_id_lock);
    bool loaded = s_idle_timeout_loaded;
    portEXIT_CRITICAL(&s_request_id_lock);
    if (loaded) {
        return ESP_OK;
    }

    uint16_t timeout_seconds = CONFIG_RADIO_MANAGER_IDLE_TIMEOUT_MS / 1000U;
    esp_err_t err = radio_manager_load_idle_timeout_blob(&timeout_seconds);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        return err;
    }

    portENTER_CRITICAL(&s_request_id_lock);
    if (!s_idle_timeout_loaded) {
        s_idle_timeout_seconds = timeout_seconds;
        s_idle_timeout_loaded = true;
    }
    portEXIT_CRITICAL(&s_request_id_lock);
    return ESP_OK;
}

static TickType_t radio_manager_idle_timeout_wait_ticks(void)
{
    uint16_t timeout_seconds = radio_manager_get_idle_timeout_seconds();
    return timeout_seconds == 0
        ? portMAX_DELAY
        : pdMS_TO_TICKS((uint32_t)timeout_seconds * 1000U);
}

static uint32_t radio_manager_next_request_id(void)
{
    uint32_t id = 0;
    portENTER_CRITICAL(&s_request_id_lock);
    id = s_next_request_id++;
    if (s_next_request_id == 0 || (s_next_request_id & RADIO_MANAGER_NOTIFY_GRANTED) != 0) {
        s_next_request_id = 1;
    }
    portEXIT_CRITICAL(&s_request_id_lock);
    return id;
}

static bool radio_manager_tick_reached(TickType_t now, TickType_t target)
{
    return (TickType_t)(now - target) < (TickType_t)(1U << ((sizeof(TickType_t) * 8U) - 1U));
}

static bool radio_manager_request_expired(const radio_manager_pending_request_t *pending)
{
    return radio_manager_tick_reached(xTaskGetTickCount(), pending->expires_at);
}

static esp_err_t radio_manager_prepare_internet(bool *wifi_acquired)
{
    if (!*wifi_acquired) {
        ESP_RETURN_ON_ERROR(wifi_connection_acquire(WIFI_CONNECTION_USER_RADIO_MANAGER),
                            TAG,
                            "Wi-Fi acquire failed");
        *wifi_acquired = true;
    }

    while (true) {
        wifi_connection_state_t state = WIFI_CONNECTION_STATE_STOPPED;
        if (wifi_connection_get_state(&state) != ESP_OK) {
            return ESP_ERR_INVALID_STATE;
        }
        if (state == WIFI_CONNECTION_STATE_CONNECTED) {
            return ESP_OK;
        }
        if (state == WIFI_CONNECTION_STATE_FAILED ||
            state == WIFI_CONNECTION_STATE_SETUP_REQUIRED ||
            state == WIFI_CONNECTION_STATE_SETUP_RUNNING ||
            state == WIFI_CONNECTION_STATE_STOPPED) {
            return ESP_FAIL;
        }
        if (wifi_connection_wait_connected(pdMS_TO_TICKS(RADIO_MANAGER_WIFI_WAIT_MS)) == ESP_ERR_INVALID_STATE) {
            vTaskDelay(pdMS_TO_TICKS(RADIO_MANAGER_WIFI_WAIT_MS));
        }
    }
}

static esp_err_t radio_manager_prepare_for_request(const radio_manager_pending_request_t *pending,
                                                   bool *wifi_acquired)
{
    if ((pending->request.required & RADIO_MANAGER_CAP_INTERNET) != 0) {
        return radio_manager_prepare_internet(wifi_acquired);
    }
    return ESP_ERR_NOT_SUPPORTED;
}

static void radio_manager_release_wifi_if_idle(bool *wifi_acquired)
{
    if (!*wifi_acquired) {
        return;
    }

    esp_err_t err = wifi_connection_release(WIFI_CONNECTION_USER_RADIO_MANAGER);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi release failed: %s", esp_err_to_name(err));
    }
    *wifi_acquired = false;
}

static void radio_manager_grant_owner(radio_manager_owner_t *owner,
                                      const radio_manager_pending_request_t *pending)
{
    owner->pending = *pending;
    owner->token = pending->request_id & RADIO_MANAGER_TOKEN_MASK;
    owner->granted_at = xTaskGetTickCount();
    owner->active = true;

    xTaskNotify(pending->notify_task,
                RADIO_MANAGER_NOTIFY_GRANTED | owner->token,
                eSetValueWithOverwrite);
}

static void radio_manager_reject_request(const radio_manager_pending_request_t *pending)
{
    if (pending == NULL || pending->notify_task == NULL) {
        return;
    }

    (void)xTaskNotify(pending->notify_task,
                      pending->request_id & RADIO_MANAGER_TOKEN_MASK,
                      eSetValueWithOverwrite);
}

static bool radio_manager_control_matches_owner(const radio_manager_owner_t *owner,
                                                const radio_manager_control_msg_t *msg)
{
    return owner->active &&
           owner->token == msg->token &&
           owner->pending.request.client == msg->client;
}

static void radio_manager_wait_owner_release(radio_manager_owner_t *owner)
{
    while (owner->active) {
        APP_STACK_MONITOR_CHECK(TAG, "radio_manager", 30000);

        TickType_t wait_ticks = portMAX_DELAY;
        if (owner->pending.request.max_hold_ticks > 0) {
            TickType_t deadline = owner->granted_at + owner->pending.request.max_hold_ticks;
            TickType_t now = xTaskGetTickCount();
            if (radio_manager_tick_reached(now, deadline)) {
                ESP_LOGW(TAG,
                         "radio lease timed out: client=%d token=%u",
                         (int)owner->pending.request.client,
                         (unsigned)owner->token);
                owner->active = false;
                return;
            }
            wait_ticks = deadline - now;
        }

        radio_manager_control_msg_t msg = { 0 };
        if (xQueueReceive(s_control_queue, &msg, wait_ticks) != pdTRUE) {
            continue;
        }

        if (!radio_manager_control_matches_owner(owner, &msg)) {
            continue;
        }

        if (msg.type == RADIO_MANAGER_CONTROL_RELEASE) {
            owner->active = false;
        } else if (msg.type == RADIO_MANAGER_CONTROL_CANCEL) {
            ESP_LOGW(TAG,
                     "radio lease canceled after grant: client=%d token=%u",
                     (int)msg.client,
                     (unsigned)msg.token);
            owner->active = false;
        }
    }
}

static void radio_manager_task(void *arg)
{
    (void)arg;

    bool wifi_acquired = false;
    radio_manager_owner_t owner = { 0 };

    while (true) {
        APP_STACK_MONITOR_CHECK(TAG, "radio_manager", 30000);

        radio_manager_pending_request_t pending = { 0 };
        if (xQueueReceive(s_request_queue,
                          &pending,
                          radio_manager_idle_timeout_wait_ticks()) != pdTRUE) {
            radio_manager_release_wifi_if_idle(&wifi_acquired);
            continue;
        }

        if (radio_manager_request_expired(&pending)) {
            ESP_LOGW(TAG,
                     "radio request expired before grant: client=%d id=%u",
                     (int)pending.request.client,
                     (unsigned)pending.request_id);
            continue;
        }

        esp_err_t err = radio_manager_prepare_for_request(&pending, &wifi_acquired);
        if (err != ESP_OK) {
            ESP_LOGW(TAG,
                     "radio request prepare failed: client=%d id=%u err=%s",
                     (int)pending.request.client,
                     (unsigned)pending.request_id,
                     esp_err_to_name(err));
            if (!radio_manager_request_expired(&pending)) {
                radio_manager_reject_request(&pending);
            }
            radio_manager_release_wifi_if_idle(&wifi_acquired);
            continue;
        }

        if (radio_manager_request_expired(&pending)) {
            ESP_LOGW(TAG,
                     "radio request expired after prepare: client=%d id=%u",
                     (int)pending.request.client,
                     (unsigned)pending.request_id);
            continue;
        }

        radio_manager_grant_owner(&owner, &pending);
        radio_manager_wait_owner_release(&owner);
    }
}

esp_err_t radio_manager_start(void)
{
    if (s_request_queue == NULL) {
        s_request_queue = xQueueCreate(CONFIG_RADIO_MANAGER_REQUEST_QUEUE_LEN,
                                       sizeof(radio_manager_pending_request_t));
        ESP_RETURN_ON_FALSE(s_request_queue != NULL, ESP_ERR_NO_MEM, TAG, "request queue alloc failed");
    }
    if (s_control_queue == NULL) {
        s_control_queue = xQueueCreate(CONFIG_RADIO_MANAGER_CONTROL_QUEUE_LEN,
                                       sizeof(radio_manager_control_msg_t));
        ESP_RETURN_ON_FALSE(s_control_queue != NULL, ESP_ERR_NO_MEM, TAG, "control queue alloc failed");
    }
    if (s_task_handle != NULL) {
        return ESP_OK;
    }

    BaseType_t task_ok = xTaskCreate(radio_manager_task,
                                     "radio_manager",
                                     CONFIG_RADIO_MANAGER_TASK_STACK_SIZE,
                                     NULL,
                                     CONFIG_RADIO_MANAGER_TASK_PRIORITY,
                                     &s_task_handle);
    ESP_RETURN_ON_FALSE(task_ok == pdPASS, ESP_ERR_NO_MEM, TAG, "task create failed");
    return ESP_OK;
}

uint16_t radio_manager_get_idle_timeout_seconds(void)
{
    esp_err_t err = radio_manager_load_idle_timeout_if_needed();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "load idle timeout failed: %s", esp_err_to_name(err));
    }

    portENTER_CRITICAL(&s_request_id_lock);
    uint16_t timeout_seconds = s_idle_timeout_seconds;
    portEXIT_CRITICAL(&s_request_id_lock);
    return timeout_seconds;
}

esp_err_t radio_manager_set_idle_timeout_seconds(uint16_t timeout_seconds)
{
    ESP_RETURN_ON_ERROR(radio_manager_load_idle_timeout_if_needed(), TAG, "load idle timeout failed");
    portENTER_CRITICAL(&s_request_id_lock);
    s_idle_timeout_seconds = timeout_seconds;
    portEXIT_CRITICAL(&s_request_id_lock);
    return ESP_OK;
}

esp_err_t radio_manager_save_idle_timeout_seconds(void)
{
    ESP_RETURN_ON_ERROR(radio_manager_load_idle_timeout_if_needed(), TAG, "load idle timeout failed");
    uint16_t timeout_seconds = radio_manager_get_idle_timeout_seconds();
    return radio_manager_write_idle_timeout_blob(timeout_seconds);
}

esp_err_t radio_manager_acquire(const radio_manager_request_t *request,
                                radio_manager_lease_t *lease,
                                TickType_t wait_ticks)
{
    ESP_RETURN_ON_FALSE(request != NULL, ESP_ERR_INVALID_ARG, TAG, "request is null");
    ESP_RETURN_ON_FALSE(lease != NULL, ESP_ERR_INVALID_ARG, TAG, "lease is null");
    ESP_RETURN_ON_FALSE(s_request_queue != NULL, ESP_ERR_INVALID_STATE, TAG, "manager not started");
    ESP_RETURN_ON_FALSE(request->required != 0, ESP_ERR_INVALID_ARG, TAG, "required capability is empty");

    uint32_t request_id = radio_manager_next_request_id();
    radio_manager_pending_request_t pending = {
        .request = *request,
        .notify_task = xTaskGetCurrentTaskHandle(),
        .request_id = request_id,
        .expires_at = xTaskGetTickCount() + wait_ticks,
    };

    uint32_t notify_value = 0;
    (void)xTaskNotifyWait(0, UINT32_MAX, &notify_value, 0);

    if (xQueueSend(s_request_queue, &pending, wait_ticks) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    if (xTaskNotifyWait(0, UINT32_MAX, &notify_value, wait_ticks) != pdTRUE) {
        radio_manager_control_msg_t cancel = {
            .type = RADIO_MANAGER_CONTROL_CANCEL,
            .client = request->client,
            .token = request_id & RADIO_MANAGER_TOKEN_MASK,
        };
        (void)xQueueSend(s_control_queue, &cancel, 0);
        return ESP_ERR_TIMEOUT;
    }

    if ((notify_value & RADIO_MANAGER_NOTIFY_GRANTED) == 0) {
        return ESP_FAIL;
    }

    lease->client = request->client;
    lease->token = notify_value & RADIO_MANAGER_TOKEN_MASK;
    return ESP_OK;
}

esp_err_t radio_manager_release(const radio_manager_lease_t *lease)
{
    ESP_RETURN_ON_FALSE(lease != NULL, ESP_ERR_INVALID_ARG, TAG, "lease is null");
    ESP_RETURN_ON_FALSE(s_control_queue != NULL, ESP_ERR_INVALID_STATE, TAG, "manager not started");

    radio_manager_control_msg_t msg = {
        .type = RADIO_MANAGER_CONTROL_RELEASE,
        .client = lease->client,
        .token = lease->token,
    };
    return xQueueSend(s_control_queue, &msg, pdMS_TO_TICKS(100)) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}
