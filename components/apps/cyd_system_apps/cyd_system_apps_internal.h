#ifndef CYD_SYSTEM_APPS_INTERNAL_H
#define CYD_SYSTEM_APPS_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cyd_display.h"
#include "cyd_input.h"
#include "time_sync.h"
#include "system_settings_view.h"

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

/* Hit-tests `screen`, the calling app's own screen (see cyd_display_screen_hit_test). */
bool cyd_system_apps_touch_confirmed_action(const cyd_display_screen_t *screen,
                                            const cyd_input_event_t *event,
                                            cyd_system_apps_touch_tracker_t *tracker,
                                            uint16_t *action_id);

/*
 * Service states as the views' own enums (system_settings_view.h), which keep
 * the views free of service headers. Shared by the settings and info apps.
 */
system_settings_view_wifi_t cyd_system_apps_view_wifi(void);
system_settings_view_sync_t cyd_system_apps_view_sync(time_sync_state_t state);
/* Fills `at` (may be NULL) only for SYSTEM_SETTINGS_VIEW_SYNC_LAST_OK_AT. */
system_settings_view_sync_last_t cyd_system_apps_view_sync_last(struct tm *at);

#endif
