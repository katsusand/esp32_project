#ifndef WIFI_PROFILE_STORE_H
#define WIFI_PROFILE_STORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_wifi_types.h"

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

#ifdef __cplusplus
}
#endif

#endif
