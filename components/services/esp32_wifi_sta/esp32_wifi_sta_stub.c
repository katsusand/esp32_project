#include <string.h>
#include "esp32_wifi_sta.h"

esp_err_t esp32_wifi_sta_init(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t esp32_wifi_sta_init_with_config(const esp32_wifi_sta_config_t *config)
{
    (void)config;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t esp32_wifi_sta_start(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t esp32_wifi_sta_stop(void)
{
    return ESP_OK;
}

esp_err_t esp32_wifi_sta_wait_connected(TickType_t wait_ticks)
{
    (void)wait_ticks;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t esp32_wifi_sta_get_status(esp32_wifi_sta_status_t *status)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *status = (esp32_wifi_sta_status_t){
        .state = ESP32_WIFI_STA_STATE_STOPPED,
    };
    return ESP_OK;
}

esp_err_t esp32_wifi_sta_get_configured_ssid(char *ssid, size_t ssid_size)
{
    if (ssid == NULL || ssid_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    ssid[0] = '\0';
    return ESP_ERR_NOT_SUPPORTED;
}

bool esp32_wifi_sta_has_configured_ssid(void)
{
    return false;
}

esp_err_t esp32_wifi_sta_enter_scan_mode(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t esp32_wifi_sta_get_scan_records(esp32_wifi_sta_scan_record_t *records,
                                          size_t record_capacity,
                                          size_t *record_count)
{
    (void)records;
    (void)record_capacity;
    if (record_count != NULL) {
        *record_count = 0;
    }
    return ESP_ERR_NOT_SUPPORTED;
}

esp32_wifi_sta_failure_reason_t esp32_wifi_sta_get_last_failure_reason(void)
{
    return ESP32_WIFI_STA_FAILURE_NONE;
}

bool esp32_wifi_sta_is_connected(void)
{
    return false;
}
