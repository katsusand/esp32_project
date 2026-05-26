#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include "freertos/portmacro.h"
#include "esp_check.h"
#include "esp_log.h"
#include "nvs.h"
#include "cyd_alarm.h"
#include "app_scheduler.h"
#include "cyd_speaker.h"

#ifndef CONFIG_CYD_ALARM_DEFAULT_1_HOUR
#define CONFIG_CYD_ALARM_DEFAULT_1_HOUR 7
#endif
#ifndef CONFIG_CYD_ALARM_DEFAULT_1_MINUTE
#define CONFIG_CYD_ALARM_DEFAULT_1_MINUTE 0
#endif
#ifndef CONFIG_CYD_ALARM_DEFAULT_2_HOUR
#define CONFIG_CYD_ALARM_DEFAULT_2_HOUR 7
#endif
#ifndef CONFIG_CYD_ALARM_DEFAULT_2_MINUTE
#define CONFIG_CYD_ALARM_DEFAULT_2_MINUTE 30
#endif

#define CYD_ALARM_CONFIG_VERSION 1U
#define CYD_ALARM_SCHEDULER_OWNER "clock"
#define CYD_ALARM_SCHEDULER_TAG_1 "alarm1"
#define CYD_ALARM_SCHEDULER_TAG_2 "alarm2"

static const char *TAG = "cyd_alarm";
static const char *NVS_NAMESPACE = "cyd_alarm";
static const char *NVS_CONFIG_KEY = "config_v1";

typedef struct {
    uint32_t version;
    uint8_t alarm1_hour;
    uint8_t alarm1_minute;
    uint8_t alarm1_weekday_mask;
    uint8_t alarm2_hour;
    uint8_t alarm2_minute;
    uint8_t flags;
    uint8_t reserved;
} cyd_alarm_config_disk_t;

#define CYD_ALARM_FLAG_1_ENABLED (1U << 0)
#define CYD_ALARM_FLAG_2_ENABLED (1U << 1)

static portMUX_TYPE s_alarm_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_alarm_started;
static cyd_alarm_config_t s_alarm1 = {
    .hour = CONFIG_CYD_ALARM_DEFAULT_1_HOUR,
    .minute = CONFIG_CYD_ALARM_DEFAULT_1_MINUTE,
    .weekday_mask = CYD_ALARM_WEEKDAY_ALL,
    .enabled = false,
};
static cyd_alarm_config_t s_alarm2 = {
    .hour = CONFIG_CYD_ALARM_DEFAULT_2_HOUR,
    .minute = CONFIG_CYD_ALARM_DEFAULT_2_MINUTE,
    .weekday_mask = 0,
    .enabled = false,
};

static uint8_t cyd_alarm_clamp_hour(uint8_t hour)
{
    return (hour <= 23U) ? hour : 23U;
}

static uint8_t cyd_alarm_clamp_minute(uint8_t minute)
{
    return (minute <= 59U) ? minute : 59U;
}

static uint8_t cyd_alarm_normalize_weekday_mask(uint8_t weekday_mask)
{
    return weekday_mask & CYD_ALARM_WEEKDAY_ALL;
}

static void cyd_alarm_apply_defaults_locked(void)
{
    s_alarm1.hour = cyd_alarm_clamp_hour(CONFIG_CYD_ALARM_DEFAULT_1_HOUR);
    s_alarm1.minute = cyd_alarm_clamp_minute(CONFIG_CYD_ALARM_DEFAULT_1_MINUTE);
    s_alarm1.weekday_mask = CYD_ALARM_WEEKDAY_ALL;
    s_alarm1.enabled = false;
    s_alarm2.hour = cyd_alarm_clamp_hour(CONFIG_CYD_ALARM_DEFAULT_2_HOUR);
    s_alarm2.minute = cyd_alarm_clamp_minute(CONFIG_CYD_ALARM_DEFAULT_2_MINUTE);
    s_alarm2.weekday_mask = 0;
    s_alarm2.enabled = false;
}

static void cyd_alarm_config_to_disk(cyd_alarm_config_disk_t *disk)
{
    ESP_RETURN_VOID_ON_FALSE(disk != NULL, TAG, "disk config is null");

    portENTER_CRITICAL(&s_alarm_lock);
    memset(disk, 0, sizeof(*disk));
    disk->version = CYD_ALARM_CONFIG_VERSION;
    disk->alarm1_hour = s_alarm1.hour;
    disk->alarm1_minute = s_alarm1.minute;
    disk->alarm1_weekday_mask = s_alarm1.weekday_mask;
    disk->alarm2_hour = s_alarm2.hour;
    disk->alarm2_minute = s_alarm2.minute;
    if (s_alarm1.enabled) {
        disk->flags |= CYD_ALARM_FLAG_1_ENABLED;
    }
    if (s_alarm2.enabled) {
        disk->flags |= CYD_ALARM_FLAG_2_ENABLED;
    }
    portEXIT_CRITICAL(&s_alarm_lock);
}

static void cyd_alarm_apply_disk_locked(const cyd_alarm_config_disk_t *disk)
{
    if (disk == NULL || disk->version != CYD_ALARM_CONFIG_VERSION) {
        cyd_alarm_apply_defaults_locked();
        return;
    }

    s_alarm1.hour = cyd_alarm_clamp_hour(disk->alarm1_hour);
    s_alarm1.minute = cyd_alarm_clamp_minute(disk->alarm1_minute);
    s_alarm1.weekday_mask = cyd_alarm_normalize_weekday_mask(disk->alarm1_weekday_mask);
    s_alarm1.enabled = (disk->flags & CYD_ALARM_FLAG_1_ENABLED) != 0;

    s_alarm2.hour = cyd_alarm_clamp_hour(disk->alarm2_hour);
    s_alarm2.minute = cyd_alarm_clamp_minute(disk->alarm2_minute);
    s_alarm2.weekday_mask = 0;
    s_alarm2.enabled = (disk->flags & CYD_ALARM_FLAG_2_ENABLED) != 0;
}

static esp_err_t cyd_alarm_load_from_nvs(void)
{
    nvs_handle_t nvs_handle;
    cyd_alarm_config_disk_t disk = { 0 };
    size_t disk_size = sizeof(disk);

    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_get_blob(nvs_handle, NVS_CONFIG_KEY, &disk, &disk_size);
    nvs_close(nvs_handle);
    if (err != ESP_OK) {
        return err;
    }
    if (disk_size != sizeof(disk) || disk.version != CYD_ALARM_CONFIG_VERSION) {
        return ESP_ERR_INVALID_VERSION;
    }

    portENTER_CRITICAL(&s_alarm_lock);
    cyd_alarm_apply_disk_locked(&disk);
    portEXIT_CRITICAL(&s_alarm_lock);
    return ESP_OK;
}

static esp_err_t cyd_alarm_write_config(void)
{
    nvs_handle_t nvs_handle;
    cyd_alarm_config_disk_t disk = { 0 };

    cyd_alarm_config_to_disk(&disk);
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle), TAG, "open NVS failed");
    esp_err_t err = nvs_set_blob(nvs_handle, NVS_CONFIG_KEY, &disk, sizeof(disk));
    if (err == ESP_OK) {
        err = nvs_commit(nvs_handle);
    }
    nvs_close(nvs_handle);
    return err;
}

static void cyd_alarm_make_scheduler_config(cyd_alarm_id_t alarm_id,
                                            const cyd_alarm_config_t *alarm,
                                            app_scheduler_config_t *config)
{
    ESP_RETURN_VOID_ON_FALSE(alarm != NULL, TAG, "alarm config is null");
    ESP_RETURN_VOID_ON_FALSE(config != NULL, TAG, "scheduler config is null");

    memset(config, 0, sizeof(*config));
    strlcpy(config->owner, CYD_ALARM_SCHEDULER_OWNER, sizeof(config->owner));
    strlcpy(config->tag,
            alarm_id == CYD_ALARM_ID_1 ? CYD_ALARM_SCHEDULER_TAG_1 : CYD_ALARM_SCHEDULER_TAG_2,
            sizeof(config->tag));
    config->mode = APP_SCHEDULER_MODE_INSTANT;
    config->enabled = alarm->enabled;
    config->repeat = alarm_id == CYD_ALARM_ID_1;
    config->weekday_mask = alarm_id == CYD_ALARM_ID_1 ? cyd_alarm_normalize_weekday_mask(alarm->weekday_mask)
                                                       : APP_SCHEDULER_WEEKDAY_ALL;
    config->at.hour = alarm->hour;
    config->at.minute = alarm->minute;
    config->at.second = 0;
}

static esp_err_t cyd_alarm_sync_scheduler_entry(cyd_alarm_id_t alarm_id, const cyd_alarm_config_t *alarm)
{
    const char *tag = alarm_id == CYD_ALARM_ID_1 ? CYD_ALARM_SCHEDULER_TAG_1 : CYD_ALARM_SCHEDULER_TAG_2;
    app_scheduler_config_t config = { 0 };

    ESP_RETURN_ON_FALSE(alarm != NULL, ESP_ERR_INVALID_ARG, TAG, "alarm config is null");

    if (!alarm->enabled) {
        esp_err_t remove_err = app_scheduler_remove(CYD_ALARM_SCHEDULER_OWNER, tag);
        return remove_err == ESP_ERR_NOT_FOUND ? ESP_OK : remove_err;
    }

    cyd_alarm_make_scheduler_config(alarm_id, alarm, &config);
    return app_scheduler_upsert(&config);
}

static esp_err_t cyd_alarm_sync_scheduler(void)
{
    cyd_alarm_config_t alarm1 = { 0 };
    cyd_alarm_config_t alarm2 = { 0 };

    portENTER_CRITICAL(&s_alarm_lock);
    alarm1 = s_alarm1;
    alarm2 = s_alarm2;
    portEXIT_CRITICAL(&s_alarm_lock);

    ESP_RETURN_ON_ERROR(cyd_alarm_sync_scheduler_entry(CYD_ALARM_ID_1, &alarm1),
                        TAG,
                        "sync alarm1 schedule failed");
    ESP_RETURN_ON_ERROR(cyd_alarm_sync_scheduler_entry(CYD_ALARM_ID_2, &alarm2),
                        TAG,
                        "sync alarm2 schedule failed");
    return ESP_OK;
}

static void cyd_alarm_scheduler_handler(const app_scheduler_event_t *event, void *ctx)
{
    (void)ctx;

    if (event == NULL ||
        event->type != APP_SCHEDULER_EVENT_FIRED ||
        strcmp(event->owner, CYD_ALARM_SCHEDULER_OWNER) != 0) {
        return;
    }

    if (strcmp(event->tag, CYD_ALARM_SCHEDULER_TAG_1) != 0 &&
        strcmp(event->tag, CYD_ALARM_SCHEDULER_TAG_2) != 0) {
        return;
    }

    ESP_LOGI(TAG, "alarm triggered: %s", event->tag);
    (void)cyd_speaker_play_event(CYD_SPEAKER_EVENT_ALARM);

    if (strcmp(event->tag, CYD_ALARM_SCHEDULER_TAG_2) == 0) {
        portENTER_CRITICAL(&s_alarm_lock);
        s_alarm2.enabled = false;
        portEXIT_CRITICAL(&s_alarm_lock);
        (void)cyd_alarm_write_config();
    }
}

esp_err_t cyd_alarm_init(void)
{
    if (s_alarm_started) {
        return ESP_OK;
    }

    portENTER_CRITICAL(&s_alarm_lock);
    cyd_alarm_apply_defaults_locked();
    portEXIT_CRITICAL(&s_alarm_lock);

    esp_err_t err = cyd_alarm_load_from_nvs();
    if (err != ESP_OK &&
        err != ESP_ERR_NVS_NOT_FOUND &&
        err != ESP_ERR_NVS_INVALID_LENGTH &&
        err != ESP_ERR_INVALID_VERSION) {
        ESP_RETURN_ON_ERROR(err, TAG, "load alarm config failed");
    }

    ESP_RETURN_ON_ERROR(app_scheduler_register_handler(CYD_ALARM_SCHEDULER_OWNER,
                                                       cyd_alarm_scheduler_handler,
                                                       NULL),
                        TAG,
                        "register alarm scheduler handler failed");
    ESP_RETURN_ON_ERROR(cyd_alarm_sync_scheduler(), TAG, "sync scheduler failed");

    s_alarm_started = true;
    return ESP_OK;
}

esp_err_t cyd_alarm_save(void)
{
    ESP_RETURN_ON_ERROR(cyd_alarm_write_config(), TAG, "write alarm config failed");
    return cyd_alarm_sync_scheduler();
}

cyd_alarm_mode_t cyd_alarm_get_mode(void)
{
    bool alarm1_enabled = false;
    bool alarm2_enabled = false;

    portENTER_CRITICAL(&s_alarm_lock);
    alarm1_enabled = s_alarm1.enabled;
    alarm2_enabled = s_alarm2.enabled;
    portEXIT_CRITICAL(&s_alarm_lock);

    if (alarm1_enabled && alarm2_enabled) {
        return CYD_ALARM_MODE_1_2;
    }
    if (alarm1_enabled) {
        return CYD_ALARM_MODE_1;
    }
    if (alarm2_enabled) {
        return CYD_ALARM_MODE_2;
    }
    return CYD_ALARM_MODE_OFF;
}

esp_err_t cyd_alarm_set_mode(cyd_alarm_mode_t mode)
{
    portENTER_CRITICAL(&s_alarm_lock);
    switch (mode) {
    case CYD_ALARM_MODE_OFF:
        s_alarm1.enabled = false;
        s_alarm2.enabled = false;
        break;
    case CYD_ALARM_MODE_1:
        s_alarm1.enabled = true;
        s_alarm2.enabled = false;
        break;
    case CYD_ALARM_MODE_2:
        s_alarm1.enabled = false;
        s_alarm2.enabled = true;
        break;
    case CYD_ALARM_MODE_1_2:
        s_alarm1.enabled = true;
        s_alarm2.enabled = true;
        break;
    default:
        portEXIT_CRITICAL(&s_alarm_lock);
        return ESP_ERR_INVALID_ARG;
    }
    portEXIT_CRITICAL(&s_alarm_lock);

    return cyd_alarm_save();
}

esp_err_t cyd_alarm_cycle_mode(cyd_alarm_mode_t *mode)
{
    cyd_alarm_mode_t next_mode = CYD_ALARM_MODE_OFF;
    cyd_alarm_mode_t current_mode = cyd_alarm_get_mode();

    switch (current_mode) {
    case CYD_ALARM_MODE_OFF:
        next_mode = CYD_ALARM_MODE_1;
        break;
    case CYD_ALARM_MODE_1:
        next_mode = CYD_ALARM_MODE_2;
        break;
    case CYD_ALARM_MODE_2:
        next_mode = CYD_ALARM_MODE_1_2;
        break;
    case CYD_ALARM_MODE_1_2:
    default:
        next_mode = CYD_ALARM_MODE_OFF;
        break;
    }

    ESP_RETURN_ON_ERROR(cyd_alarm_set_mode(next_mode), TAG, "set cycled alarm mode failed");
    if (mode != NULL) {
        *mode = next_mode;
    }
    return ESP_OK;
}

const char *cyd_alarm_mode_label(cyd_alarm_mode_t mode)
{
    switch (mode) {
    case CYD_ALARM_MODE_1:
        return "ALARM1 ON";
    case CYD_ALARM_MODE_2:
        return "ALARM2 ON";
    case CYD_ALARM_MODE_1_2:
        return "ALARM1/2 ON";
    case CYD_ALARM_MODE_OFF:
    default:
        return "ALARM OFF";
    }
}

bool cyd_alarm_is_any_enabled(void)
{
    return cyd_alarm_get_mode() != CYD_ALARM_MODE_OFF;
}

esp_err_t cyd_alarm_get_config(cyd_alarm_id_t alarm_id, cyd_alarm_config_t *config)
{
    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG, "config is null");

    portENTER_CRITICAL(&s_alarm_lock);
    if (alarm_id == CYD_ALARM_ID_1) {
        *config = s_alarm1;
    } else if (alarm_id == CYD_ALARM_ID_2) {
        *config = s_alarm2;
    } else {
        portEXIT_CRITICAL(&s_alarm_lock);
        return ESP_ERR_INVALID_ARG;
    }
    portEXIT_CRITICAL(&s_alarm_lock);
    return ESP_OK;
}

esp_err_t cyd_alarm_set_time(cyd_alarm_id_t alarm_id, uint8_t hour, uint8_t minute)
{
    hour = cyd_alarm_clamp_hour(hour);
    minute = cyd_alarm_clamp_minute(minute);

    portENTER_CRITICAL(&s_alarm_lock);
    if (alarm_id == CYD_ALARM_ID_1) {
        s_alarm1.hour = hour;
        s_alarm1.minute = minute;
    } else if (alarm_id == CYD_ALARM_ID_2) {
        s_alarm2.hour = hour;
        s_alarm2.minute = minute;
    } else {
        portEXIT_CRITICAL(&s_alarm_lock);
        return ESP_ERR_INVALID_ARG;
    }
    portEXIT_CRITICAL(&s_alarm_lock);

    return cyd_alarm_save();
}

esp_err_t cyd_alarm_set_alarm1_weekday_mask(uint8_t weekday_mask)
{
    weekday_mask = cyd_alarm_normalize_weekday_mask(weekday_mask);

    portENTER_CRITICAL(&s_alarm_lock);
    s_alarm1.weekday_mask = weekday_mask;
    portEXIT_CRITICAL(&s_alarm_lock);

    return cyd_alarm_save();
}
