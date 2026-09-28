#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "esp32_wifi_sta.h"

/*
 * Shared between wifi_connection.c (connection procedures) and
 * wifi_connection_manager.c (the manager task) only. Everything here runs on
 * the manager task.
 *
 * English contract: a procedure may operate the STA, and it waits only
 * through wifi_connection_manager_delay() and
 * wifi_connection_manager_wait_attempt(), which keep answering requests
 * meanwhile. Once wifi_connection_manager_aborted() is true, a request that
 * needs the STA is waiting: the procedure must return ESP_ERR_INVALID_STATE
 * without touching the STA again, and that request runs next.
 */
bool wifi_connection_manager_aborted(void);
/* Returns false when aborted. A zero delay only drains what is queued, which
   also drops STA events left over from an earlier attempt. */
bool wifi_connection_manager_delay(TickType_t delay_ticks);
/* Waits for the attempt just started: ESP_OK once connected, ESP_FAIL with
   *failure_reason set, ESP_ERR_TIMEOUT, or ESP_ERR_INVALID_STATE if aborted. */
esp_err_t wifi_connection_manager_wait_attempt(TickType_t wait_ticks,
                                               esp32_wifi_sta_failure_reason_t *failure_reason);

/* Scans, then tries the visible saved profiles, best first. */
esp_err_t wifi_connection_run_saved_profiles(TickType_t wait_ticks,
                                             esp32_wifi_sta_failure_reason_t *failure_reason);
/* Tests the credentials and saves them only after a successful connection. */
esp_err_t wifi_connection_run_connect_and_save(const char *ssid,
                                               const char *password,
                                               wifi_auth_mode_t authmode,
                                               TickType_t wait_ticks,
                                               esp32_wifi_sta_failure_reason_t *failure_reason);
