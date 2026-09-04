#ifndef CYD_SYSTEM_APPS_INTERNAL_H
#define CYD_SYSTEM_APPS_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cyd_input.h"
#include "time_sync.h"

#define CYD_SYSTEM_APPS_INPUT_POLL_MS 50

/*
 * Settings chrome geometry now lives in cyd_ui (cyd_ui_add_settings_title,
 * _back, _page_nav). It was duplicated here, in cyd_clock_settings_app and in
 * cyd_time_punch_settings_app, and the copies had already drifted apart.
 */

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
