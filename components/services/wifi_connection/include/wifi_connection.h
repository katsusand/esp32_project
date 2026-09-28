#ifndef WIFI_CONNECTION_H
#define WIFI_CONNECTION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp32_wifi_sta.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WIFI_CONNECTION_PROGRESS_IDLE = 0,
    WIFI_CONNECTION_PROGRESS_SEARCHING,
    WIFI_CONNECTION_PROGRESS_CONNECTING,
} wifi_connection_progress_phase_t;

typedef struct {
    wifi_connection_progress_phase_t phase;
    char ssid[33];
} wifi_connection_progress_t;

typedef enum {
    WIFI_CONNECTION_STATE_STOPPED = 0,
    WIFI_CONNECTION_STATE_INIT,
    WIFI_CONNECTION_STATE_OFF,
    WIFI_CONNECTION_STATE_CONNECTING,
    WIFI_CONNECTION_STATE_CONNECTED,
    WIFI_CONNECTION_STATE_RECONNECTING,
    WIFI_CONNECTION_STATE_FAILED,
    WIFI_CONNECTION_STATE_SETUP_REQUIRED,
    WIFI_CONNECTION_STATE_SETUP_RUNNING,
} wifi_connection_state_t;

typedef enum {
    WIFI_CONNECTION_USER_RADIO_MANAGER = BIT0,
} wifi_connection_user_t;

typedef enum {
    WIFI_CONNECTION_WARNING_NONE = 0,
    WIFI_CONNECTION_WARNING_CONNECTED_TOO_LONG,
} wifi_connection_warning_t;

typedef struct {
    bool credentials_configured;
    bool last_connection_succeeded;
} wifi_connection_connectivity_status_t;

typedef void (*wifi_connection_connected_callback_t)(void *ctx);
typedef void (*wifi_connection_connectivity_callback_t)(
    const wifi_connection_connectivity_status_t *status,
    void *ctx
);

/*
 * One task operates the STA: the manager task started by
 * wifi_connection_start().
 *
 * English contract: the functions that change Wi-Fi (acquire, release, retry
 * and the setup calls) hand a request to that task and return once it has
 * been handled, so what the caller reads afterwards already reflects it.
 * Requests run one at a time in arrival order. A connection attempt under way
 * is interrupted only by a request that needs the STA (the setup calls);
 * acquire, release and retry are answered between its steps. Getters read
 * what the task publishes and never block. Callbacks run on the manager task,
 * where the request functions return ESP_ERR_INVALID_STATE instead of waiting
 * on themselves.
 */
esp_err_t wifi_connection_start(void);
void wifi_connection_request_setup_on_start(void);
void wifi_connection_set_connected_callback(wifi_connection_connected_callback_t callback, void *ctx);
void wifi_connection_set_connectivity_callback(wifi_connection_connectivity_callback_t callback, void *ctx);
esp_err_t wifi_connection_get_connectivity_status(wifi_connection_connectivity_status_t *status);
/* Once counted, the state is CONNECTED, CONNECTING / RECONNECTING, or a setup
   state; a stale FAILED from an earlier attempt is never visible here. */
esp_err_t wifi_connection_acquire(wifi_connection_user_t user);
/* Wi-Fi turns off once the last user is gone and any attempt has finished. */
esp_err_t wifi_connection_release(wifi_connection_user_t user);
/* Starts an attempt with the saved profiles and returns once it is accepted
   (CONNECTING), without waiting for its result. ESP_OK with nothing to do when
   already CONNECTED; ESP_ERR_INVALID_STATE during setup or another attempt. */
esp_err_t wifi_connection_retry_connection_without_setup_async(void);
/*
 * ESP_OK once connected, ESP_ERR_TIMEOUT otherwise. ESP_ERR_NOT_FINISHED while
 * Wi-Fi setup has the STA: a pause, not a failure. When setup ends, a user
 * that is still acquired is resumed: connected with the new or the saved
 * profiles, or failed with the reason (no saved profile, AP not found, ...).
 */
esp_err_t wifi_connection_wait_connected(TickType_t wait_ticks);
esp_err_t wifi_connection_get_state(wifi_connection_state_t *state);
uint32_t wifi_connection_get_active_users(void);
wifi_connection_user_t wifi_connection_get_last_user(void);
uint32_t wifi_connection_get_connected_duration_seconds(void);
uint32_t wifi_connection_get_connected_duration_high_water_seconds(void);
wifi_connection_warning_t wifi_connection_get_warning(void);
esp32_wifi_sta_failure_reason_t wifi_connection_get_last_failure_reason(void);
bool wifi_connection_can_request_connection(void);
bool wifi_connection_is_enabled(void);
bool wifi_connection_is_setup_active(void);
bool wifi_connection_is_setup_requested_explicitly(void);
wifi_connection_progress_t wifi_connection_get_progress(void);

/*
 * Setup, for the setup UI. begin_setup stops normal connection handling and
 * gives setup the STA (SETUP_RUNNING). scan and connect_and_save are accepted
 * only in that state, and complete_setup ends it: CONNECTED when the tested
 * profile is still up, otherwise OFF.
 *
 * scan blocks for one scan and reports how many records it copied.
 * connect_and_save makes up to CONFIG_ESP32_WIFI_STA_MAX_RETRY + 1 fresh
 * attempts of `wait_ticks` each and saves the profile only after one of them
 * connects; the caller blocks until then.
 */
esp_err_t wifi_connection_begin_setup(void);
esp_err_t wifi_connection_setup_scan(esp32_wifi_sta_scan_record_t *records,
                                     size_t record_capacity,
                                     size_t *record_count);
esp_err_t wifi_connection_connect_and_save(const char *ssid,
                                           const char *password,
                                           wifi_auth_mode_t authmode,
                                           TickType_t wait_ticks,
                                           esp32_wifi_sta_failure_reason_t *failure_reason);
esp_err_t wifi_connection_complete_setup(bool connected);

#ifdef __cplusplus
}
#endif

#endif
