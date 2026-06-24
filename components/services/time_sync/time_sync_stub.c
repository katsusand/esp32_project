#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "sdkconfig.h"
#include "time_sync.h"

#ifndef CONFIG_TIME_SYNC_INTERVAL_MINUTES
#define CONFIG_TIME_SYNC_INTERVAL_MINUTES 60
#endif
#ifndef CONFIG_TIME_SYNC_TIMEZONE
#define CONFIG_TIME_SYNC_TIMEZONE "JST-9"
#endif

#define TIME_SYNC_TIMEZONE_MAX_LEN 31U

static uint16_t s_time_sync_interval_minutes = CONFIG_TIME_SYNC_INTERVAL_MINUTES;
static char s_time_sync_timezone[TIME_SYNC_TIMEZONE_MAX_LEN + 1] = CONFIG_TIME_SYNC_TIMEZONE;

static void time_sync_apply_timezone(void)
{
    if (s_time_sync_timezone[0] != '\0') {
        setenv("TZ", s_time_sync_timezone, 1);
    } else {
        unsetenv("TZ");
    }
    tzset();
}

esp_err_t time_sync_start(void)
{
    time_sync_apply_timezone();
    return ESP_OK;
}

void time_sync_request_soon(void)
{
}

void time_sync_request_soon_and_release_wifi(void)
{
}

esp_err_t time_sync_set_interval_minutes(uint16_t interval_minutes)
{
    if (interval_minutes == 0) {
        interval_minutes = 1;
    }
    if (interval_minutes > 1440U) {
        interval_minutes = 1440U;
    }
    s_time_sync_interval_minutes = interval_minutes;
    return ESP_OK;
}

esp_err_t time_sync_save_interval_minutes(void)
{
    return ESP_OK;
}

uint16_t time_sync_get_interval_minutes(void)
{
    return s_time_sync_interval_minutes;
}

esp_err_t time_sync_set_timezone(const char *timezone)
{
    if (timezone == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (strlen(timezone) > TIME_SYNC_TIMEZONE_MAX_LEN) {
        return ESP_ERR_INVALID_ARG;
    }
    snprintf(s_time_sync_timezone, sizeof(s_time_sync_timezone), "%s", timezone);
    time_sync_apply_timezone();
    return ESP_OK;
}

esp_err_t time_sync_save_timezone(void)
{
    return ESP_OK;
}

const char *time_sync_get_timezone(void)
{
    return s_time_sync_timezone;
}

time_sync_state_t time_sync_get_state(void)
{
    return TIME_SYNC_STATE_STOPPED;
}

bool time_sync_is_busy(void)
{
    return false;
}

bool time_sync_get_last_success_at(time_t *sync_time)
{
    (void)sync_time;
    return false;
}

bool time_sync_get_last_attempt_status(esp_err_t *status)
{
    if (status != NULL) {
        *status = ESP_ERR_NOT_SUPPORTED;
    }
    return true;
}
