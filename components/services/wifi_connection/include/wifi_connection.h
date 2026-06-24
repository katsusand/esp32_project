#ifndef WIFI_CONNECTION_H
#define WIFI_CONNECTION_H

#include <stdbool.h>
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

esp_err_t wifi_connection_start(void);
void wifi_connection_request_setup_on_start(void);
void wifi_connection_set_connected_callback(wifi_connection_connected_callback_t callback, void *ctx);
void wifi_connection_set_connectivity_callback(wifi_connection_connectivity_callback_t callback, void *ctx);
esp_err_t wifi_connection_get_connectivity_status(wifi_connection_connectivity_status_t *status);
esp_err_t wifi_connection_acquire(wifi_connection_user_t user);
esp_err_t wifi_connection_release(wifi_connection_user_t user);
esp_err_t wifi_connection_enable(void);
esp_err_t wifi_connection_disable(void);
esp_err_t wifi_connection_request_connection_without_setup_async(void);
esp_err_t wifi_connection_retry_connection_without_setup_async(void);
esp_err_t wifi_connection_retry_connection_without_setup(TickType_t wait_ticks);
esp_err_t wifi_connection_request_connection(TickType_t wait_ticks);
esp_err_t wifi_connection_request_connection_without_setup(TickType_t wait_ticks);
esp_err_t wifi_connection_begin_setup(void);
esp_err_t wifi_connection_complete_setup(bool connected);
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

esp_err_t wifi_connection_connect_configured(TickType_t wait_ticks,
                                              esp32_wifi_sta_failure_reason_t *failure_reason);
esp_err_t wifi_connection_connect_and_save(const char *ssid,
                                           const char *password,
                                           wifi_auth_mode_t authmode,
                                           TickType_t wait_ticks,
                                           esp32_wifi_sta_failure_reason_t *failure_reason);
wifi_connection_progress_t wifi_connection_get_progress(void);

#ifdef __cplusplus
}
#endif

#endif
