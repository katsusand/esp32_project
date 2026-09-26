#ifndef CYD_CLOCK_ALARM_H
#define CYD_CLOCK_ALARM_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "app_scheduler.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The clock app's two alarms.
 *
 * Part of the clock app, not a reusable alarm service: only
 * cyd_clock_app_register() installs it, so a product without the clock has no
 * alarm, and replacing the clock takes the alarm with it. The alarms are
 * app_scheduler entries (owner "clock", app scope). This component keeps no
 * copy of their state, which is what separates it from the removed cyd_alarm,
 * a second store that had to be kept in sync with the scheduler.
 *
 * English contract: app scope means Clear App Data resets both alarms to their
 * defaults at the next boot, and a product that drops the clock leaves them as
 * app data in the NVS list instead of live feature data.
 */

typedef enum {
    CYD_CLOCK_ALARM_1 = 0, /* repeats on the selected weekdays */
    CYD_CLOCK_ALARM_2,     /* rings once, then turns itself off */
    CYD_CLOCK_ALARM_COUNT,
} cyd_clock_alarm_id_t;

typedef struct {
    uint8_t hour;
    uint8_t minute;
    uint8_t weekday_mask; /* APP_SCHEDULER_WEEKDAY_* bits; used by alarm 1 only */
    bool enabled;
} cyd_clock_alarm_config_t;

/*
 * Installs the alarm handler and makes sure both schedules exist in app scope,
 * creating the defaults or moving ones saved before schedules had a scope.
 * Called from cyd_clock_app_register(); app_scheduler_init() must run first.
 */
esp_err_t cyd_clock_alarm_register(void);

/* The saved settings, or the defaults while the schedule does not exist. */
esp_err_t cyd_clock_alarm_get(cyd_clock_alarm_id_t alarm_id, cyd_clock_alarm_config_t *config);
esp_err_t cyd_clock_alarm_set(cyd_clock_alarm_id_t alarm_id, const cyd_clock_alarm_config_t *config);
bool cyd_clock_alarm_is_enabled(cyd_clock_alarm_id_t alarm_id);
esp_err_t cyd_clock_alarm_set_enabled(cyd_clock_alarm_id_t alarm_id, bool enabled);

#ifdef __cplusplus
}
#endif

#endif
