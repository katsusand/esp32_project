#include "radio_manager_failure.h"

#define RADIO_MANAGER_CONNECT_FAILED_TEXT "Wi-Fi connect failed: "

radio_manager_failure_t radio_manager_failure_from_wifi_state(wifi_connection_state_t state)
{
    switch (state) {
    case WIFI_CONNECTION_STATE_FAILED:
        return RADIO_MANAGER_FAILURE_WIFI_CONNECT_FAILED;
    case WIFI_CONNECTION_STATE_SETUP_REQUIRED:
        return RADIO_MANAGER_FAILURE_WIFI_SETUP_REQUIRED;
    case WIFI_CONNECTION_STATE_STOPPED:
        /* Only before wifi_connection_start(), and then its acquire already
           failed; kept so that the wait can never hang on it. */
        return RADIO_MANAGER_FAILURE_WIFI_UNAVAILABLE;
    case WIFI_CONNECTION_STATE_INIT:
    case WIFI_CONNECTION_STATE_OFF:
    case WIFI_CONNECTION_STATE_CONNECTING:
    case WIFI_CONNECTION_STATE_CONNECTED:
    case WIFI_CONNECTION_STATE_RECONNECTING:
    case WIFI_CONNECTION_STATE_SETUP_RUNNING:
    default:
        return RADIO_MANAGER_FAILURE_NONE;
    }
}

/* Whole strings rather than a prefix joined at run time: the text needs no
   buffer, and the reason words still come from wifi_connection.h. */
static const char *radio_manager_connect_failed_text(uint8_t wifi_failure_reason)
{
    switch ((esp32_wifi_sta_failure_reason_t)wifi_failure_reason) {
    case ESP32_WIFI_STA_FAILURE_NO_SAVED_PROFILE:
        return RADIO_MANAGER_CONNECT_FAILED_TEXT WIFI_CONNECTION_FAILURE_TEXT_NO_SAVED_PROFILE;
    case ESP32_WIFI_STA_FAILURE_NO_AP_IN_RANGE:
        return RADIO_MANAGER_CONNECT_FAILED_TEXT WIFI_CONNECTION_FAILURE_TEXT_NO_AP_IN_RANGE;
    case ESP32_WIFI_STA_FAILURE_AUTH:
        return RADIO_MANAGER_CONNECT_FAILED_TEXT WIFI_CONNECTION_FAILURE_TEXT_AUTH;
    case ESP32_WIFI_STA_FAILURE_TIMEOUT:
        return RADIO_MANAGER_CONNECT_FAILED_TEXT WIFI_CONNECTION_FAILURE_TEXT_TIMEOUT;
    case ESP32_WIFI_STA_FAILURE_CONNECT:
        return RADIO_MANAGER_CONNECT_FAILED_TEXT WIFI_CONNECTION_FAILURE_TEXT_CONNECT;
    case ESP32_WIFI_STA_FAILURE_NONE:
    default:
        return RADIO_MANAGER_CONNECT_FAILED_TEXT WIFI_CONNECTION_FAILURE_TEXT_UNKNOWN;
    }
}

const char *radio_manager_failure_text(radio_manager_failure_t failure, uint8_t wifi_failure_reason)
{
    switch (failure) {
    case RADIO_MANAGER_FAILURE_NONE:
        return "no detail";
    case RADIO_MANAGER_FAILURE_INVALID_REQUEST:
        return "invalid request";
    case RADIO_MANAGER_FAILURE_NOT_STARTED:
        return "radio manager not started";
    case RADIO_MANAGER_FAILURE_NOT_SUPPORTED:
        return "capability not supported";
    case RADIO_MANAGER_FAILURE_DISABLED:
        return "Wi-Fi disabled in this build";
    case RADIO_MANAGER_FAILURE_WIFI_UNAVAILABLE:
        return "Wi-Fi not available";
    case RADIO_MANAGER_FAILURE_WIFI_SETUP_PAUSED:
        return "paused for Wi-Fi setup";
    case RADIO_MANAGER_FAILURE_WIFI_SETUP_REQUIRED:
        return "Wi-Fi setup required";
    case RADIO_MANAGER_FAILURE_WIFI_CONNECT_FAILED:
        return radio_manager_connect_failed_text(wifi_failure_reason);
    case RADIO_MANAGER_FAILURE_QUEUE_FULL:
        return "timed out: request queue full";
    case RADIO_MANAGER_FAILURE_RADIO_BUSY:
        return "timed out: radio in use by another request";
    case RADIO_MANAGER_FAILURE_WIFI_CONNECT_TIMEOUT:
        return "timed out waiting for Wi-Fi to connect";
    default:
        return "unknown";
    }
}

const char *radio_manager_lease_failure_text(const radio_manager_lease_t *lease)
{
    if (lease == NULL) {
        return radio_manager_failure_text(RADIO_MANAGER_FAILURE_NONE, 0);
    }
    return radio_manager_failure_text(lease->failure, lease->wifi_failure_reason);
}
