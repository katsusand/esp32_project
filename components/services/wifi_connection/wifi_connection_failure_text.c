/* Free of the SDK, so the host tests compile it as it is. Built in the
   APP_WIFI_STA=0 stub build too. */
#include "wifi_connection.h"

const char *wifi_connection_failure_reason_text(esp32_wifi_sta_failure_reason_t reason)
{
    switch (reason) {
    case ESP32_WIFI_STA_FAILURE_NO_SAVED_PROFILE:
        return WIFI_CONNECTION_FAILURE_TEXT_NO_SAVED_PROFILE;
    case ESP32_WIFI_STA_FAILURE_NO_AP_IN_RANGE:
        return WIFI_CONNECTION_FAILURE_TEXT_NO_AP_IN_RANGE;
    case ESP32_WIFI_STA_FAILURE_AUTH:
        return WIFI_CONNECTION_FAILURE_TEXT_AUTH;
    case ESP32_WIFI_STA_FAILURE_TIMEOUT:
        return WIFI_CONNECTION_FAILURE_TEXT_TIMEOUT;
    case ESP32_WIFI_STA_FAILURE_CONNECT:
        return WIFI_CONNECTION_FAILURE_TEXT_CONNECT;
    case ESP32_WIFI_STA_FAILURE_NONE:
    default:
        return WIFI_CONNECTION_FAILURE_TEXT_UNKNOWN;
    }
}
