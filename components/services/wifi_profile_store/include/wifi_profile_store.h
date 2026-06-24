#ifndef WIFI_PROFILE_STORE_H
#define WIFI_PROFILE_STORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "sdkconfig.h"
#if !defined(__ESP_WIFI_TYPES_H__)
#ifndef WIFI_AUTH_MODE_T_FALLBACK_DEFINED
#define WIFI_AUTH_MODE_T_FALLBACK_DEFINED
typedef enum {
    WIFI_AUTH_OPEN = 0,
    WIFI_AUTH_WEP,
    WIFI_AUTH_WPA_PSK,
    WIFI_AUTH_WPA2_PSK,
    WIFI_AUTH_WPA_WPA2_PSK,
    WIFI_AUTH_WPA3_PSK,
    WIFI_AUTH_WPA2_WPA3_PSK,
    WIFI_AUTH_WAPI_PSK,
} wifi_auth_mode_t;
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_PROFILE_STORE_MAX_ENTRIES 5

typedef struct {
    char ssid[33];
    char password[65];
    wifi_auth_mode_t authmode;
    uint32_t last_success_seq;
} wifi_profile_store_entry_t;

esp_err_t wifi_profile_store_load_entries(wifi_profile_store_entry_t *entries,
                                          size_t entry_capacity,
                                          size_t *entry_count);
bool wifi_profile_store_has_profiles(void);
esp_err_t wifi_profile_store_record_success(const char *ssid,
                                            const char *password,
                                            wifi_auth_mode_t authmode);
esp_err_t wifi_profile_store_set_priority(const char *ssid);
esp_err_t wifi_profile_store_remove(const char *ssid);

#ifdef __cplusplus
}
#endif

#endif
