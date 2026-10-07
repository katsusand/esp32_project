#pragma once

#include <stdint.h>
#include "radio_manager.h"
#include "wifi_connection.h"

/*
 * Which failure a Wi-Fi state means, and the words for each failure. Free of
 * the SDK so that the host tests compile it as it is.
 */

/*
 * English contract: NONE for CONNECTED and for every state that is still on its
 * way (INIT, OFF, CONNECTING, RECONNECTING, SETUP_RUNNING), where the manager
 * keeps waiting. Only FAILED, SETUP_REQUIRED and STOPPED end the wait.
 */
radio_manager_failure_t radio_manager_failure_from_wifi_state(wifi_connection_state_t state);

/*
 * English contract: never NULL. wifi_failure_reason matters only with
 * WIFI_CONNECT_FAILED. An unknown failure value gives "unknown".
 */
const char *radio_manager_failure_text(radio_manager_failure_t failure, uint8_t wifi_failure_reason);
