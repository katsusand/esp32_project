#include <stdbool.h>
#include <string.h>
#include "esp_check.h"
#include "esp_log.h"
#include "app_scheduler.h"
#include "cyd_clock_alarm.h"
#include "cyd_speaker.h"

#define TAG "cyd_clock_alarm"
/* The scheduler identity of the clock's alarms; spelled only here. */
#define CYD_CLOCK_ALARM_OWNER "clock"

static const char *const CYD_CLOCK_ALARM_TAGS[CYD_CLOCK_ALARM_COUNT] = {
    [CYD_CLOCK_ALARM_1] = "alarm1",
    [CYD_CLOCK_ALARM_2] = "alarm2",
};

static bool cyd_clock_alarm_id_valid(cyd_clock_alarm_id_t alarm_id)
{
    return (size_t)alarm_id < CYD_CLOCK_ALARM_COUNT;
}

/* The fields that make a schedule one of the clock's alarms. */
static app_scheduler_config_t cyd_clock_alarm_schedule_base(cyd_clock_alarm_id_t alarm_id)
{
    app_scheduler_config_t schedule = {
        .mode = APP_SCHEDULER_MODE_INSTANT,
        .behavior = APP_SCHEDULER_BEHAVIOR_EVENT,
        .repeat = alarm_id == CYD_CLOCK_ALARM_1,
        .weekday_mask = APP_SCHEDULER_WEEKDAY_ALL,
        .scope = APP_SCHEDULER_SCOPE_APP,
    };

    strlcpy(schedule.owner, CYD_CLOCK_ALARM_OWNER, sizeof(schedule.owner));
    strlcpy(schedule.tag, CYD_CLOCK_ALARM_TAGS[alarm_id], sizeof(schedule.tag));
    return schedule;
}

static app_scheduler_config_t cyd_clock_alarm_default_schedule(cyd_clock_alarm_id_t alarm_id)
{
    app_scheduler_config_t schedule = cyd_clock_alarm_schedule_base(alarm_id);

    schedule.enabled = false;
    schedule.at.hour = 7;
    schedule.at.minute = alarm_id == CYD_CLOCK_ALARM_1 ? 0 : 30;
    return schedule;
}

static bool cyd_clock_alarm_find(cyd_clock_alarm_id_t alarm_id, app_scheduler_status_t *status)
{
    return app_scheduler_get_status(CYD_CLOCK_ALARM_OWNER, CYD_CLOCK_ALARM_TAGS[alarm_id], status) == ESP_OK;
}

static void cyd_clock_alarm_handle_event(const app_scheduler_event_t *event, void *ctx)
{
    (void)ctx;

    if (event == NULL || event->type != APP_SCHEDULER_EVENT_FIRED) {
        return;
    }
    if (strcmp(event->tag, CYD_CLOCK_ALARM_TAGS[CYD_CLOCK_ALARM_1]) != 0 &&
        strcmp(event->tag, CYD_CLOCK_ALARM_TAGS[CYD_CLOCK_ALARM_2]) != 0) {
        return;
    }

    ESP_LOGI(TAG, "alarm triggered: %s", event->tag);
    esp_err_t err = cyd_speaker_play_event(CYD_SPEAKER_EVENT_ALARM);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "alarm sound failed: %s", esp_err_to_name(err));
    }
}

static esp_err_t cyd_clock_alarm_prepare_schedule(cyd_clock_alarm_id_t alarm_id)
{
    app_scheduler_status_t status = { 0 };

    if (!cyd_clock_alarm_find(alarm_id, &status)) {
        app_scheduler_config_t schedule = cyd_clock_alarm_default_schedule(alarm_id);
        return app_scheduler_upsert(&schedule);
    }
    if (status.config.scope != APP_SCHEDULER_SCOPE_APP) {
        /* Saved before schedules had a scope, so it sits in the feature bucket
           where Clear App Data cannot reach it. Moving keeps the user's time. */
        ESP_LOGI(TAG, "moving %s to app scope", CYD_CLOCK_ALARM_TAGS[alarm_id]);
        status.config.scope = APP_SCHEDULER_SCOPE_APP;
        return app_scheduler_upsert(&status.config);
    }
    return ESP_OK;
}

esp_err_t cyd_clock_alarm_register(void)
{
    ESP_RETURN_ON_ERROR(app_scheduler_register_handler(CYD_CLOCK_ALARM_OWNER, cyd_clock_alarm_handle_event, NULL),
                        TAG,
                        "register alarm handler failed");
    for (size_t i = 0; i < CYD_CLOCK_ALARM_COUNT; ++i) {
        ESP_RETURN_ON_ERROR(cyd_clock_alarm_prepare_schedule((cyd_clock_alarm_id_t)i),
                            TAG,
                            "prepare %s failed",
                            CYD_CLOCK_ALARM_TAGS[i]);
    }
    return ESP_OK;
}

esp_err_t cyd_clock_alarm_get(cyd_clock_alarm_id_t alarm_id, cyd_clock_alarm_config_t *config)
{
    app_scheduler_status_t status = { 0 };

    ESP_RETURN_ON_FALSE(cyd_clock_alarm_id_valid(alarm_id), ESP_ERR_INVALID_ARG, TAG, "invalid alarm id");
    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG, "config is null");

    app_scheduler_config_t schedule = cyd_clock_alarm_find(alarm_id, &status)
                                          ? status.config
                                          : cyd_clock_alarm_default_schedule(alarm_id);
    *config = (cyd_clock_alarm_config_t){
        .hour = schedule.at.hour,
        .minute = schedule.at.minute,
        .weekday_mask = schedule.weekday_mask,
        .enabled = schedule.enabled,
    };
    return ESP_OK;
}

esp_err_t cyd_clock_alarm_set(cyd_clock_alarm_id_t alarm_id, const cyd_clock_alarm_config_t *config)
{
    ESP_RETURN_ON_FALSE(cyd_clock_alarm_id_valid(alarm_id), ESP_ERR_INVALID_ARG, TAG, "invalid alarm id");
    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG, "config is null");
    ESP_RETURN_ON_FALSE(config->hour <= 23U && config->minute <= 59U, ESP_ERR_INVALID_ARG, TAG, "invalid alarm time");

    app_scheduler_config_t schedule = cyd_clock_alarm_schedule_base(alarm_id);
    schedule.enabled = config->enabled;
    schedule.at.hour = config->hour;
    schedule.at.minute = config->minute;
    /* A repeating alarm keeps whatever days the user picked, including none.
       A one-shot alarm ignores weekdays, so it keeps the full mask. */
    if (schedule.repeat) {
        schedule.weekday_mask = config->weekday_mask & APP_SCHEDULER_WEEKDAY_ALL;
    }
    return app_scheduler_upsert(&schedule);
}

bool cyd_clock_alarm_is_enabled(cyd_clock_alarm_id_t alarm_id)
{
    app_scheduler_status_t status = { 0 };

    return cyd_clock_alarm_id_valid(alarm_id) &&
           cyd_clock_alarm_find(alarm_id, &status) &&
           status.config.enabled;
}

esp_err_t cyd_clock_alarm_set_enabled(cyd_clock_alarm_id_t alarm_id, bool enabled)
{
    ESP_RETURN_ON_FALSE(cyd_clock_alarm_id_valid(alarm_id), ESP_ERR_INVALID_ARG, TAG, "invalid alarm id");
    return app_scheduler_set_enabled(CYD_CLOCK_ALARM_OWNER, CYD_CLOCK_ALARM_TAGS[alarm_id], enabled);
}
