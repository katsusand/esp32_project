#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "esp32_wifi_sta.h"

/*
 * Shared between wifi_connection.c and wifi_connection_manager.c only.
 *
 * `should_abort` is polled between connection steps (scan, each attempt, each
 * retry delay). Returning true stops the sequence with ESP_ERR_INVALID_STATE
 * and leaves the STA for whoever asked for it. NULL means "never abort".
 */
typedef bool (*wifi_connection_abort_fn_t)(void);

esp_err_t wifi_connection_connect_configured_abortable(TickType_t wait_ticks,
                                                       esp32_wifi_sta_failure_reason_t *failure_reason,
                                                       wifi_connection_abort_fn_t should_abort);
