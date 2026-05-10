#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_log.h"
#include "time_tick.h"

#ifndef CONFIG_TIME_TICK_TASK_STACK_SIZE
#define CONFIG_TIME_TICK_TASK_STACK_SIZE 3072
#endif
#ifndef CONFIG_TIME_TICK_TASK_PRIORITY
#define CONFIG_TIME_TICK_TASK_PRIORITY 4
#endif

#define TIME_TICK_TASK_POLL_MS 50U
#define TIME_TICK_QUEUE_LENGTH 1U
#define TIME_TICK_MAX_SUBSCRIBERS 8U
#define TIME_TICK_VALID_YEAR 2024

static const char *TAG = "time_tick";
static TaskHandle_t s_time_tick_task_handle;
static portMUX_TYPE s_time_tick_lock = portMUX_INITIALIZER_UNLOCKED;
static QueueHandle_t s_time_tick_subscribers[TIME_TICK_MAX_SUBSCRIBERS];
static time_t s_time_tick_last_epoch_sec;
static time_tick_event_t s_time_tick_latest_event;
static bool s_time_tick_has_latest;
static bool s_time_tick_started;

static bool time_tick_valid(const struct tm *local_time)
{
    return local_time != NULL && (local_time->tm_year + 1900) >= TIME_TICK_VALID_YEAR;
}

static void time_tick_publish(const time_tick_event_t *event)
{
    if (event == NULL) {
        return;
    }

    portENTER_CRITICAL(&s_time_tick_lock);
    s_time_tick_latest_event = *event;
    s_time_tick_has_latest = true;
    for (size_t i = 0; i < TIME_TICK_MAX_SUBSCRIBERS; ++i) {
        if (s_time_tick_subscribers[i] != NULL) {
            (void)xQueueOverwrite(s_time_tick_subscribers[i], event);
        }
    }
    portEXIT_CRITICAL(&s_time_tick_lock);
}

static void time_tick_task(void *arg)
{
    (void)arg;

    while (true) {
        time_t now = 0;
        struct tm local_time = { 0 };

        time(&now);
        localtime_r(&now, &local_time);

        if (!s_time_tick_has_latest || now != s_time_tick_last_epoch_sec) {
            int64_t delta = s_time_tick_has_latest ? (int64_t)now - (int64_t)s_time_tick_last_epoch_sec : 0;
            time_tick_event_t event = {
                .epoch_sec = now,
                .local_time = local_time,
                .valid = time_tick_valid(&local_time),
                .jumped = s_time_tick_has_latest && delta != 1,
                .delta_sec = (delta > INT32_MAX) ? INT32_MAX : (delta < INT32_MIN) ? INT32_MIN : (int32_t)delta,
            };

            s_time_tick_last_epoch_sec = now;
            time_tick_publish(&event);
        }

        vTaskDelay(pdMS_TO_TICKS(TIME_TICK_TASK_POLL_MS));
    }
}

esp_err_t time_tick_start(void)
{
    if (s_time_tick_started) {
        return ESP_OK;
    }

    portENTER_CRITICAL(&s_time_tick_lock);
    memset(s_time_tick_subscribers, 0, sizeof(s_time_tick_subscribers));
    s_time_tick_last_epoch_sec = 0;
    s_time_tick_has_latest = false;
    portEXIT_CRITICAL(&s_time_tick_lock);

    BaseType_t task_ok = xTaskCreate(time_tick_task,
                                     "time_tick",
                                     CONFIG_TIME_TICK_TASK_STACK_SIZE,
                                     NULL,
                                     CONFIG_TIME_TICK_TASK_PRIORITY,
                                     &s_time_tick_task_handle);
    ESP_RETURN_ON_FALSE(task_ok == pdPASS, ESP_ERR_NO_MEM, TAG, "task create failed");

    s_time_tick_started = true;
    return ESP_OK;
}

esp_err_t time_tick_subscribe(QueueHandle_t *queue_out)
{
    QueueHandle_t queue = NULL;
    bool added = false;

    ESP_RETURN_ON_FALSE(queue_out != NULL, ESP_ERR_INVALID_ARG, TAG, "queue out is null");
    ESP_RETURN_ON_FALSE(s_time_tick_started, ESP_ERR_INVALID_STATE, TAG, "time tick not started");

    queue = xQueueCreate(TIME_TICK_QUEUE_LENGTH, sizeof(time_tick_event_t));
    ESP_RETURN_ON_FALSE(queue != NULL, ESP_ERR_NO_MEM, TAG, "queue alloc failed");

    portENTER_CRITICAL(&s_time_tick_lock);
    for (size_t i = 0; i < TIME_TICK_MAX_SUBSCRIBERS; ++i) {
        if (s_time_tick_subscribers[i] == NULL) {
            s_time_tick_subscribers[i] = queue;
            if (s_time_tick_has_latest) {
                (void)xQueueOverwrite(queue, &s_time_tick_latest_event);
            }
            added = true;
            break;
        }
    }
    portEXIT_CRITICAL(&s_time_tick_lock);

    if (!added) {
        vQueueDelete(queue);
        return ESP_ERR_NO_MEM;
    }

    *queue_out = queue;
    return ESP_OK;
}

esp_err_t time_tick_unsubscribe(QueueHandle_t queue)
{
    bool removed = false;

    ESP_RETURN_ON_FALSE(queue != NULL, ESP_ERR_INVALID_ARG, TAG, "queue is null");

    portENTER_CRITICAL(&s_time_tick_lock);
    for (size_t i = 0; i < TIME_TICK_MAX_SUBSCRIBERS; ++i) {
        if (s_time_tick_subscribers[i] == queue) {
            s_time_tick_subscribers[i] = NULL;
            removed = true;
            break;
        }
    }
    portEXIT_CRITICAL(&s_time_tick_lock);

    ESP_RETURN_ON_FALSE(removed, ESP_ERR_NOT_FOUND, TAG, "subscriber not found");
    vQueueDelete(queue);
    return ESP_OK;
}

bool time_tick_get_latest(time_tick_event_t *event)
{
    bool has_latest = false;

    if (event == NULL) {
        return false;
    }

    portENTER_CRITICAL(&s_time_tick_lock);
    has_latest = s_time_tick_has_latest;
    if (has_latest) {
        *event = s_time_tick_latest_event;
    }
    portEXIT_CRITICAL(&s_time_tick_lock);

    return has_latest;
}
