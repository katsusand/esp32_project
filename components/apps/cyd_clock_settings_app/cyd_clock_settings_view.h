#ifndef CYD_CLOCK_SETTINGS_VIEW_H
#define CYD_CLOCK_SETTINGS_VIEW_H

/*
 * The clock settings screens, built from a model only: the two alarm pages
 * and the scheduler diagnostics page. All text is 16px, like every settings
 * screen.
 *
 * English contract: cyd_clock_settings_view_build() calls no service and
 * touches no global state. Scheduler owner and tag names are identifiers and
 * are shown as they are.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "cyd_display.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CYD_CLOCK_SETTINGS_ACTION_BACK 0x3101
#define CYD_CLOCK_SETTINGS_ACTION_PREV_PAGE 0x3102
#define CYD_CLOCK_SETTINGS_ACTION_NEXT_PAGE 0x3103
#define CYD_CLOCK_SETTINGS_ACTION_ALARM1_HOUR_DOWN 0x3104
#define CYD_CLOCK_SETTINGS_ACTION_ALARM1_HOUR_UP 0x3105
#define CYD_CLOCK_SETTINGS_ACTION_ALARM1_MINUTE_DOWN 0x3106
#define CYD_CLOCK_SETTINGS_ACTION_ALARM1_MINUTE_UP 0x3107
#define CYD_CLOCK_SETTINGS_ACTION_ALARM2_HOUR_DOWN 0x3108
#define CYD_CLOCK_SETTINGS_ACTION_ALARM2_HOUR_UP 0x3109
#define CYD_CLOCK_SETTINGS_ACTION_ALARM2_MINUTE_DOWN 0x310a
#define CYD_CLOCK_SETTINGS_ACTION_ALARM2_MINUTE_UP 0x310b
/* + weekday, 0 = Sunday ... 6 = Saturday */
#define CYD_CLOCK_SETTINGS_ACTION_ALARM1_WEEKDAY_BASE 0x310c

#define CYD_CLOCK_SETTINGS_VIEW_SCHEDULES_MAX 5

typedef enum {
    CYD_CLOCK_SETTINGS_VIEW_ALARM1 = 0,
    CYD_CLOCK_SETTINGS_VIEW_ALARM2,
    CYD_CLOCK_SETTINGS_VIEW_SCHEDULER,
    CYD_CLOCK_SETTINGS_VIEW_PAGE_COUNT,
} cyd_clock_settings_view_page_t;

/* app_scheduler_state_t */
typedef enum {
    CYD_CLOCK_SETTINGS_VIEW_SCHED_DISABLED = 0,
    CYD_CLOCK_SETTINGS_VIEW_SCHED_WAITING,
    CYD_CLOCK_SETTINGS_VIEW_SCHED_ACTIVE,
    CYD_CLOCK_SETTINGS_VIEW_SCHED_STOPPED,
    CYD_CLOCK_SETTINGS_VIEW_SCHED_UNKNOWN,
} cyd_clock_settings_view_sched_state_t;

typedef struct {
    unsigned slot;     /* 1-based */
    const char *owner;
    const char *tag;
    bool window;       /* runs from `at` to `to`; else fires at `at` */
    bool latched;
    bool app_scope;    /* cleared with app data; else feature scope */
    cyd_clock_settings_view_sched_state_t state;
    uint8_t at_hour, at_minute, to_hour, to_minute;
} cyd_clock_settings_view_schedule_t;

typedef struct {
    cyd_clock_settings_view_page_t page;

    /* ALARM1 / ALARM2 */
    uint8_t hour;
    uint8_t minute;
    uint8_t weekday_mask; /* ALARM1 only; bit 0 = Sunday, as app_scheduler */

    /* SCHEDULER */
    bool schedules_unavailable;
    cyd_clock_settings_view_schedule_t schedules[CYD_CLOCK_SETTINGS_VIEW_SCHEDULES_MAX];
    size_t schedule_count;
    size_t schedule_capacity;
} cyd_clock_settings_view_model_t;

/* Clears `screen` and builds the page the model selects. */
void cyd_clock_settings_view_build(cyd_display_screen_t *screen, const cyd_clock_settings_view_model_t *model);

#ifdef __cplusplus
}
#endif

#endif
