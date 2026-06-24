#ifndef ESP32_WIFI_STA_H
#define ESP32_WIFI_STA_H

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

#ifndef WIFI_SAE_PWE_METHOD_T_FALLBACK_DEFINED
#define WIFI_SAE_PWE_METHOD_T_FALLBACK_DEFINED
typedef enum {
    WPA3_SAE_PWE_UNSPECIFIED = 0,
    WPA3_SAE_PWE_HUNT_AND_PECK,
    WPA3_SAE_PWE_HASH_TO_ELEMENT,
    WPA3_SAE_PWE_BOTH,
} wifi_sae_pwe_method_t;
#endif
#endif

#if !defined(_ESP_NETIF_TYPES_H_)
typedef struct {
    uint32_t ip;
    uint32_t netmask;
    uint32_t gw;
} esp_netif_ip_info_t;
#endif
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ESP32_WIFI_STA_STATE_STOPPED = 0,
    ESP32_WIFI_STA_STATE_CONNECTING,
    ESP32_WIFI_STA_STATE_CONNECTED,
    ESP32_WIFI_STA_STATE_FAILED,
    ESP32_WIFI_STA_STATE_SCAN_MODE,
} esp32_wifi_sta_state_t;

typedef struct {
    const char *ssid;
    const char *password;
    uint8_t max_retry;
    wifi_auth_mode_t authmode_threshold;
    wifi_sae_pwe_method_t sae_pwe_h2e;
    const char *sae_h2e_identifier;
} esp32_wifi_sta_config_t;

typedef struct {
    esp32_wifi_sta_state_t state;
    uint8_t retry_count;
    bool has_ip;
    esp_netif_ip_info_t ip_info;
} esp32_wifi_sta_status_t;

typedef struct {
    char ssid[33];
    int8_t rssi;
    uint8_t channel;
    wifi_auth_mode_t authmode;
} esp32_wifi_sta_scan_record_t;

typedef enum {
    ESP32_WIFI_STA_FAILURE_NONE = 0,
    ESP32_WIFI_STA_FAILURE_NO_SAVED_PROFILE,
    ESP32_WIFI_STA_FAILURE_NO_AP_IN_RANGE,
    ESP32_WIFI_STA_FAILURE_AUTH,
    ESP32_WIFI_STA_FAILURE_TIMEOUT,
    ESP32_WIFI_STA_FAILURE_CONNECT,
} esp32_wifi_sta_failure_reason_t;

esp_err_t esp32_wifi_sta_init(void);
esp_err_t esp32_wifi_sta_init_with_config(const esp32_wifi_sta_config_t *config);
esp_err_t esp32_wifi_sta_start(void);
esp_err_t esp32_wifi_sta_stop(void);
esp_err_t esp32_wifi_sta_wait_connected(TickType_t wait_ticks);
esp_err_t esp32_wifi_sta_get_status(esp32_wifi_sta_status_t *status);
esp_err_t esp32_wifi_sta_get_configured_ssid(char *ssid, size_t ssid_size);
bool esp32_wifi_sta_has_configured_ssid(void);
esp_err_t esp32_wifi_sta_enter_scan_mode(void);
esp_err_t esp32_wifi_sta_get_scan_records(esp32_wifi_sta_scan_record_t *records,
                                          size_t record_capacity,
                                          size_t *record_count);
esp32_wifi_sta_failure_reason_t esp32_wifi_sta_get_last_failure_reason(void);
bool esp32_wifi_sta_is_connected(void);

#ifdef __cplusplus
}
#endif

#endif
