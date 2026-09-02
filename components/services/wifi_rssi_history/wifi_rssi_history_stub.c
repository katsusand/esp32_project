#include "wifi_rssi_history.h"

/*
 * APP_WIFI_STA=0 build. The API stays callable so app code does not need
 * #if guards; callers simply observe "no history available".
 */

esp_err_t wifi_rssi_history_start(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

bool wifi_rssi_history_get(const int16_t **samples, uint16_t *count, uint16_t *revision)
{
    (void)samples;
    (void)count;
    (void)revision;
    return false;
}

bool wifi_rssi_history_get_latest(int16_t *rssi_dbm)
{
    (void)rssi_dbm;
    return false;
}
