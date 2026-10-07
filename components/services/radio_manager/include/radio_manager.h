#ifndef RADIO_MANAGER_H
#define RADIO_MANAGER_H

#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RADIO_MANAGER_CLIENT_TIME_SYNC = 0,
    RADIO_MANAGER_CLIENT_COUNT, /* not a client; sizes the per-client reply slots */
} radio_manager_client_t;

typedef enum {
    RADIO_MANAGER_CAP_INTERNET = BIT0,
} radio_manager_capability_t;

typedef struct {
    radio_manager_client_t client;
    radio_manager_capability_t required;
    TickType_t max_hold_ticks;
} radio_manager_request_t;

/*
 * Why an acquire failed, for the caller's own log line. The return value of
 * radio_manager_acquire() keeps its meaning; this only says more about it.
 *
 * English contract: decided by the manager when it rejects that request (the
 * Wi-Fi state changes afterwards), and carried back with that request's reply,
 * so a client never reads another client's failure. 0 means no detail: a
 * success, or a lease the caller only zeroed.
 */
typedef enum {
    RADIO_MANAGER_FAILURE_NONE = 0,
    RADIO_MANAGER_FAILURE_INVALID_REQUEST,      /* ESP_ERR_INVALID_ARG */
    RADIO_MANAGER_FAILURE_NOT_STARTED,          /* ESP_ERR_INVALID_STATE */
    RADIO_MANAGER_FAILURE_NOT_SUPPORTED,        /* ESP_ERR_NOT_SUPPORTED: capability */
    RADIO_MANAGER_FAILURE_DISABLED,             /* ESP_ERR_NOT_SUPPORTED: APP_WIFI_STA=0 build */
    RADIO_MANAGER_FAILURE_WIFI_UNAVAILABLE,     /* wifi_connection not running */
    RADIO_MANAGER_FAILURE_WIFI_SETUP_PAUSED,    /* ESP_ERR_NOT_FINISHED: not a failure */
    RADIO_MANAGER_FAILURE_WIFI_SETUP_REQUIRED,  /* ESP_FAIL: no Wi-Fi configured */
    RADIO_MANAGER_FAILURE_WIFI_CONNECT_FAILED,  /* ESP_FAIL: see wifi_failure_reason */
    RADIO_MANAGER_FAILURE_QUEUE_FULL,           /* ESP_ERR_TIMEOUT: request not accepted */
    RADIO_MANAGER_FAILURE_RADIO_BUSY,           /* ESP_ERR_TIMEOUT: another request had the radio */
    RADIO_MANAGER_FAILURE_WIFI_CONNECT_TIMEOUT, /* ESP_ERR_TIMEOUT: Wi-Fi still connecting */
} radio_manager_failure_t;

typedef struct {
    radio_manager_client_t client;
    uint32_t token;
    /* Written by radio_manager_acquire(): the reason when it fails, cleared when
       it succeeds. Read them through radio_manager_lease_failure_text(). */
    radio_manager_failure_t failure;
    /* An esp32_wifi_sta_failure_reason_t value, set with WIFI_CONNECT_FAILED.
       A plain integer so that this header needs no Wi-Fi types. */
    uint8_t wifi_failure_reason;
} radio_manager_lease_t;

esp_err_t radio_manager_start(void);
/*
 * ESP_OK with a lease once the capability is ready. ESP_ERR_NOT_FINISHED when
 * Wi-Fi setup has paused the connection: not a failure, try again after
 * setup. Other errors are real failures (no connection, timeout); the lease
 * then says why (radio_manager_lease_failure_text()).
 */
esp_err_t radio_manager_acquire(const radio_manager_request_t *request,
                                radio_manager_lease_t *lease,
                                TickType_t wait_ticks);
esp_err_t radio_manager_release(const radio_manager_lease_t *lease);
/*
 * The reason a failed acquire left in the lease, as words for a log line:
 *
 *     ERROR_LOG(err, "time sync failed: no Internet connection (%s)",
 *               radio_manager_lease_failure_text(&lease));
 *
 * English contract: never NULL; "no detail" for a NULL or zeroed lease. Short,
 * ASCII and stable, without SSID or anything that changes between failures, so
 * the error log can fold repeated lines.
 */
const char *radio_manager_lease_failure_text(const radio_manager_lease_t *lease);
uint16_t radio_manager_get_idle_timeout_seconds(void);
esp_err_t radio_manager_set_idle_timeout_seconds(uint16_t timeout_seconds);
esp_err_t radio_manager_save_idle_timeout_seconds(void);

#ifdef __cplusplus
}
#endif

#endif
