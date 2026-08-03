#ifndef CYD_SYSTEM_APPS_INTERNAL_H
#define CYD_SYSTEM_APPS_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cyd_input.h"
#include "time_sync.h"

#define CYD_SYSTEM_APPS_INPUT_POLL_MS 50
#define CYD_SYSTEM_APPS_BACK_COL 0
#define CYD_SYSTEM_APPS_BACK_ROW 0
#define CYD_SYSTEM_APPS_BACK_SPAN_COLS 6
#define CYD_SYSTEM_APPS_BACK_SPAN_ROWS 3
#define CYD_SYSTEM_APPS_TITLE_COL 8
#define CYD_SYSTEM_APPS_TITLE_ROW 0
#define CYD_SYSTEM_APPS_TITLE_SPAN_COLS 32
#define CYD_SYSTEM_APPS_TITLE_SPAN_ROWS 2

typedef struct {
    bool pending;
    bool long_pressed;
    uint16_t action_id;
} cyd_system_apps_touch_tracker_t;

bool cyd_system_apps_touch_confirmed_action(const cyd_input_event_t *event,
                                            cyd_system_apps_touch_tracker_t *tracker,
                                            uint16_t *action_id);

void cyd_system_apps_format_wifi_status(char *status_text, size_t status_size);

const char *cyd_system_apps_time_sync_state_text(time_sync_state_t state);

void cyd_system_apps_format_sync_attempt(char *status_text, size_t status_size);

#endif
