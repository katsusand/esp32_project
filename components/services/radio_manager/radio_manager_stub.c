#include "radio_manager.h"

static uint16_t s_radio_manager_idle_timeout_seconds = 30;

esp_err_t radio_manager_start(void)
{
    return ESP_OK;
}

esp_err_t radio_manager_acquire(const radio_manager_request_t *request,
                                radio_manager_lease_t *lease,
                                TickType_t wait_ticks)
{
    (void)request;
    (void)lease;
    (void)wait_ticks;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t radio_manager_release(const radio_manager_lease_t *lease)
{
    (void)lease;
    return ESP_OK;
}

uint16_t radio_manager_get_idle_timeout_seconds(void)
{
    return s_radio_manager_idle_timeout_seconds;
}

esp_err_t radio_manager_set_idle_timeout_seconds(uint16_t timeout_seconds)
{
    s_radio_manager_idle_timeout_seconds = timeout_seconds;
    return ESP_OK;
}

esp_err_t radio_manager_save_idle_timeout_seconds(void)
{
    return ESP_OK;
}
