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
#include "nvs.h"
#include "nvs_health.h"
#include "nvs_schema.h"
#include "app_scheduler.h"
#include "time_tick.h"

#ifndef CONFIG_APP_SCHEDULER_TASK_STACK_SIZE
#define CONFIG_APP_SCHEDULER_TASK_STACK_SIZE 4096
#endif
#ifndef CONFIG_APP_SCHEDULER_TASK_PRIORITY
#define CONFIG_APP_SCHEDULER_TASK_PRIORITY 4
#endif

#define APP_SCHEDULER_EVENT_QUEUE_LEN 16U
#define APP_SCHEDULER_CONFIG_VERSION 2U

static const char *TAG = "app_scheduler";
typedef struct {
    bool occupied;
    app_scheduler_config_t config;
    app_scheduler_state_t state;
    uint32_t fired_count;
    uint32_t missed_event_count;
    time_t last_event_at;
    int64_t last_event_stamp;
} app_scheduler_entry_t;

typedef struct {
    uint32_t version;
    app_scheduler_config_t configs[APP_SCHEDULER_MAX_ENTRIES];
    uint8_t occupied[APP_SCHEDULER_MAX_ENTRIES];
} app_scheduler_disk_t;

typedef struct {
    bool occupied;
    char owner[APP_SCHEDULER_OWNER_MAX_LEN + 1];
    app_scheduler_event_handler_t handler;
    void *ctx;
} app_scheduler_handler_entry_t;

static portMUX_TYPE s_scheduler_lock = portMUX_INITIALIZER_UNLOCKED;
static app_scheduler_entry_t s_entries[APP_SCHEDULER_MAX_ENTRIES];
static app_scheduler_handler_entry_t s_handlers[APP_SCHEDULER_MAX_ENTRIES];
static QueueHandle_t s_event_queue;
static QueueHandle_t s_tick_queue;
static TaskHandle_t s_scheduler_task_handle;
static bool s_scheduler_started;

static uint8_t app_scheduler_normalize_weekday_mask(uint8_t weekday_mask)
{
    weekday_mask &= APP_SCHEDULER_WEEKDAY_ALL;
    return weekday_mask == 0 ? APP_SCHEDULER_WEEKDAY_ALL : weekday_mask;
}

static bool app_scheduler_valid_name(const char *name)
{
    return name != NULL && name[0] != '\0';
}

static bool app_scheduler_valid_time(app_scheduler_time_of_day_t time_of_day)
{
    return time_of_day.hour <= 23U && time_of_day.minute <= 59U && time_of_day.second <= 59U;
}

static uint32_t app_scheduler_seconds_of_day(app_scheduler_time_of_day_t time_of_day)
{
    return ((uint32_t)time_of_day.hour * 3600U) +
           ((uint32_t)time_of_day.minute * 60U) +
           (uint32_t)time_of_day.second;
}

static int64_t app_scheduler_second_stamp(const struct tm *timeinfo)
{
    if (timeinfo == NULL) {
        return -1;
    }

    return ((int64_t)timeinfo->tm_year * 366 * 24 * 60 * 60) +
           ((int64_t)timeinfo->tm_yday * 24 * 60 * 60) +
           ((int64_t)timeinfo->tm_hour * 60 * 60) +
           ((int64_t)timeinfo->tm_min * 60) +
           (int64_t)timeinfo->tm_sec;
}

static bool app_scheduler_entry_matches(const app_scheduler_entry_t *entry, const char *owner, const char *tag)
{
    if (entry == NULL || owner == NULL || tag == NULL || !entry->occupied) {
        return false;
    }

    return strcmp(entry->config.owner, owner) == 0 && strcmp(entry->config.tag, tag) == 0;
}

static app_scheduler_state_t app_scheduler_initial_state(const app_scheduler_config_t *config)
{
    if (config == NULL || !config->enabled) {
        return APP_SCHEDULER_STATE_DISABLED;
    }
    return APP_SCHEDULER_STATE_WAITING;
}

static esp_err_t app_scheduler_validate_config(const app_scheduler_config_t *config)
{
    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG, "config is null");
    ESP_RETURN_ON_FALSE(app_scheduler_valid_name(config->owner), ESP_ERR_INVALID_ARG, TAG, "owner is required");
    ESP_RETURN_ON_FALSE(app_scheduler_valid_name(config->tag), ESP_ERR_INVALID_ARG, TAG, "tag is required");
    ESP_RETURN_ON_FALSE(app_scheduler_valid_time(config->at), ESP_ERR_INVALID_ARG, TAG, "invalid at time");
    ESP_RETURN_ON_FALSE(config->mode == APP_SCHEDULER_MODE_INSTANT ||
                            config->mode == APP_SCHEDULER_MODE_WINDOW,
                        ESP_ERR_INVALID_ARG,
                        TAG,
                        "invalid mode");
    ESP_RETURN_ON_FALSE(config->behavior == APP_SCHEDULER_BEHAVIOR_EVENT ||
                            config->behavior == APP_SCHEDULER_BEHAVIOR_LATCHED,
                        ESP_ERR_INVALID_ARG,
                        TAG,
                        "invalid behavior");
    ESP_RETURN_ON_FALSE(config->mode == APP_SCHEDULER_MODE_INSTANT ||
                            config->behavior == APP_SCHEDULER_BEHAVIOR_EVENT,
                        ESP_ERR_INVALID_ARG,
                        TAG,
                        "window schedules only support event behavior");

    if (config->mode == APP_SCHEDULER_MODE_WINDOW) {
        ESP_RETURN_ON_FALSE(app_scheduler_valid_time(config->to), ESP_ERR_INVALID_ARG, TAG, "invalid to time");
        ESP_RETURN_ON_FALSE(app_scheduler_seconds_of_day(config->at) < app_scheduler_seconds_of_day(config->to),
                            ESP_ERR_INVALID_ARG,
                            TAG,
                            "overnight windows are not supported yet");
    }

    return ESP_OK;
}

static void app_scheduler_make_event_locked(size_t index,
                                            app_scheduler_event_type_t type,
                                            time_t occurred_at,
                                            app_scheduler_event_t *event)
{
    ESP_RETURN_VOID_ON_FALSE(index < APP_SCHEDULER_MAX_ENTRIES, TAG, "invalid index");
    ESP_RETURN_VOID_ON_FALSE(event != NULL, TAG, "event is null");

    memset(event, 0, sizeof(*event));
    event->slot_id = (uint8_t)index;
    event->type = type;
    event->occurred_at = occurred_at;
    strlcpy(event->owner, s_entries[index].config.owner, sizeof(event->owner));
    strlcpy(event->tag, s_entries[index].config.tag, sizeof(event->tag));
}

static void app_scheduler_publish_event(size_t index, app_scheduler_event_t *event)
{
    bool missed = false;

    if (event == NULL || s_event_queue == NULL) {
        missed = true;
    } else if (xQueueSend(s_event_queue, event, 0) != pdTRUE) {
        missed = true;
    }

    if (missed && index < APP_SCHEDULER_MAX_ENTRIES) {
        portENTER_CRITICAL(&s_scheduler_lock);
        ++s_entries[index].missed_event_count;
        portEXIT_CRITICAL(&s_scheduler_lock);
        ESP_LOGW(TAG, "event queue full: slot=%u", (unsigned)index);
    }
}

static void app_scheduler_dispatch_event(const app_scheduler_event_t *event)
{
    app_scheduler_event_handler_t handler = NULL;
    void *ctx = NULL;

    if (event == NULL) {
        return;
    }

    portENTER_CRITICAL(&s_scheduler_lock);
    for (size_t i = 0; i < APP_SCHEDULER_MAX_ENTRIES; ++i) {
        if (s_handlers[i].occupied && strcmp(s_handlers[i].owner, event->owner) == 0) {
            handler = s_handlers[i].handler;
            ctx = s_handlers[i].ctx;
            break;
        }
    }
    portEXIT_CRITICAL(&s_scheduler_lock);

    if (handler != NULL) {
        handler(event, ctx);
    }
}

static esp_err_t app_scheduler_write_configs(void)
{
    nvs_handle_t nvs_handle;
    app_scheduler_disk_t disk = {
        .version = APP_SCHEDULER_CONFIG_VERSION,
    };

    portENTER_CRITICAL(&s_scheduler_lock);
    for (size_t i = 0; i < APP_SCHEDULER_MAX_ENTRIES; ++i) {
        disk.occupied[i] = s_entries[i].occupied ? 1U : 0U;
        disk.configs[i] = s_entries[i].config;
    }
    portEXIT_CRITICAL(&s_scheduler_lock);

    ESP_RETURN_ON_ERROR(nvs_open_descriptor(NVS_KEY_APP_SCHEDULER_CONFIG.ns, NVS_READWRITE, &nvs_handle),
                        TAG,
                        "open NVS failed");
    esp_err_t err = nvs_set_blob(nvs_handle, NVS_KEY_APP_SCHEDULER_CONFIG.key, &disk, sizeof(disk));
    if (err == ESP_OK) {
        err = nvs_commit(nvs_handle);
    }
    nvs_close(nvs_handle);
    return err;
}

static esp_err_t app_scheduler_load_configs(void)
{
    nvs_handle_t nvs_handle;
    app_scheduler_disk_t disk = { 0 };
    size_t disk_size = sizeof(disk);

    esp_err_t err = nvs_open_descriptor(NVS_KEY_APP_SCHEDULER_CONFIG.ns, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_get_blob(nvs_handle, NVS_KEY_APP_SCHEDULER_CONFIG.key, &disk, &disk_size);
    nvs_close(nvs_handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_NVS_INVALID_LENGTH) {
            nvs_health_report_invalid(&NVS_KEY_APP_SCHEDULER_CONFIG, err, "invalid scheduler blob length");
        }
        return err;
    }
    if (disk_size != sizeof(disk) || disk.version != APP_SCHEDULER_CONFIG_VERSION) {
        nvs_health_report_invalid(&NVS_KEY_APP_SCHEDULER_CONFIG, ESP_ERR_INVALID_VERSION, "invalid scheduler blob");
        return ESP_ERR_INVALID_VERSION;
    }

    for (size_t i = 0; i < APP_SCHEDULER_MAX_ENTRIES; ++i) {
        if (disk.occupied[i] == 0) {
            continue;
        }
        disk.configs[i].owner[APP_SCHEDULER_OWNER_MAX_LEN] = '\0';
        disk.configs[i].tag[APP_SCHEDULER_TAG_MAX_LEN] = '\0';
        disk.configs[i].weekday_mask = app_scheduler_normalize_weekday_mask(disk.configs[i].weekday_mask);
        if (app_scheduler_validate_config(&disk.configs[i]) != ESP_OK) {
            nvs_health_report_invalid(&NVS_KEY_APP_SCHEDULER_CONFIG, ESP_ERR_INVALID_ARG, "invalid scheduler entry");
            return ESP_ERR_INVALID_ARG;
        }
    }

    portENTER_CRITICAL(&s_scheduler_lock);
    memset(s_entries, 0, sizeof(s_entries));
    for (size_t i = 0; i < APP_SCHEDULER_MAX_ENTRIES; ++i) {
        if (disk.occupied[i] == 0) {
            continue;
        }
        s_entries[i].occupied = true;
        s_entries[i].config = disk.configs[i];
        s_entries[i].config.owner[APP_SCHEDULER_OWNER_MAX_LEN] = '\0';
        s_entries[i].config.tag[APP_SCHEDULER_TAG_MAX_LEN] = '\0';
        s_entries[i].config.weekday_mask = app_scheduler_normalize_weekday_mask(s_entries[i].config.weekday_mask);
        s_entries[i].state = app_scheduler_initial_state(&s_entries[i].config);
        s_entries[i].last_event_stamp = -1;
    }
    portEXIT_CRITICAL(&s_scheduler_lock);
    return ESP_OK;
}

static bool app_scheduler_weekday_matches(const app_scheduler_config_t *config, const struct tm *timeinfo)
{
    if (config == NULL || timeinfo == NULL) {
        return false;
    }
    if (!config->repeat) {
        return true;
    }
    return (app_scheduler_normalize_weekday_mask(config->weekday_mask) & (1U << timeinfo->tm_wday)) != 0;
}

static bool app_scheduler_in_window(const app_scheduler_config_t *config, const struct tm *timeinfo)
{
    uint32_t now_seconds = 0;
    uint32_t at_seconds = 0;
    uint32_t to_seconds = 0;

    if (config == NULL || timeinfo == NULL || config->mode != APP_SCHEDULER_MODE_WINDOW) {
        return false;
    }
    if (!app_scheduler_weekday_matches(config, timeinfo)) {
        return false;
    }

    now_seconds = ((uint32_t)timeinfo->tm_hour * 3600U) +
                  ((uint32_t)timeinfo->tm_min * 60U) +
                  (uint32_t)timeinfo->tm_sec;
    at_seconds = app_scheduler_seconds_of_day(config->at);
    to_seconds = app_scheduler_seconds_of_day(config->to);
    return now_seconds >= at_seconds && now_seconds < to_seconds;
}

static bool app_scheduler_instant_matches(const app_scheduler_config_t *config, const struct tm *timeinfo)
{
    if (config == NULL || timeinfo == NULL || config->mode != APP_SCHEDULER_MODE_INSTANT) {
        return false;
    }
    if (!app_scheduler_weekday_matches(config, timeinfo)) {
        return false;
    }

    return timeinfo->tm_hour == config->at.hour &&
           timeinfo->tm_min == config->at.minute &&
           timeinfo->tm_sec == config->at.second;
}

static void app_scheduler_process_entry(size_t index, const struct tm *timeinfo, time_t now, int64_t stamp)
{
    app_scheduler_event_t event = { 0 };
    bool publish = false;
    bool persist = false;

    portENTER_CRITICAL(&s_scheduler_lock);
    if (index >= APP_SCHEDULER_MAX_ENTRIES ||
        !s_entries[index].occupied ||
        !s_entries[index].config.enabled) {
        if (index < APP_SCHEDULER_MAX_ENTRIES && s_entries[index].occupied) {
            s_entries[index].state = APP_SCHEDULER_STATE_DISABLED;
        }
        portEXIT_CRITICAL(&s_scheduler_lock);
        return;
    }

    if (s_entries[index].config.mode == APP_SCHEDULER_MODE_INSTANT) {
        if (app_scheduler_instant_matches(&s_entries[index].config, timeinfo) &&
            (s_entries[index].config.behavior == APP_SCHEDULER_BEHAVIOR_EVENT ||
             s_entries[index].state == APP_SCHEDULER_STATE_WAITING) &&
            s_entries[index].last_event_stamp != stamp) {
            s_entries[index].last_event_stamp = stamp;
            s_entries[index].last_event_at = now;
            ++s_entries[index].fired_count;
            if (s_entries[index].config.behavior == APP_SCHEDULER_BEHAVIOR_LATCHED) {
                s_entries[index].state = APP_SCHEDULER_STATE_ACTIVE;
                app_scheduler_make_event_locked(index, APP_SCHEDULER_EVENT_STARTED, now, &event);
            } else {
                app_scheduler_make_event_locked(index, APP_SCHEDULER_EVENT_FIRED, now, &event);
            }
            publish = true;

            if (s_entries[index].config.behavior == APP_SCHEDULER_BEHAVIOR_EVENT &&
                s_entries[index].config.repeat) {
                s_entries[index].state = APP_SCHEDULER_STATE_WAITING;
            } else if (s_entries[index].config.behavior == APP_SCHEDULER_BEHAVIOR_EVENT) {
                s_entries[index].config.enabled = false;
                s_entries[index].state = APP_SCHEDULER_STATE_DISABLED;
                persist = true;
            }
        } else if (s_entries[index].state == APP_SCHEDULER_STATE_STOPPED &&
                   !app_scheduler_instant_matches(&s_entries[index].config, timeinfo)) {
            if (s_entries[index].config.repeat) {
                s_entries[index].state = APP_SCHEDULER_STATE_WAITING;
            } else {
                s_entries[index].config.enabled = false;
                s_entries[index].state = APP_SCHEDULER_STATE_DISABLED;
                persist = true;
            }
        } else if (s_entries[index].state != APP_SCHEDULER_STATE_ACTIVE &&
                   s_entries[index].state != APP_SCHEDULER_STATE_STOPPED) {
            s_entries[index].state = APP_SCHEDULER_STATE_WAITING;
        }
    } else if (s_entries[index].config.mode == APP_SCHEDULER_MODE_WINDOW) {
        bool in_window = app_scheduler_in_window(&s_entries[index].config, timeinfo);
        if (in_window && s_entries[index].state == APP_SCHEDULER_STATE_WAITING) {
            s_entries[index].state = APP_SCHEDULER_STATE_ACTIVE;
            s_entries[index].last_event_stamp = stamp;
            s_entries[index].last_event_at = now;
            ++s_entries[index].fired_count;
            app_scheduler_make_event_locked(index, APP_SCHEDULER_EVENT_STARTED, now, &event);
            publish = true;
        } else if (!in_window &&
                   (s_entries[index].state == APP_SCHEDULER_STATE_ACTIVE ||
                    s_entries[index].state == APP_SCHEDULER_STATE_STOPPED)) {
            s_entries[index].state = APP_SCHEDULER_STATE_WAITING;
            s_entries[index].last_event_stamp = stamp;
            s_entries[index].last_event_at = now;
            app_scheduler_make_event_locked(index, APP_SCHEDULER_EVENT_ENDED, now, &event);
            publish = true;
            if (!s_entries[index].config.repeat) {
                s_entries[index].config.enabled = false;
                s_entries[index].state = APP_SCHEDULER_STATE_DISABLED;
                persist = true;
            }
        } else if (!in_window) {
            s_entries[index].state = APP_SCHEDULER_STATE_WAITING;
        }
    }
    portEXIT_CRITICAL(&s_scheduler_lock);

    if (publish) {
        app_scheduler_publish_event(index, &event);
        app_scheduler_dispatch_event(&event);
        if (persist) {
            (void)app_scheduler_write_configs();
        }
    }
}

static void app_scheduler_task(void *arg)
{
    (void)arg;

    while (true) {
        time_tick_event_t tick = { 0 };

        if (xQueueReceive(s_tick_queue, &tick, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (!tick.valid) {
            continue;
        }

        int64_t stamp = app_scheduler_second_stamp(&tick.local_time);
        for (size_t i = 0; i < APP_SCHEDULER_MAX_ENTRIES; ++i) {
            app_scheduler_process_entry(i, &tick.local_time, tick.epoch_sec, stamp);
        }
    }
}

esp_err_t app_scheduler_init(void)
{
    if (s_scheduler_started) {
        return ESP_OK;
    }

    portENTER_CRITICAL(&s_scheduler_lock);
    memset(s_entries, 0, sizeof(s_entries));
    for (size_t i = 0; i < APP_SCHEDULER_MAX_ENTRIES; ++i) {
        s_entries[i].last_event_stamp = -1;
    }
    portEXIT_CRITICAL(&s_scheduler_lock);

    esp_err_t err = app_scheduler_load_configs();
    if (err != ESP_OK &&
        err != ESP_ERR_NVS_NOT_FOUND &&
        err != ESP_ERR_NVS_INVALID_LENGTH &&
        err != ESP_ERR_INVALID_VERSION &&
        err != ESP_ERR_INVALID_ARG) {
        ESP_RETURN_ON_ERROR(err, TAG, "load scheduler config failed");
    }

    s_event_queue = xQueueCreate(APP_SCHEDULER_EVENT_QUEUE_LEN, sizeof(app_scheduler_event_t));
    ESP_RETURN_ON_FALSE(s_event_queue != NULL, ESP_ERR_NO_MEM, TAG, "event queue alloc failed");
    ESP_RETURN_ON_ERROR(time_tick_subscribe(&s_tick_queue), TAG, "time tick subscribe failed");

    BaseType_t created = xTaskCreate(app_scheduler_task,
                                     "app_scheduler",
                                     CONFIG_APP_SCHEDULER_TASK_STACK_SIZE,
                                     NULL,
                                     CONFIG_APP_SCHEDULER_TASK_PRIORITY,
                                     &s_scheduler_task_handle);
    ESP_RETURN_ON_FALSE(created == pdPASS, ESP_FAIL, TAG, "create scheduler task failed");

    s_scheduler_started = true;
    return ESP_OK;
}

esp_err_t app_scheduler_upsert(const app_scheduler_config_t *config)
{
    size_t target = APP_SCHEDULER_MAX_ENTRIES;
    app_scheduler_config_t normalized = { 0 };

    ESP_RETURN_ON_ERROR(app_scheduler_validate_config(config), TAG, "invalid config");
    normalized = *config;
    normalized.owner[APP_SCHEDULER_OWNER_MAX_LEN] = '\0';
    normalized.tag[APP_SCHEDULER_TAG_MAX_LEN] = '\0';
    normalized.weekday_mask = app_scheduler_normalize_weekday_mask(normalized.weekday_mask);

    portENTER_CRITICAL(&s_scheduler_lock);
    for (size_t i = 0; i < APP_SCHEDULER_MAX_ENTRIES; ++i) {
        if (app_scheduler_entry_matches(&s_entries[i], normalized.owner, normalized.tag)) {
            target = i;
            break;
        }
    }
    if (target == APP_SCHEDULER_MAX_ENTRIES) {
        for (size_t i = 0; i < APP_SCHEDULER_MAX_ENTRIES; ++i) {
            if (!s_entries[i].occupied) {
                target = i;
                break;
            }
        }
    }
    if (target == APP_SCHEDULER_MAX_ENTRIES) {
        portEXIT_CRITICAL(&s_scheduler_lock);
        return ESP_ERR_NO_MEM;
    }

    s_entries[target].occupied = true;
    s_entries[target].config = normalized;
    s_entries[target].state = app_scheduler_initial_state(&normalized);
    s_entries[target].last_event_stamp = -1;
    portEXIT_CRITICAL(&s_scheduler_lock);

    return app_scheduler_write_configs();
}

esp_err_t app_scheduler_remove(const char *owner, const char *tag)
{
    ESP_RETURN_ON_FALSE(app_scheduler_valid_name(owner), ESP_ERR_INVALID_ARG, TAG, "owner is required");
    ESP_RETURN_ON_FALSE(app_scheduler_valid_name(tag), ESP_ERR_INVALID_ARG, TAG, "tag is required");

    portENTER_CRITICAL(&s_scheduler_lock);
    for (size_t i = 0; i < APP_SCHEDULER_MAX_ENTRIES; ++i) {
        if (app_scheduler_entry_matches(&s_entries[i], owner, tag)) {
            memset(&s_entries[i], 0, sizeof(s_entries[i]));
            s_entries[i].last_event_stamp = -1;
            portEXIT_CRITICAL(&s_scheduler_lock);
            return app_scheduler_write_configs();
        }
    }
    portEXIT_CRITICAL(&s_scheduler_lock);
    return ESP_ERR_NOT_FOUND;
}

esp_err_t app_scheduler_set_enabled(const char *owner, const char *tag, bool enabled)
{
    ESP_RETURN_ON_FALSE(app_scheduler_valid_name(owner), ESP_ERR_INVALID_ARG, TAG, "owner is required");
    ESP_RETURN_ON_FALSE(app_scheduler_valid_name(tag), ESP_ERR_INVALID_ARG, TAG, "tag is required");

    portENTER_CRITICAL(&s_scheduler_lock);
    for (size_t i = 0; i < APP_SCHEDULER_MAX_ENTRIES; ++i) {
        if (app_scheduler_entry_matches(&s_entries[i], owner, tag)) {
            s_entries[i].config.enabled = enabled;
            s_entries[i].state = app_scheduler_initial_state(&s_entries[i].config);
            s_entries[i].last_event_stamp = -1;
            portEXIT_CRITICAL(&s_scheduler_lock);
            return app_scheduler_write_configs();
        }
    }
    portEXIT_CRITICAL(&s_scheduler_lock);
    return ESP_ERR_NOT_FOUND;
}

esp_err_t app_scheduler_stop(const char *owner, const char *tag)
{
    app_scheduler_event_t event = { 0 };
    size_t target = APP_SCHEDULER_MAX_ENTRIES;
    time_t now = 0;

    ESP_RETURN_ON_FALSE(app_scheduler_valid_name(owner), ESP_ERR_INVALID_ARG, TAG, "owner is required");
    ESP_RETURN_ON_FALSE(app_scheduler_valid_name(tag), ESP_ERR_INVALID_ARG, TAG, "tag is required");
    time(&now);

    portENTER_CRITICAL(&s_scheduler_lock);
    for (size_t i = 0; i < APP_SCHEDULER_MAX_ENTRIES; ++i) {
        if (app_scheduler_entry_matches(&s_entries[i], owner, tag)) {
            if (s_entries[i].state != APP_SCHEDULER_STATE_ACTIVE) {
                portEXIT_CRITICAL(&s_scheduler_lock);
                return ESP_ERR_INVALID_STATE;
            }
            s_entries[i].state = APP_SCHEDULER_STATE_STOPPED;
            s_entries[i].last_event_at = now;
            if (s_entries[i].config.mode == APP_SCHEDULER_MODE_INSTANT &&
                s_entries[i].config.behavior == APP_SCHEDULER_BEHAVIOR_LATCHED &&
                !s_entries[i].config.repeat) {
                s_entries[i].config.enabled = false;
            }
            app_scheduler_make_event_locked(i, APP_SCHEDULER_EVENT_STOPPED_BY_USER, now, &event);
            target = i;
            break;
        }
    }
    portEXIT_CRITICAL(&s_scheduler_lock);

    if (target == APP_SCHEDULER_MAX_ENTRIES) {
        return ESP_ERR_NOT_FOUND;
    }
    app_scheduler_publish_event(target, &event);
    app_scheduler_dispatch_event(&event);
    return app_scheduler_write_configs();
}

esp_err_t app_scheduler_get_status(const char *owner, const char *tag, app_scheduler_status_t *status)
{
    ESP_RETURN_ON_FALSE(app_scheduler_valid_name(owner), ESP_ERR_INVALID_ARG, TAG, "owner is required");
    ESP_RETURN_ON_FALSE(app_scheduler_valid_name(tag), ESP_ERR_INVALID_ARG, TAG, "tag is required");
    ESP_RETURN_ON_FALSE(status != NULL, ESP_ERR_INVALID_ARG, TAG, "status is null");

    portENTER_CRITICAL(&s_scheduler_lock);
    for (size_t i = 0; i < APP_SCHEDULER_MAX_ENTRIES; ++i) {
        if (app_scheduler_entry_matches(&s_entries[i], owner, tag)) {
            memset(status, 0, sizeof(*status));
            status->occupied = true;
            status->slot_id = (uint8_t)i;
            status->config = s_entries[i].config;
            status->state = s_entries[i].state;
            status->fired_count = s_entries[i].fired_count;
            status->missed_event_count = s_entries[i].missed_event_count;
            status->last_event_at = s_entries[i].last_event_at;
            portEXIT_CRITICAL(&s_scheduler_lock);
            return ESP_OK;
        }
    }
    portEXIT_CRITICAL(&s_scheduler_lock);
    return ESP_ERR_NOT_FOUND;
}

esp_err_t app_scheduler_list(app_scheduler_status_t *statuses, size_t max_count, size_t *count)
{
    size_t written = 0;
    size_t occupied = 0;

    ESP_RETURN_ON_FALSE(statuses != NULL || max_count == 0, ESP_ERR_INVALID_ARG, TAG, "statuses is null");

    portENTER_CRITICAL(&s_scheduler_lock);
    for (size_t i = 0; i < APP_SCHEDULER_MAX_ENTRIES; ++i) {
        if (!s_entries[i].occupied) {
            continue;
        }
        ++occupied;
        if (written < max_count) {
            statuses[written] = (app_scheduler_status_t){
                .occupied = true,
                .slot_id = (uint8_t)i,
                .config = s_entries[i].config,
                .state = s_entries[i].state,
                .fired_count = s_entries[i].fired_count,
                .missed_event_count = s_entries[i].missed_event_count,
                .last_event_at = s_entries[i].last_event_at,
            };
            ++written;
        }
    }
    portEXIT_CRITICAL(&s_scheduler_lock);

    if (count != NULL) {
        *count = occupied;
    }
    return occupied > max_count ? ESP_ERR_INVALID_SIZE : ESP_OK;
}

esp_err_t app_scheduler_receive_event(app_scheduler_event_t *event, TickType_t wait_ticks)
{
    ESP_RETURN_ON_FALSE(event != NULL, ESP_ERR_INVALID_ARG, TAG, "event is null");
    ESP_RETURN_ON_FALSE(s_event_queue != NULL, ESP_ERR_INVALID_STATE, TAG, "scheduler not initialized");
    return xQueueReceive(s_event_queue, event, wait_ticks) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t app_scheduler_register_handler(const char *owner, app_scheduler_event_handler_t handler, void *ctx)
{
    size_t target = APP_SCHEDULER_MAX_ENTRIES;

    ESP_RETURN_ON_FALSE(app_scheduler_valid_name(owner), ESP_ERR_INVALID_ARG, TAG, "owner is required");

    portENTER_CRITICAL(&s_scheduler_lock);
    for (size_t i = 0; i < APP_SCHEDULER_MAX_ENTRIES; ++i) {
        if (s_handlers[i].occupied && strcmp(s_handlers[i].owner, owner) == 0) {
            target = i;
            break;
        }
    }
    if (target == APP_SCHEDULER_MAX_ENTRIES && handler != NULL) {
        for (size_t i = 0; i < APP_SCHEDULER_MAX_ENTRIES; ++i) {
            if (!s_handlers[i].occupied) {
                target = i;
                break;
            }
        }
    }
    if (target == APP_SCHEDULER_MAX_ENTRIES) {
        portEXIT_CRITICAL(&s_scheduler_lock);
        return handler == NULL ? ESP_ERR_NOT_FOUND : ESP_ERR_NO_MEM;
    }

    if (handler == NULL) {
        memset(&s_handlers[target], 0, sizeof(s_handlers[target]));
    } else {
        s_handlers[target].occupied = true;
        strlcpy(s_handlers[target].owner, owner, sizeof(s_handlers[target].owner));
        s_handlers[target].handler = handler;
        s_handlers[target].ctx = ctx;
    }
    portEXIT_CRITICAL(&s_scheduler_lock);

    return ESP_OK;
}
