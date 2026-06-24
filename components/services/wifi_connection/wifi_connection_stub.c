#include "wifi_connection.h"

static wifi_connection_connectivity_callback_t s_connectivity_callback;
static void *s_connectivity_callback_ctx;

esp_err_t wifi_connection_start(void)
{
    return ESP_OK;
}

void wifi_connection_request_setup_on_start(void)
{
}

void wifi_connection_set_connected_callback(wifi_connection_connected_callback_t callback, void *ctx)
{
    (void)callback;
    (void)ctx;
}

void wifi_connection_set_connectivity_callback(wifi_connection_connectivity_callback_t callback, void *ctx)
{
    s_connectivity_callback = callback;
    s_connectivity_callback_ctx = ctx;
    if (s_connectivity_callback != NULL) {
        const wifi_connection_connectivity_status_t status = { 0 };
        s_connectivity_callback(&status, s_connectivity_callback_ctx);
    }
}

esp_err_t wifi_connection_get_connectivity_status(wifi_connection_connectivity_status_t *status)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *status = (wifi_connection_connectivity_status_t){ 0 };
    return ESP_OK;
}

esp_err_t wifi_connection_acquire(wifi_connection_user_t user)
{
    (void)user;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t wifi_connection_release(wifi_connection_user_t user)
{
    (void)user;
    return ESP_OK;
}

esp_err_t wifi_connection_enable(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t wifi_connection_disable(void)
{
    return ESP_OK;
}

esp_err_t wifi_connection_request_connection_without_setup_async(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t wifi_connection_retry_connection_without_setup_async(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t wifi_connection_retry_connection_without_setup(TickType_t wait_ticks)
{
    (void)wait_ticks;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t wifi_connection_request_connection(TickType_t wait_ticks)
{
    (void)wait_ticks;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t wifi_connection_request_connection_without_setup(TickType_t wait_ticks)
{
    (void)wait_ticks;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t wifi_connection_begin_setup(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t wifi_connection_complete_setup(bool connected)
{
    (void)connected;
    return ESP_OK;
}

esp_err_t wifi_connection_wait_connected(TickType_t wait_ticks)
{
    (void)wait_ticks;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t wifi_connection_get_state(wifi_connection_state_t *state)
{
    if (state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *state = WIFI_CONNECTION_STATE_STOPPED;
    return ESP_OK;
}

uint32_t wifi_connection_get_active_users(void)
{
    return 0;
}

wifi_connection_user_t wifi_connection_get_last_user(void)
{
    return 0;
}

uint32_t wifi_connection_get_connected_duration_seconds(void)
{
    return 0;
}

uint32_t wifi_connection_get_connected_duration_high_water_seconds(void)
{
    return 0;
}

wifi_connection_warning_t wifi_connection_get_warning(void)
{
    return WIFI_CONNECTION_WARNING_NONE;
}

esp32_wifi_sta_failure_reason_t wifi_connection_get_last_failure_reason(void)
{
    return ESP32_WIFI_STA_FAILURE_NONE;
}

bool wifi_connection_can_request_connection(void)
{
    return false;
}

bool wifi_connection_is_enabled(void)
{
    return false;
}

bool wifi_connection_is_setup_active(void)
{
    return false;
}

bool wifi_connection_is_setup_requested_explicitly(void)
{
    return false;
}

esp_err_t wifi_connection_connect_configured(TickType_t wait_ticks,
                                             esp32_wifi_sta_failure_reason_t *failure_reason)
{
    (void)wait_ticks;
    if (failure_reason != NULL) {
        *failure_reason = ESP32_WIFI_STA_FAILURE_NONE;
    }
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t wifi_connection_connect_and_save(const char *ssid,
                                           const char *password,
                                           wifi_auth_mode_t authmode,
                                           TickType_t wait_ticks,
                                           esp32_wifi_sta_failure_reason_t *failure_reason)
{
    (void)ssid;
    (void)password;
    (void)authmode;
    (void)wait_ticks;
    if (failure_reason != NULL) {
        *failure_reason = ESP32_WIFI_STA_FAILURE_NONE;
    }
    return ESP_ERR_NOT_SUPPORTED;
}

wifi_connection_progress_t wifi_connection_get_progress(void)
{
    return (wifi_connection_progress_t){ 0 };
}
