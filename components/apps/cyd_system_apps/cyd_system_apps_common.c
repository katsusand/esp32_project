#include <stdio.h>
#include <time.h>

#include "cyd_display.h"
#include "cyd_system_apps_internal.h"
#include "wifi_connection.h"

bool cyd_system_apps_touch_confirmed_action(const cyd_input_event_t *event,
                                            cyd_system_apps_touch_tracker_t *tracker,
                                            uint16_t *action_id)
{
    if (event == NULL || tracker == NULL || event->type != CYD_INPUT_EVENT_TOUCH) {
        return false;
    }

    switch (event->data.touch.action) {
    case CYD_INPUT_TOUCH_ACTION_PRESS:
        tracker->pending = cyd_display_hit_test_action(event->data.touch.x,
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
                         cyd_display_hit_test_action(event->data.touch.x,
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

static const char *cyd_system_apps_wifi_state_text(wifi_connection_state_t state)
{
    switch (state) {
    case WIFI_CONNECTION_STATE_STOPPED:
        return "wifi: stopped";
    case WIFI_CONNECTION_STATE_INIT:
        return "wifi: init";
    case WIFI_CONNECTION_STATE_OFF:
        return "wifi: off";
    case WIFI_CONNECTION_STATE_CONNECTING:
        return "wifi: connecting";
    case WIFI_CONNECTION_STATE_CONNECTED:
        return "wifi: connected";
    case WIFI_CONNECTION_STATE_RECONNECTING:
        return "wifi: reconnecting";
    case WIFI_CONNECTION_STATE_FAILED:
        return "wifi: failed";
    case WIFI_CONNECTION_STATE_SETUP_REQUIRED:
        return "wifi: setup needed";
    case WIFI_CONNECTION_STATE_SETUP_RUNNING:
        return "wifi: setup";
    default:
        return "wifi: unknown";
    }
}

void cyd_system_apps_format_wifi_status(char *status_text, size_t status_size)
{
    wifi_connection_state_t state = WIFI_CONNECTION_STATE_STOPPED;

    if (status_text == NULL || status_size == 0) {
        return;
    }

    if (wifi_connection_get_state(&state) != ESP_OK) {
        snprintf(status_text, status_size, "wifi: unavailable");
        return;
    }

    snprintf(status_text, status_size, "%s", cyd_system_apps_wifi_state_text(state));
}

const char *cyd_system_apps_time_sync_state_text(time_sync_state_t state)
{
    switch (state) {
    case TIME_SYNC_STATE_STOPPED:
        return "stopped";
    case TIME_SYNC_STATE_IDLE:
        return "idle";
    case TIME_SYNC_STATE_WAITING_WIFI:
        return "waiting wifi";
    case TIME_SYNC_STATE_SYNCING:
        return "syncing";
    case TIME_SYNC_STATE_RETRY_WAIT:
        return "retry wait";
    default:
        return "unknown";
    }
}

void cyd_system_apps_format_sync_attempt(char *status_text, size_t status_size)
{
    esp_err_t last_status = ESP_OK;
    time_t last_success_at = 0;

    if (status_text == NULL || status_size == 0) {
        return;
    }

    if (!time_sync_get_last_attempt_status(&last_status)) {
        snprintf(status_text, status_size, "sync last: pending");
        return;
    }

    if (last_status != ESP_OK) {
        snprintf(status_text, status_size, "sync last: failed");
        return;
    }

    if (!time_sync_get_last_success_at(&last_success_at)) {
        snprintf(status_text, status_size, "sync last: ok");
        return;
    }

    {
        struct tm sync_time = { 0 };
        localtime_r(&last_success_at, &sync_time);
        strftime(status_text, status_size, "sync last: %m-%d %H:%M", &sync_time);
    }
}
