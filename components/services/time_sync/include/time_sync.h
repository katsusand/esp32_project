#ifndef TIME_SYNC_H
#define TIME_SYNC_H

#include <stdbool.h>
#include <time.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TIME_SYNC_STATE_STOPPED = 0,
    TIME_SYNC_STATE_IDLE,
    TIME_SYNC_STATE_WAITING_WIFI,
    TIME_SYNC_STATE_SYNCING,
    TIME_SYNC_STATE_RETRY_WAIT,
} time_sync_state_t;

esp_err_t time_sync_start(void);
void time_sync_request_soon(void);
void time_sync_request_soon_and_release_wifi(void);
esp_err_t time_sync_set_interval_minutes(uint16_t interval_minutes);
esp_err_t time_sync_save_interval_minutes(void);
uint16_t time_sync_get_interval_minutes(void);
esp_err_t time_sync_set_timezone(const char *timezone);
esp_err_t time_sync_save_timezone(void);
const char *time_sync_get_timezone(void);
time_sync_state_t time_sync_get_state(void);
bool time_sync_is_busy(void);
bool time_sync_get_last_success_at(time_t *sync_time);
bool time_sync_get_last_attempt_status(esp_err_t *status);

#ifdef __cplusplus
}
#endif

#endif
