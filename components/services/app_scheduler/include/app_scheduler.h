#ifndef APP_SCHEDULER_H
#define APP_SCHEDULER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_SCHEDULER_MAX_ENTRIES 5U
#define APP_SCHEDULER_OWNER_MAX_LEN 15U
#define APP_SCHEDULER_TAG_MAX_LEN 15U
#define APP_SCHEDULER_WEEKDAY_SUNDAY (1U << 0)
#define APP_SCHEDULER_WEEKDAY_MONDAY (1U << 1)
#define APP_SCHEDULER_WEEKDAY_TUESDAY (1U << 2)
#define APP_SCHEDULER_WEEKDAY_WEDNESDAY (1U << 3)
#define APP_SCHEDULER_WEEKDAY_THURSDAY (1U << 4)
#define APP_SCHEDULER_WEEKDAY_FRIDAY (1U << 5)
#define APP_SCHEDULER_WEEKDAY_SATURDAY (1U << 6)
#define APP_SCHEDULER_WEEKDAY_ALL 0x7fU

typedef enum {
    APP_SCHEDULER_MODE_INSTANT = 0,
    APP_SCHEDULER_MODE_WINDOW,
} app_scheduler_mode_t;

typedef enum {
    APP_SCHEDULER_BEHAVIOR_EVENT = 0,
    APP_SCHEDULER_BEHAVIOR_LATCHED,
} app_scheduler_behavior_t;

typedef enum {
    APP_SCHEDULER_STATE_DISABLED = 0,
    APP_SCHEDULER_STATE_WAITING,
    APP_SCHEDULER_STATE_ACTIVE,
    APP_SCHEDULER_STATE_STOPPED,
} app_scheduler_state_t;

typedef enum {
    APP_SCHEDULER_EVENT_FIRED = 0,
    APP_SCHEDULER_EVENT_STARTED,
    APP_SCHEDULER_EVENT_ENDED,
    APP_SCHEDULER_EVENT_STOPPED_BY_USER,
} app_scheduler_event_type_t;

typedef struct {
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
} app_scheduler_time_of_day_t;

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
} app_scheduler_config_t;

typedef struct {
    bool occupied;
    uint8_t slot_id;
    app_scheduler_config_t config;
    app_scheduler_state_t state;
    uint32_t fired_count;
    uint32_t missed_event_count;
    time_t last_event_at;
} app_scheduler_status_t;

typedef struct {
    uint8_t slot_id;
    char owner[APP_SCHEDULER_OWNER_MAX_LEN + 1];
    char tag[APP_SCHEDULER_TAG_MAX_LEN + 1];
    app_scheduler_event_type_t type;
    time_t occurred_at;
} app_scheduler_event_t;

typedef void (*app_scheduler_event_handler_t)(const app_scheduler_event_t *event, void *ctx);

esp_err_t app_scheduler_init(void);
esp_err_t app_scheduler_upsert(const app_scheduler_config_t *config);
esp_err_t app_scheduler_remove(const char *owner, const char *tag);
esp_err_t app_scheduler_set_enabled(const char *owner, const char *tag, bool enabled);
esp_err_t app_scheduler_stop(const char *owner, const char *tag);
esp_err_t app_scheduler_get_status(const char *owner, const char *tag, app_scheduler_status_t *status);
esp_err_t app_scheduler_list(app_scheduler_status_t *statuses, size_t max_count, size_t *count);
esp_err_t app_scheduler_receive_event(app_scheduler_event_t *event, TickType_t wait_ticks);
esp_err_t app_scheduler_register_handler(const char *owner, app_scheduler_event_handler_t handler, void *ctx);

#ifdef __cplusplus
}
#endif

#endif
