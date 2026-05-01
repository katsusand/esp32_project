#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_log.h"
#include "nvs.h"
#include "cyd_alarm.h"
#include "cyd_speaker.h"

#ifndef CONFIG_CYD_ALARM_TASK_STACK_SIZE
#define CONFIG_CYD_ALARM_TASK_STACK_SIZE 4096
#endif
#ifndef CONFIG_CYD_ALARM_TASK_PRIORITY
#define CONFIG_CYD_ALARM_TASK_PRIORITY 4
#endif
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

#define CYD_ALARM_TASK_POLL_MS 250
#define CYD_ALARM_CONFIG_VERSION 1U

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
static TaskHandle_t s_alarm_task_handle;
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

static bool cyd_alarm_matches_alarm1(const struct tm *timeinfo)
{
    uint8_t weekday_mask = 0;
    uint8_t hour = 0;
    uint8_t minute = 0;
    bool enabled = false;

    if (timeinfo == NULL) {
        return false;
    }

    portENTER_CRITICAL(&s_alarm_lock);
    enabled = s_alarm1.enabled;
    weekday_mask = s_alarm1.weekday_mask;
    hour = s_alarm1.hour;
    minute = s_alarm1.minute;
    portEXIT_CRITICAL(&s_alarm_lock);

    if (!enabled) {
        return false;
    }

    if ((weekday_mask & (1U << timeinfo->tm_wday)) == 0) {
        return false;
    }

    return timeinfo->tm_hour == hour && timeinfo->tm_min == minute;
}

static bool cyd_alarm_matches_alarm2(const struct tm *timeinfo)
{
    uint8_t hour = 0;
    uint8_t minute = 0;
    bool enabled = false;

    if (timeinfo == NULL) {
        return false;
    }

    portENTER_CRITICAL(&s_alarm_lock);
    enabled = s_alarm2.enabled;
    hour = s_alarm2.hour;
    minute = s_alarm2.minute;
    portEXIT_CRITICAL(&s_alarm_lock);

    return enabled && timeinfo->tm_hour == hour && timeinfo->tm_min == minute;
}

static int32_t cyd_alarm_minute_stamp(const struct tm *timeinfo)
{
    if (timeinfo == NULL) {
        return -1;
    }

    return ((int32_t)timeinfo->tm_year * 366 * 24 * 60) +
           ((int32_t)timeinfo->tm_yday * 24 * 60) +
           ((int32_t)timeinfo->tm_hour * 60) +
           (int32_t)timeinfo->tm_min;
}

static void cyd_alarm_task(void *arg)
{
    int32_t last_stamp = -1;

    (void)arg;

    while (true) {
        time_t now = 0;
        struct tm local_time = { 0 };

        time(&now);
        localtime_r(&now, &local_time);

        if ((local_time.tm_year + 1900) >= 2024) {
            int32_t stamp = cyd_alarm_minute_stamp(&local_time);
            if (stamp != last_stamp) {
                bool alarm1_match = cyd_alarm_matches_alarm1(&local_time);
                bool alarm2_match = cyd_alarm_matches_alarm2(&local_time);

                if (alarm1_match || alarm2_match) {
                    ESP_LOGI(TAG,
                             "alarm triggered at %02d:%02d (alarm1=%d alarm2=%d)",
                             local_time.tm_hour,
                             local_time.tm_min,
                             alarm1_match,
                             alarm2_match);
                    (void)cyd_speaker_play_event(CYD_SPEAKER_EVENT_ALARM);
                }

                if (alarm2_match) {
                    portENTER_CRITICAL(&s_alarm_lock);
                    s_alarm2.enabled = false;
                    portEXIT_CRITICAL(&s_alarm_lock);
                    (void)cyd_alarm_save();
                }

                last_stamp = stamp;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(CYD_ALARM_TASK_POLL_MS));
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

    BaseType_t created = xTaskCreate(cyd_alarm_task,
                                     "cyd_alarm",
                                     CONFIG_CYD_ALARM_TASK_STACK_SIZE,
                                     NULL,
                                     CONFIG_CYD_ALARM_TASK_PRIORITY,
                                     &s_alarm_task_handle);
    ESP_RETURN_ON_FALSE(created == pdPASS, ESP_FAIL, TAG, "create alarm task failed");

    s_alarm_started = true;
    return ESP_OK;
}

esp_err_t cyd_alarm_save(void)
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
