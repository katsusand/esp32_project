#include <stdio.h>
#include <time.h>

#include "cyd_display.h"
#include "cyd_system_apps_internal.h"
#include "wifi_connection.h"

bool cyd_system_apps_touch_confirmed_action(const cyd_display_screen_t *screen,
                                            const cyd_input_event_t *event,
                                            cyd_system_apps_touch_tracker_t *tracker,
                                            uint16_t *action_id)
{
    if (event == NULL || tracker == NULL || event->type != CYD_INPUT_EVENT_TOUCH) {
        return false;
    }

    switch (event->data.touch.action) {
    case CYD_INPUT_TOUCH_ACTION_PRESS:
        tracker->pending = cyd_display_screen_hit_test(screen,
                                                       event->data.touch.x,
                                                       event->data.touch.y,
                                                       &tracker->action_id);
        tracker->long_pressed = false;
        return false;
    case CYD_INPUT_TOUCH_ACTION_LONG_PRESS:
    case CYD_INPUT_TOUCH_ACTION_REPEAT:
        if (tracker->pending) {
            tracker->long_pressed = true;
        }
        return false;
    case CYD_INPUT_TOUCH_ACTION_RELEASE: {
        uint16_t release_action_id = 0;
        bool confirmed = tracker->pending &&
                         !tracker->long_pressed &&
                         cyd_display_screen_hit_test(screen,
                                                     event->data.touch.x,
                                                     event->data.touch.y,
                                                     &release_action_id) &&
                         release_action_id == tracker->action_id;
        if (confirmed && action_id != NULL) {
            *action_id = release_action_id;
        }
        tracker->pending = false;
        tracker->long_pressed = false;
        tracker->action_id = 0;
        return confirmed;
    }
    default:
        return false;
    }
}

system_settings_view_wifi_t cyd_system_apps_view_wifi(void)
{
    wifi_connection_state_t state = WIFI_CONNECTION_STATE_STOPPED;

    if (wifi_connection_get_state(&state) != ESP_OK) {
        return SYSTEM_SETTINGS_VIEW_WIFI_UNAVAILABLE;
    }
    switch (state) {
    case WIFI_CONNECTION_STATE_STOPPED: return SYSTEM_SETTINGS_VIEW_WIFI_STOPPED;
    case WIFI_CONNECTION_STATE_INIT: return SYSTEM_SETTINGS_VIEW_WIFI_INIT;
    case WIFI_CONNECTION_STATE_OFF: return SYSTEM_SETTINGS_VIEW_WIFI_OFF;
    case WIFI_CONNECTION_STATE_CONNECTING: return SYSTEM_SETTINGS_VIEW_WIFI_CONNECTING;
    case WIFI_CONNECTION_STATE_CONNECTED: return SYSTEM_SETTINGS_VIEW_WIFI_CONNECTED;
    case WIFI_CONNECTION_STATE_RECONNECTING: return SYSTEM_SETTINGS_VIEW_WIFI_RECONNECTING;
    case WIFI_CONNECTION_STATE_FAILED: return SYSTEM_SETTINGS_VIEW_WIFI_FAILED;
    case WIFI_CONNECTION_STATE_SETUP_REQUIRED: return SYSTEM_SETTINGS_VIEW_WIFI_SETUP_REQUIRED;
    case WIFI_CONNECTION_STATE_SETUP_RUNNING: return SYSTEM_SETTINGS_VIEW_WIFI_SETUP_RUNNING;
    default: return SYSTEM_SETTINGS_VIEW_WIFI_UNAVAILABLE;
    }
}

system_settings_view_sync_t cyd_system_apps_view_sync(time_sync_state_t state)
{
    switch (state) {
    case TIME_SYNC_STATE_IDLE: return SYSTEM_SETTINGS_VIEW_SYNC_IDLE;
    case TIME_SYNC_STATE_WAITING_WIFI: return SYSTEM_SETTINGS_VIEW_SYNC_WAITING_WIFI;
    case TIME_SYNC_STATE_SYNCING: return SYSTEM_SETTINGS_VIEW_SYNC_SYNCING;
    case TIME_SYNC_STATE_RETRY_WAIT: return SYSTEM_SETTINGS_VIEW_SYNC_RETRY_WAIT;
    default: return SYSTEM_SETTINGS_VIEW_SYNC_STOPPED;
    }
}

system_settings_view_sync_last_t cyd_system_apps_view_sync_last(struct tm *at)
{
    esp_err_t last_status = ESP_OK;
    time_t last_success_at = 0;

    if (!time_sync_get_last_attempt_status(&last_status)) {
        return SYSTEM_SETTINGS_VIEW_SYNC_LAST_NONE;
    }
    if (last_status != ESP_OK) {
        return SYSTEM_SETTINGS_VIEW_SYNC_LAST_FAILED;
    }
    if (!time_sync_get_last_success_at(&last_success_at)) {
        return SYSTEM_SETTINGS_VIEW_SYNC_LAST_OK;
    }
    if (at != NULL) {
        localtime_r(&last_success_at, at);
    }
    return SYSTEM_SETTINGS_VIEW_SYNC_LAST_OK_AT;
}
