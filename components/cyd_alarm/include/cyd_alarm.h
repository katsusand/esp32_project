#ifndef CYD_ALARM_H
#define CYD_ALARM_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CYD_ALARM_WEEKDAY_SUNDAY (1U << 0)
#define CYD_ALARM_WEEKDAY_MONDAY (1U << 1)
#define CYD_ALARM_WEEKDAY_TUESDAY (1U << 2)
#define CYD_ALARM_WEEKDAY_WEDNESDAY (1U << 3)
#define CYD_ALARM_WEEKDAY_THURSDAY (1U << 4)
#define CYD_ALARM_WEEKDAY_FRIDAY (1U << 5)
#define CYD_ALARM_WEEKDAY_SATURDAY (1U << 6)
#define CYD_ALARM_WEEKDAY_ALL 0x7fU

typedef enum {
    CYD_ALARM_MODE_OFF = 0,
    CYD_ALARM_MODE_1,
    CYD_ALARM_MODE_2,
    CYD_ALARM_MODE_1_2,
} cyd_alarm_mode_t;

typedef enum {
    CYD_ALARM_ID_1 = 0,
    CYD_ALARM_ID_2,
} cyd_alarm_id_t;

typedef struct {
    uint8_t hour;
    uint8_t minute;
    uint8_t weekday_mask;
    bool enabled;
} cyd_alarm_config_t;

esp_err_t cyd_alarm_init(void);
esp_err_t cyd_alarm_save(void);
cyd_alarm_mode_t cyd_alarm_get_mode(void);
esp_err_t cyd_alarm_set_mode(cyd_alarm_mode_t mode);
esp_err_t cyd_alarm_cycle_mode(cyd_alarm_mode_t *mode);
const char *cyd_alarm_mode_label(cyd_alarm_mode_t mode);
bool cyd_alarm_is_any_enabled(void);
esp_err_t cyd_alarm_get_config(cyd_alarm_id_t alarm_id, cyd_alarm_config_t *config);
esp_err_t cyd_alarm_set_time(cyd_alarm_id_t alarm_id, uint8_t hour, uint8_t minute);
esp_err_t cyd_alarm_set_alarm1_weekday_mask(uint8_t weekday_mask);

#ifdef __cplusplus
}
#endif

#endif
