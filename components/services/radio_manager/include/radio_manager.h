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
} radio_manager_client_t;

typedef enum {
    RADIO_MANAGER_CAP_INTERNET = BIT0,
} radio_manager_capability_t;

typedef struct {
    radio_manager_client_t client;
    radio_manager_capability_t required;
    TickType_t max_hold_ticks;
} radio_manager_request_t;

typedef struct {
    radio_manager_client_t client;
    uint32_t token;
} radio_manager_lease_t;

esp_err_t radio_manager_start(void);
esp_err_t radio_manager_acquire(const radio_manager_request_t *request,
                                radio_manager_lease_t *lease,
                                TickType_t wait_ticks);
esp_err_t radio_manager_release(const radio_manager_lease_t *lease);
uint16_t radio_manager_get_idle_timeout_seconds(void);
esp_err_t radio_manager_set_idle_timeout_seconds(uint16_t timeout_seconds);
esp_err_t radio_manager_save_idle_timeout_seconds(void);

#ifdef __cplusplus
}
#endif

#endif
