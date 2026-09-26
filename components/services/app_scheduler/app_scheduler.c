#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_health.h"
#include "nvs_schema.h"
#include "app_scheduler.h"
#include "time_tick.h"

/* This component owns both namespaces. An entry's scope picks the one it is
   stored in, and the prefix is what lets nvs_schema clear app-owned schedules
   with the rest of the app's data while service-owned ones stay. */
NVS_SCHEMA_DECLARE_NS(NVS_NS_FEATURE, "ftr_sched");
NVS_SCHEMA_DECLARE_NS(NVS_NS_APP, "app_sched");
static const nvs_key_descriptor_t NVS_KEY_APP_SCHEDULER_FEATURE_CONFIG = {
    .ns = NVS_NS_FEATURE,
    .key = "config_v1",
};
static const nvs_key_descriptor_t NVS_KEY_APP_SCHEDULER_APP_CONFIG = {
    .ns = NVS_NS_APP,
    .key = "config_v1",
};

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

/*
 * On-flash layout of one entry, frozen at what APP_SCHEDULER_CONFIG_VERSION 2
 * blobs already hold. Deliberately not app_scheduler_config_t: that struct
 * gained `scope`, which the namespace an entry is stored in already tells, and
 * letting the API struct define the stored format is how adding a field would
 * silently invalidate every stored blob and force Initialize NVS on update.
 */
typedef struct {
    char owner[APP_SCHEDULER_OWNER_MAX_LEN + 1];
    char tag[APP_SCHEDULER_TAG_MAX_LEN + 1];
    app_scheduler_mode_t mode;
    app_scheduler_behavior_t behavior;
    bool enabled;
    bool repeat;
    uint8_t weekday_mask;
    app_scheduler_time_of_day_t at;
    app_scheduler_time_of_day_t to;
} app_scheduler_disk_config_t;

typedef struct {
    uint32_t version;
    app_scheduler_disk_config_t configs[APP_SCHEDULER_MAX_ENTRIES];
    uint8_t occupied[APP_SCHEDULER_MAX_ENTRIES];
} app_scheduler_disk_t;

/* The sizes blobs written by earlier firmware have; checked against them. */
ESP_STATIC_ASSERT(sizeof(app_scheduler_disk_config_t) == 52, "scheduler entry layout on flash changed");
ESP_STATIC_ASSERT(sizeof(app_scheduler_disk_t) == 272, "scheduler blob layout on flash changed");

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
/* Serializes app_scheduler_write_scope(); see there. */
static SemaphoreHandle_t s_persist_mutex;
static StaticSemaphore_t s_persist_mutex_storage;

/*
 * Only drops bits outside the weekday range.
 *
 * An empty mask is a legal setting meaning "no day selected", so a repeating
 * schedule with it simply never fires. Substituting every weekday for it used
 * to make deselecting the last day silently select all seven, which reads as
 * the UI fighting the user. A pointless setting is still the user's to make.
 */
static uint8_t app_scheduler_normalize_weekday_mask(uint8_t weekday_mask)
{
    return weekday_mask & APP_SCHEDULER_WEEKDAY_ALL;
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
    ESP_RETURN_ON_FALSE(config->scope == APP_SCHEDULER_SCOPE_APP ||
                            config->scope == APP_SCHEDULER_SCOPE_FEATURE,
                        ESP_ERR_INVALID_ARG,
                        TAG,
                        "invalid scope");

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

/* Returns true when the owner had a handler and it ran. */
static bool app_scheduler_dispatch_event(const app_scheduler_event_t *event)
{
    app_scheduler_event_handler_t handler = NULL;
    void *ctx = NULL;

    if (event == NULL) {
        return false;
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

    if (handler == NULL) {
        return false;
    }
    handler(event, ctx);
    return true;
}

/*
 * Queue for observers, then the owner's handler.
 *
 * English contract: an event its owner's handler received counts as delivered
 * even when the observer queue is full. Nothing in this project drains that
 * queue, so it filled after 16 events and from then on every alarm logged
 * "event queue full" and counted as missed although its handler had run. Only
 * an event that reached neither the queue nor a handler is missed.
 */
static void app_scheduler_deliver_event(size_t index, const app_scheduler_event_t *event)
{
    bool queued = event != NULL &&
                  s_event_queue != NULL &&
                  xQueueSend(s_event_queue, event, 0) == pdTRUE;
    bool handled = app_scheduler_dispatch_event(event);

    if (!queued && !handled && index < APP_SCHEDULER_MAX_ENTRIES) {
        portENTER_CRITICAL(&s_scheduler_lock);
        ++s_entries[index].missed_event_count;
        portEXIT_CRITICAL(&s_scheduler_lock);
        ESP_LOGW(TAG, "event reached no handler and the queue is full: slot=%u", (unsigned)index);
    }
}

static const nvs_key_descriptor_t *app_scheduler_scope_key(app_scheduler_scope_t scope)
{
    return scope == APP_SCHEDULER_SCOPE_FEATURE ? &NVS_KEY_APP_SCHEDULER_FEATURE_CONFIG
                                                : &NVS_KEY_APP_SCHEDULER_APP_CONFIG;
}

static app_scheduler_disk_config_t app_scheduler_to_disk_config(const app_scheduler_config_t *config)
{
    app_scheduler_disk_config_t disk = {
        .mode = config->mode,
        .behavior = config->behavior,
        .enabled = config->enabled,
        .repeat = config->repeat,
        .weekday_mask = config->weekday_mask,
        .at = config->at,
        .to = config->to,
    };

    memcpy(disk.owner, config->owner, sizeof(disk.owner));
    memcpy(disk.tag, config->tag, sizeof(disk.tag));
    return disk;
}

static app_scheduler_config_t app_scheduler_from_disk_config(const app_scheduler_disk_config_t *disk,
                                                             app_scheduler_scope_t scope)
{
    app_scheduler_config_t config = {
        .mode = disk->mode,
        .behavior = disk->behavior,
        .enabled = disk->enabled,
        .repeat = disk->repeat,
        .weekday_mask = app_scheduler_normalize_weekday_mask(disk->weekday_mask),
        .at = disk->at,
        .to = disk->to,
        .scope = scope,
    };

    memcpy(config.owner, disk->owner, sizeof(config.owner));
    memcpy(config.tag, disk->tag, sizeof(config.tag));
    config.owner[APP_SCHEDULER_OWNER_MAX_LEN] = '\0';
    config.tag[APP_SCHEDULER_TAG_MAX_LEN] = '\0';
    return config;
}

/*
 * Rewrites one scope's blob from the entries currently in that scope. A scope
 * left without entries has its key erased, so a namespace holds data only while
 * it has schedules.
 *
 * English contract: snapshot and write happen under s_persist_mutex as one
 * step. The tick task (disabling a fired one-shot) and a UI edit can both get
 * here; without the mutex the older snapshot could be written last and a
 * reboot would bring back the stale setting.
 */
static esp_err_t app_scheduler_write_scope(app_scheduler_scope_t scope)
{
    const nvs_key_descriptor_t *key = app_scheduler_scope_key(scope);
    nvs_handle_t nvs_handle;
    size_t stored = 0;
    app_scheduler_disk_t disk = {
        .version = APP_SCHEDULER_CONFIG_VERSION,
    };

    xSemaphoreTake(s_persist_mutex, portMAX_DELAY);
    portENTER_CRITICAL(&s_scheduler_lock);
    for (size_t i = 0; i < APP_SCHEDULER_MAX_ENTRIES; ++i) {
        if (!s_entries[i].occupied || s_entries[i].config.scope != scope) {
            continue;
        }
        disk.configs[stored] = app_scheduler_to_disk_config(&s_entries[i].config);
        disk.occupied[stored] = 1U;
        ++stored;
    }
    portEXIT_CRITICAL(&s_scheduler_lock);

    esp_err_t err = nvs_open_descriptor(key->ns, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        xSemaphoreGive(s_persist_mutex);
        ESP_LOGE(TAG, "open NVS failed: %s", esp_err_to_name(err));
        return err;
    }
    err = stored > 0 ? nvs_set_blob(nvs_handle, key->key, &disk, sizeof(disk))
                     : nvs_erase_key(nvs_handle, key->key);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK;
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs_handle);
    }
    nvs_close(nvs_handle);
    xSemaphoreGive(s_persist_mutex);
    return err;
}

/* Persists `scope`, and `previous_scope` too when an entry moved out of it.
   The destination is written first, so a reset in between leaves a duplicate
   (resolved on load) rather than a lost entry. */
static esp_err_t app_scheduler_persist(app_scheduler_scope_t scope, app_scheduler_scope_t previous_scope)
{
    esp_err_t err = app_scheduler_write_scope(scope);
    if (err == ESP_OK && previous_scope != scope) {
        err = app_scheduler_write_scope(previous_scope);
    }
    return err;
}

/*
 * Adds one scope's stored entries to the pool, into free slots.
 *
 * An entry whose owner and tag are already loaded is skipped. That only happens
 * when a move between scopes was cut short by a reset, and init loads app scope
 * first because the only move this project makes is into app scope. The stale
 * copy is then removed from this scope's blob; left there, it would come back
 * as soon as the other scope is cleared (Clear App Data would restore an old
 * alarm instead of the defaults). Runs from init, before the task exists.
 */
static esp_err_t app_scheduler_load_scope(app_scheduler_scope_t scope)
{
    const nvs_key_descriptor_t *key = app_scheduler_scope_key(scope);
    nvs_handle_t nvs_handle;
    app_scheduler_disk_t disk = { 0 };
    size_t disk_size = sizeof(disk);
    size_t duplicates = 0;
    size_t overflow = 0;

    esp_err_t err = nvs_open_descriptor(key->ns, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_get_blob(nvs_handle, key->key, &disk, &disk_size);
    nvs_close(nvs_handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_NVS_INVALID_LENGTH) {
            nvs_health_report_invalid(key, err, "invalid scheduler blob length");
        }
        return err;
    }
    if (disk_size != sizeof(disk) || disk.version != APP_SCHEDULER_CONFIG_VERSION) {
        nvs_health_report_invalid(key, ESP_ERR_INVALID_VERSION, "invalid scheduler blob");
        return ESP_ERR_INVALID_VERSION;
    }

    for (size_t i = 0; i < APP_SCHEDULER_MAX_ENTRIES; ++i) {
        if (disk.occupied[i] == 0) {
            continue;
        }
        app_scheduler_config_t config = app_scheduler_from_disk_config(&disk.configs[i], scope);
        if (app_scheduler_validate_config(&config) != ESP_OK) {
            nvs_health_report_invalid(key, ESP_ERR_INVALID_ARG, "invalid scheduler entry");
            return ESP_ERR_INVALID_ARG;
        }
    }

    portENTER_CRITICAL(&s_scheduler_lock);
    for (size_t i = 0; i < APP_SCHEDULER_MAX_ENTRIES; ++i) {
        if (disk.occupied[i] == 0) {
            continue;
        }
        app_scheduler_config_t config = app_scheduler_from_disk_config(&disk.configs[i], scope);
        size_t target = APP_SCHEDULER_MAX_ENTRIES;
        bool duplicate = false;
        for (size_t j = 0; j < APP_SCHEDULER_MAX_ENTRIES; ++j) {
            if (app_scheduler_entry_matches(&s_entries[j], config.owner, config.tag)) {
                duplicate = true;
                break;
            }
            if (target == APP_SCHEDULER_MAX_ENTRIES && !s_entries[j].occupied) {
                target = j;
            }
        }
        if (duplicate) {
            ++duplicates;
            continue;
        }
        if (target == APP_SCHEDULER_MAX_ENTRIES) {
            ++overflow;
            continue;
        }
        s_entries[target].occupied = true;
        s_entries[target].config = config;
        s_entries[target].state = app_scheduler_initial_state(&config);
        s_entries[target].last_event_stamp = -1;
    }
    portEXIT_CRITICAL(&s_scheduler_lock);

    if (overflow > 0) {
        ESP_LOGW(TAG, "%s: no free slot for %u entries", key->ns, (unsigned)overflow);
    }
    /* Not while anything overflowed: the rewrite would drop those for good. */
    if (duplicates > 0 && overflow == 0) {
        ESP_LOGW(TAG, "%s: removing %u stale duplicate entries", key->ns, (unsigned)duplicates);
        err = app_scheduler_write_scope(scope);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "%s: removing duplicates failed: %s", key->ns, esp_err_to_name(err));
        }
    }
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
    app_scheduler_scope_t persist_scope = APP_SCHEDULER_SCOPE_APP;

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
    persist_scope = s_entries[index].config.scope;
    portEXIT_CRITICAL(&s_scheduler_lock);

    if (publish) {
        app_scheduler_deliver_event(index, &event);
        if (persist) {
            (void)app_scheduler_write_scope(persist_scope);
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
    /* Before loading: a load can rewrite a scope. Static, so it cannot fail. */
    if (s_persist_mutex == NULL) {
        s_persist_mutex = xSemaphoreCreateMutexStatic(&s_persist_mutex_storage);
    }

    portENTER_CRITICAL(&s_scheduler_lock);
    memset(s_entries, 0, sizeof(s_entries));
    for (size_t i = 0; i < APP_SCHEDULER_MAX_ENTRIES; ++i) {
        s_entries[i].last_event_stamp = -1;
    }
    portEXIT_CRITICAL(&s_scheduler_lock);

    /* App scope first; see app_scheduler_load_scope(). A bad blob in one scope
       is reported to nvs_health and leaves the other scope's entries intact. */
    static const app_scheduler_scope_t load_order[] = {
        APP_SCHEDULER_SCOPE_APP,
        APP_SCHEDULER_SCOPE_FEATURE,
    };
    for (size_t i = 0; i < (sizeof(load_order) / sizeof(load_order[0])); ++i) {
        esp_err_t err = app_scheduler_load_scope(load_order[i]);
        if (err != ESP_OK &&
            err != ESP_ERR_NVS_NOT_FOUND &&
            err != ESP_ERR_NVS_INVALID_LENGTH &&
            err != ESP_ERR_INVALID_VERSION &&
            err != ESP_ERR_INVALID_ARG) {
            ESP_RETURN_ON_ERROR(err, TAG, "load scheduler config failed");
        }
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

    ESP_RETURN_ON_FALSE(s_scheduler_started, ESP_ERR_INVALID_STATE, TAG, "scheduler not initialized");
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

    app_scheduler_scope_t previous_scope = s_entries[target].occupied ? s_entries[target].config.scope
                                                                      : normalized.scope;
    s_entries[target].occupied = true;
    s_entries[target].config = normalized;
    s_entries[target].state = app_scheduler_initial_state(&normalized);
    s_entries[target].last_event_stamp = -1;
    portEXIT_CRITICAL(&s_scheduler_lock);

    return app_scheduler_persist(normalized.scope, previous_scope);
}

esp_err_t app_scheduler_remove(const char *owner, const char *tag)
{
    ESP_RETURN_ON_FALSE(s_scheduler_started, ESP_ERR_INVALID_STATE, TAG, "scheduler not initialized");
    ESP_RETURN_ON_FALSE(app_scheduler_valid_name(owner), ESP_ERR_INVALID_ARG, TAG, "owner is required");
    ESP_RETURN_ON_FALSE(app_scheduler_valid_name(tag), ESP_ERR_INVALID_ARG, TAG, "tag is required");

    portENTER_CRITICAL(&s_scheduler_lock);
    for (size_t i = 0; i < APP_SCHEDULER_MAX_ENTRIES; ++i) {
        if (app_scheduler_entry_matches(&s_entries[i], owner, tag)) {
            app_scheduler_scope_t scope = s_entries[i].config.scope;
            memset(&s_entries[i], 0, sizeof(s_entries[i]));
            s_entries[i].last_event_stamp = -1;
            portEXIT_CRITICAL(&s_scheduler_lock);
            return app_scheduler_write_scope(scope);
        }
    }
    portEXIT_CRITICAL(&s_scheduler_lock);
    return ESP_ERR_NOT_FOUND;
}

esp_err_t app_scheduler_set_enabled(const char *owner, const char *tag, bool enabled)
{
    ESP_RETURN_ON_FALSE(s_scheduler_started, ESP_ERR_INVALID_STATE, TAG, "scheduler not initialized");
    ESP_RETURN_ON_FALSE(app_scheduler_valid_name(owner), ESP_ERR_INVALID_ARG, TAG, "owner is required");
    ESP_RETURN_ON_FALSE(app_scheduler_valid_name(tag), ESP_ERR_INVALID_ARG, TAG, "tag is required");

    portENTER_CRITICAL(&s_scheduler_lock);
    for (size_t i = 0; i < APP_SCHEDULER_MAX_ENTRIES; ++i) {
        if (app_scheduler_entry_matches(&s_entries[i], owner, tag)) {
            s_entries[i].config.enabled = enabled;
            s_entries[i].state = app_scheduler_initial_state(&s_entries[i].config);
            s_entries[i].last_event_stamp = -1;
            app_scheduler_scope_t scope = s_entries[i].config.scope;
            portEXIT_CRITICAL(&s_scheduler_lock);
            return app_scheduler_write_scope(scope);
        }
    }
    portEXIT_CRITICAL(&s_scheduler_lock);
    return ESP_ERR_NOT_FOUND;
}

esp_err_t app_scheduler_stop(const char *owner, const char *tag)
{
    app_scheduler_event_t event = { 0 };
    size_t target = APP_SCHEDULER_MAX_ENTRIES;
    app_scheduler_scope_t scope = APP_SCHEDULER_SCOPE_APP;
    time_t now = 0;

    ESP_RETURN_ON_FALSE(s_scheduler_started, ESP_ERR_INVALID_STATE, TAG, "scheduler not initialized");
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
            scope = s_entries[i].config.scope;
            break;
        }
    }
    portEXIT_CRITICAL(&s_scheduler_lock);

    if (target == APP_SCHEDULER_MAX_ENTRIES) {
        return ESP_ERR_NOT_FOUND;
    }
    app_scheduler_deliver_event(target, &event);
    return app_scheduler_write_scope(scope);
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
