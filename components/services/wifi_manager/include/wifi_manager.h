#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stdbool.h>
#include "esp_err.h"
#include "esp32_wifi_sta.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WIFI_MANAGER_STATE_STOPPED = 0,
    WIFI_MANAGER_STATE_INIT,
    WIFI_MANAGER_STATE_OFF,
    WIFI_MANAGER_STATE_CONNECTING,
    WIFI_MANAGER_STATE_CONNECTED,
    WIFI_MANAGER_STATE_RECONNECTING,
    WIFI_MANAGER_STATE_FAILED,
    WIFI_MANAGER_STATE_SETUP_REQUIRED,
    WIFI_MANAGER_STATE_SETUP_RUNNING,
} wifi_manager_state_t;

typedef enum {
    WIFI_MANAGER_USER_RADIO_MANAGER = BIT0,
} wifi_manager_user_t;

typedef enum {
    WIFI_MANAGER_WARNING_NONE = 0,
    WIFI_MANAGER_WARNING_CONNECTED_TOO_LONG,
} wifi_manager_warning_t;

typedef void (*wifi_manager_connected_callback_t)(void *ctx);

esp_err_t wifi_manager_start(void);
void wifi_manager_request_setup_on_start(void);
void wifi_manager_set_connected_callback(wifi_manager_connected_callback_t callback, void *ctx);
esp_err_t wifi_manager_acquire(wifi_manager_user_t user);
esp_err_t wifi_manager_release(wifi_manager_user_t user);
esp_err_t wifi_manager_enable(void);
esp_err_t wifi_manager_disable(void);
esp_err_t wifi_manager_request_connection_without_setup_async(void);
esp_err_t wifi_manager_retry_connection_without_setup_async(void);
esp_err_t wifi_manager_retry_connection_without_setup(TickType_t wait_ticks);
esp_err_t wifi_manager_request_connection(TickType_t wait_ticks);
esp_err_t wifi_manager_request_connection_without_setup(TickType_t wait_ticks);
esp_err_t wifi_manager_begin_setup(void);
esp_err_t wifi_manager_complete_setup(bool connected);
esp_err_t wifi_manager_wait_connected(TickType_t wait_ticks);
esp_err_t wifi_manager_get_state(wifi_manager_state_t *state);
uint32_t wifi_manager_get_active_users(void);
wifi_manager_user_t wifi_manager_get_last_user(void);
uint32_t wifi_manager_get_connected_duration_seconds(void);
uint32_t wifi_manager_get_connected_duration_high_water_seconds(void);
wifi_manager_warning_t wifi_manager_get_warning(void);
esp32_wifi_sta_failure_reason_t wifi_manager_get_last_failure_reason(void);
bool wifi_manager_can_request_connection(void);
bool wifi_manager_is_enabled(void);
bool wifi_manager_is_setup_active(void);
bool wifi_manager_is_setup_requested_explicitly(void);

#ifdef __cplusplus
}
#endif

#endif
