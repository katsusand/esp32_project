#include "wifi_profile_store.h"

esp_err_t wifi_profile_store_load_entries(wifi_profile_store_entry_t *entries,
                                          size_t entry_capacity,
                                          size_t *entry_count)
{
    (void)entries;
    (void)entry_capacity;
    if (entry_count != NULL) {
        *entry_count = 0;
    }
    return ESP_OK;
}

bool wifi_profile_store_has_profiles(void)
{
    return false;
}

esp_err_t wifi_profile_store_record_success(const char *ssid,
                                            const char *password,
                                            wifi_auth_mode_t authmode)
{
    (void)ssid;
    (void)password;
    (void)authmode;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t wifi_profile_store_set_priority(const char *ssid)
{
    (void)ssid;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t wifi_profile_store_remove(const char *ssid)
{
    (void)ssid;
    return ESP_ERR_NOT_SUPPORTED;
}
