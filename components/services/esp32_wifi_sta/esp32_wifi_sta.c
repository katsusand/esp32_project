#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "sdkconfig.h"
#include "esp32_wifi_sta.h"
#include "wifi_profile_store.h"

#ifndef CONFIG_ESP32_WIFI_STA_SSID
#define CONFIG_ESP32_WIFI_STA_SSID ""
#endif

#ifndef CONFIG_ESP32_WIFI_STA_PASSWORD
#define CONFIG_ESP32_WIFI_STA_PASSWORD ""
#endif

#ifndef CONFIG_ESP32_WIFI_STA_MAX_RETRY
#define CONFIG_ESP32_WIFI_STA_MAX_RETRY 5
#endif

#ifndef CONFIG_ESP32_WIFI_STA_SCAN_LIST_SIZE
#define CONFIG_ESP32_WIFI_STA_SCAN_LIST_SIZE 30
#endif

#ifndef CONFIG_ESP32_WIFI_STA_SAE_H2E_IDENTIFIER
#define CONFIG_ESP32_WIFI_STA_SAE_H2E_IDENTIFIER ""
#endif

#if CONFIG_ESP32_WIFI_STA_WPA3_SAE_PWE_HUNT_AND_PECK
#define ESP32_WIFI_STA_SAE_MODE WPA3_SAE_PWE_HUNT_AND_PECK
#elif CONFIG_ESP32_WIFI_STA_WPA3_SAE_PWE_HASH_TO_ELEMENT
#define ESP32_WIFI_STA_SAE_MODE WPA3_SAE_PWE_HASH_TO_ELEMENT
#else
#define ESP32_WIFI_STA_SAE_MODE WPA3_SAE_PWE_BOTH
#endif

#if CONFIG_ESP32_WIFI_STA_AUTH_OPEN
#define ESP32_WIFI_STA_AUTH_MODE_THRESHOLD WIFI_AUTH_OPEN
#elif CONFIG_ESP32_WIFI_STA_AUTH_WEP
#define ESP32_WIFI_STA_AUTH_MODE_THRESHOLD WIFI_AUTH_WEP
#elif CONFIG_ESP32_WIFI_STA_AUTH_WPA_PSK
#define ESP32_WIFI_STA_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_PSK
#elif CONFIG_ESP32_WIFI_STA_AUTH_WPA_WPA2_PSK
#define ESP32_WIFI_STA_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_WPA2_PSK
#elif CONFIG_ESP32_WIFI_STA_AUTH_WPA3_PSK
#define ESP32_WIFI_STA_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA3_PSK
#elif CONFIG_ESP32_WIFI_STA_AUTH_WPA2_WPA3_PSK
#define ESP32_WIFI_STA_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_WPA3_PSK
#elif CONFIG_ESP32_WIFI_STA_AUTH_WAPI_PSK
#define ESP32_WIFI_STA_AUTH_MODE_THRESHOLD WIFI_AUTH_WAPI_PSK
#else
#define ESP32_WIFI_STA_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK
#endif

#define ESP32_WIFI_STA_CONNECTED_BIT BIT0
#define ESP32_WIFI_STA_FAIL_BIT      BIT1

static const char *TAG = "esp32_wifi_sta";

typedef struct {
    bool initialized;
    bool started;
    bool configured;
    bool connect_on_start;
    bool has_ip;
    uint8_t retry_count;
    size_t scan_record_count;
    esp32_wifi_sta_state_t state;
    esp32_wifi_sta_failure_reason_t last_failure_reason;
    wifi_err_reason_t last_disconnect_reason;
    esp32_wifi_sta_config_t config;
    esp_netif_ip_info_t ip_info;
    esp32_wifi_sta_scan_record_t scan_records[CONFIG_ESP32_WIFI_STA_SCAN_LIST_SIZE];
    esp_netif_t *netif;
    EventGroupHandle_t event_group;
    SemaphoreHandle_t mutex;
} esp32_wifi_sta_ctx_t;

static esp32_wifi_sta_ctx_t s_wifi_sta;
static char s_saved_ssid[33];
static char s_saved_password[65];

static esp_err_t esp32_wifi_sta_register_handlers(void);

static esp32_wifi_sta_failure_reason_t esp32_wifi_sta_map_disconnect_reason(wifi_err_reason_t reason)
{
    switch (reason) {
    case WIFI_REASON_NO_AP_FOUND:
    case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
    case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
    case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD:
        return ESP32_WIFI_STA_FAILURE_NO_AP_IN_RANGE;
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_AUTH_EXPIRE:
    case WIFI_REASON_AUTH_LEAVE:
    case WIFI_REASON_ASSOC_FAIL:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_802_1X_AUTH_FAILED:
    case WIFI_REASON_BAD_CIPHER_OR_AKM:
        return ESP32_WIFI_STA_FAILURE_AUTH;
    case WIFI_REASON_BEACON_TIMEOUT:
    case WIFI_REASON_TIMEOUT:
        return ESP32_WIFI_STA_FAILURE_TIMEOUT;
    default:
        return ESP32_WIFI_STA_FAILURE_CONNECT;
    }
}

static esp32_wifi_sta_config_t esp32_wifi_sta_default_config(void)
{
    const char *ssid = CONFIG_ESP32_WIFI_STA_SSID;
    const char *password = CONFIG_ESP32_WIFI_STA_PASSWORD;

    if (ssid[0] == '\0') {
        wifi_profile_store_entry_t entries[1] = { 0 };
        size_t entry_count = 0;
        if (wifi_profile_store_load_entries(entries, 1, &entry_count) == ESP_OK &&
            entry_count > 0 &&
            entries[0].ssid[0] != '\0') {
            strlcpy(s_saved_ssid, entries[0].ssid, sizeof(s_saved_ssid));
            strlcpy(s_saved_password, entries[0].password, sizeof(s_saved_password));
            ssid = s_saved_ssid;
            password = s_saved_password;
        }
    }

    return (esp32_wifi_sta_config_t) {
        .ssid = ssid,
        .password = password,
        .max_retry = CONFIG_ESP32_WIFI_STA_MAX_RETRY,
        .authmode_threshold = ESP32_WIFI_STA_AUTH_MODE_THRESHOLD,
        .sae_pwe_h2e = ESP32_WIFI_STA_SAE_MODE,
        .sae_h2e_identifier = CONFIG_ESP32_WIFI_STA_SAE_H2E_IDENTIFIER,
    };
}

static void esp32_wifi_sta_set_state(esp32_wifi_sta_state_t state)
{
    if (xSemaphoreTake(s_wifi_sta.mutex, portMAX_DELAY) == pdTRUE) {
        s_wifi_sta.state = state;
        xSemaphoreGive(s_wifi_sta.mutex);
    }
}

static void esp32_wifi_sta_clear_ip(void)
{
    if (xSemaphoreTake(s_wifi_sta.mutex, portMAX_DELAY) == pdTRUE) {
        s_wifi_sta.has_ip = false;
        memset(&s_wifi_sta.ip_info, 0, sizeof(s_wifi_sta.ip_info));
        xSemaphoreGive(s_wifi_sta.mutex);
    }
}

static esp_err_t esp32_wifi_sta_ensure_driver(void)
{
    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err;

    if (s_wifi_sta.initialized) {
        return ESP_OK;
    }

    s_wifi_sta.event_group = xEventGroupCreate();
    ESP_RETURN_ON_FALSE(s_wifi_sta.event_group != NULL, ESP_ERR_NO_MEM, TAG, "event group alloc failed");

    s_wifi_sta.mutex = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_wifi_sta.mutex != NULL, ESP_ERR_NO_MEM, TAG, "mutex alloc failed");

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "esp_netif init failed");
    err = esp_event_loop_create_default();
    ESP_RETURN_ON_FALSE(err == ESP_OK || err == ESP_ERR_INVALID_STATE,
                        err,
                        TAG,
                        "event loop init failed");

    s_wifi_sta.netif = esp_netif_create_default_wifi_sta();
    ESP_RETURN_ON_FALSE(s_wifi_sta.netif != NULL, ESP_ERR_NO_MEM, TAG, "Wi-Fi netif create failed");

    ESP_RETURN_ON_ERROR(esp_wifi_init(&init_config), TAG, "Wi-Fi init failed");
    ESP_RETURN_ON_ERROR(esp32_wifi_sta_register_handlers(), TAG, "event handler register failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "set Wi-Fi mode failed");

    s_wifi_sta.retry_count = 0;
    s_wifi_sta.has_ip = false;
    s_wifi_sta.state = ESP32_WIFI_STA_STATE_STOPPED;
    s_wifi_sta.last_failure_reason = ESP32_WIFI_STA_FAILURE_NONE;
    s_wifi_sta.last_disconnect_reason = WIFI_REASON_UNSPECIFIED;
    s_wifi_sta.initialized = true;

    return ESP_OK;
}

static void esp32_wifi_sta_store_scan_records(const wifi_ap_record_t *ap_records, uint16_t ap_count)
{
    uint16_t record_limit = ap_count < CONFIG_ESP32_WIFI_STA_SCAN_LIST_SIZE ?
                            ap_count :
                            CONFIG_ESP32_WIFI_STA_SCAN_LIST_SIZE;

    if (xSemaphoreTake(s_wifi_sta.mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }

    memset(s_wifi_sta.scan_records, 0, sizeof(s_wifi_sta.scan_records));
    s_wifi_sta.scan_record_count = 0;

    for (uint16_t i = 0; i < record_limit; ++i) {
        esp32_wifi_sta_scan_record_t *record = &s_wifi_sta.scan_records[s_wifi_sta.scan_record_count];
        size_t ssid_len = strnlen((const char *)ap_records[i].ssid, sizeof(ap_records[i].ssid));

        if (ssid_len == 0) {
            continue;
        }

        if (ssid_len >= sizeof(record->ssid)) {
            ssid_len = sizeof(record->ssid) - 1;
        }
        memcpy(record->ssid, ap_records[i].ssid, ssid_len);
        record->ssid[ssid_len] = '\0';
        record->rssi = ap_records[i].rssi;
        record->channel = ap_records[i].primary;
        record->authmode = ap_records[i].authmode;
        ++s_wifi_sta.scan_record_count;

        ESP_LOGI(TAG,
                 "scan[%u]: ssid=\"%s\" rssi=%d channel=%u auth=%d",
                 (unsigned)(s_wifi_sta.scan_record_count - 1),
                 record->ssid,
                 record->rssi,
                 (unsigned)record->channel,
                 record->authmode);
    }

    s_wifi_sta.state = ESP32_WIFI_STA_STATE_SCAN_MODE;
    ESP_LOGI(TAG, "Wi-Fi scan mode found %u AP records", (unsigned)s_wifi_sta.scan_record_count);
    xSemaphoreGive(s_wifi_sta.mutex);
}

static bool esp32_wifi_sta_config_has_ssid(const esp32_wifi_sta_config_t *config)
{
    return config != NULL && config->ssid != NULL && config->ssid[0] != '\0';
}

static void esp32_wifi_sta_on_wifi_event(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_base;
    (void)event_data;

    if (event_id == WIFI_EVENT_STA_START && s_wifi_sta.connect_on_start) {
        esp32_wifi_sta_set_state(ESP32_WIFI_STA_STATE_CONNECTING);
        ESP_ERROR_CHECK(esp_wifi_connect());
        return;
    }

    if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *event = (wifi_event_sta_disconnected_t *)event_data;
        esp32_wifi_sta_clear_ip();
        s_wifi_sta.last_disconnect_reason = event != NULL ? event->reason : WIFI_REASON_UNSPECIFIED;

        if (!s_wifi_sta.connect_on_start) {
            return;
        }

        if (s_wifi_sta.retry_count < s_wifi_sta.config.max_retry) {
            s_wifi_sta.retry_count++;
            esp32_wifi_sta_set_state(ESP32_WIFI_STA_STATE_CONNECTING);
            ESP_LOGW(TAG,
                     "Wi-Fi disconnected; retrying %u/%u",
                     (unsigned)s_wifi_sta.retry_count,
                     (unsigned)s_wifi_sta.config.max_retry);
            ESP_ERROR_CHECK(esp_wifi_connect());
            return;
        }

        esp32_wifi_sta_set_state(ESP32_WIFI_STA_STATE_FAILED);
        s_wifi_sta.last_failure_reason = esp32_wifi_sta_map_disconnect_reason(s_wifi_sta.last_disconnect_reason);
        xEventGroupSetBits(s_wifi_sta.event_group, ESP32_WIFI_STA_FAIL_BIT);
        ESP_LOGE(TAG,
                 "Wi-Fi connect failed: reason=%d mapped=%d",
                 (int)s_wifi_sta.last_disconnect_reason,
                 (int)s_wifi_sta.last_failure_reason);
    }
}

static void esp32_wifi_sta_on_ip_event(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_base;

    if (event_id != IP_EVENT_STA_GOT_IP) {
        return;
    }

    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;

    if (xSemaphoreTake(s_wifi_sta.mutex, portMAX_DELAY) == pdTRUE) {
        s_wifi_sta.retry_count = 0;
        s_wifi_sta.has_ip = true;
        s_wifi_sta.ip_info = event->ip_info;
        s_wifi_sta.state = ESP32_WIFI_STA_STATE_CONNECTED;
        s_wifi_sta.last_failure_reason = ESP32_WIFI_STA_FAILURE_NONE;
        xSemaphoreGive(s_wifi_sta.mutex);
    }

    ESP_LOGI(TAG, "Wi-Fi connected: " IPSTR, IP2STR(&event->ip_info.ip));
    xEventGroupSetBits(s_wifi_sta.event_group, ESP32_WIFI_STA_CONNECTED_BIT);
}

static esp_err_t esp32_wifi_sta_copy_config(const esp32_wifi_sta_config_t *config, wifi_config_t *wifi_config)
{
    size_t ssid_len;
    size_t password_len;
    size_t sae_h2e_identifier_len;

    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG, "config is null");
    ESP_RETURN_ON_FALSE(wifi_config != NULL, ESP_ERR_INVALID_ARG, TAG, "wifi_config is null");
    ESP_RETURN_ON_FALSE(config->ssid != NULL && config->ssid[0] != '\0',
                        ESP_ERR_INVALID_ARG,
                        TAG,
                        "Wi-Fi SSID is empty");

    ssid_len = strlen(config->ssid);
    password_len = config->password != NULL ? strlen(config->password) : 0;
    sae_h2e_identifier_len = config->sae_h2e_identifier != NULL ? strlen(config->sae_h2e_identifier) : 0;
    ESP_RETURN_ON_FALSE(ssid_len <= sizeof(wifi_config->sta.ssid),
                        ESP_ERR_INVALID_ARG,
                        TAG,
                        "Wi-Fi SSID is too long");
    ESP_RETURN_ON_FALSE(password_len <= sizeof(wifi_config->sta.password),
                        ESP_ERR_INVALID_ARG,
                        TAG,
                        "Wi-Fi password is too long");
    ESP_RETURN_ON_FALSE(sae_h2e_identifier_len <= sizeof(wifi_config->sta.sae_h2e_identifier),
                        ESP_ERR_INVALID_ARG,
                        TAG,
                        "Wi-Fi SAE H2E identifier is too long");

    memset(wifi_config, 0, sizeof(*wifi_config));
    memcpy(wifi_config->sta.ssid, config->ssid, ssid_len);
    if (password_len > 0) {
        memcpy(wifi_config->sta.password, config->password, password_len);
    }
    wifi_config->sta.threshold.authmode = config->authmode_threshold;
    wifi_config->sta.sae_pwe_h2e = config->sae_pwe_h2e == WPA3_SAE_PWE_UNSPECIFIED ?
                                    WPA3_SAE_PWE_BOTH :
                                    config->sae_pwe_h2e;
    if (sae_h2e_identifier_len > 0) {
        memcpy(wifi_config->sta.sae_h2e_identifier, config->sae_h2e_identifier, sae_h2e_identifier_len);
    }

    return ESP_OK;
}

static esp_err_t esp32_wifi_sta_register_handlers(void)
{
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(WIFI_EVENT,
                                                            ESP_EVENT_ANY_ID,
                                                            &esp32_wifi_sta_on_wifi_event,
                                                            NULL,
                                                            NULL),
                        TAG,
                        "register Wi-Fi event handler failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(IP_EVENT,
                                                            IP_EVENT_STA_GOT_IP,
                                                            &esp32_wifi_sta_on_ip_event,
                                                            NULL,
                                                            NULL),
                        TAG,
                        "register IP event handler failed");
    return ESP_OK;
}

esp_err_t esp32_wifi_sta_init(void)
{
    esp32_wifi_sta_config_t config = esp32_wifi_sta_default_config();
    return esp32_wifi_sta_init_with_config(&config);
}

esp_err_t esp32_wifi_sta_init_with_config(const esp32_wifi_sta_config_t *config)
{
    wifi_config_t wifi_config;

    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG, "config is null");

    ESP_RETURN_ON_ERROR(esp32_wifi_sta_ensure_driver(), TAG, "Wi-Fi driver init failed");

    if (s_wifi_sta.started) {
        esp_err_t disconnect_err = esp_wifi_disconnect();
        if (disconnect_err != ESP_OK && disconnect_err != ESP_ERR_WIFI_NOT_CONNECT) {
            ESP_LOGW(TAG, "Wi-Fi disconnect before reconfigure returned %s", esp_err_to_name(disconnect_err));
        }
    }

    s_wifi_sta.config = *config;
    s_wifi_sta.configured = false;
    s_wifi_sta.last_failure_reason = ESP32_WIFI_STA_FAILURE_NONE;
    s_wifi_sta.last_disconnect_reason = WIFI_REASON_UNSPECIFIED;

    if (!esp32_wifi_sta_config_has_ssid(&s_wifi_sta.config)) {
        ESP_LOGW(TAG, "Wi-Fi SSID is empty");
        return ESP_ERR_NOT_FOUND;
    }

    ESP_RETURN_ON_ERROR(esp32_wifi_sta_copy_config(config, &wifi_config), TAG, "Wi-Fi config invalid");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_config), TAG, "set Wi-Fi config failed");
    s_wifi_sta.configured = true;

    ESP_LOGI(TAG, "Wi-Fi STA initialized for SSID \"%s\"", config->ssid);
    return ESP_OK;
}

esp_err_t esp32_wifi_sta_start(void)
{
    ESP_RETURN_ON_FALSE(s_wifi_sta.initialized, ESP_ERR_INVALID_STATE, TAG, "Wi-Fi STA not initialized");
    ESP_RETURN_ON_FALSE(s_wifi_sta.configured, ESP_ERR_INVALID_STATE, TAG, "Wi-Fi STA has no configured SSID");

    if (s_wifi_sta.started && s_wifi_sta.state == ESP32_WIFI_STA_STATE_CONNECTED) {
        return ESP_OK;
    }

    xEventGroupClearBits(s_wifi_sta.event_group, ESP32_WIFI_STA_CONNECTED_BIT | ESP32_WIFI_STA_FAIL_BIT);
    s_wifi_sta.retry_count = 0;
    s_wifi_sta.connect_on_start = true;
    esp32_wifi_sta_set_state(ESP32_WIFI_STA_STATE_CONNECTING);

    if (s_wifi_sta.started) {
        return esp_wifi_connect();
    }

    esp_err_t err = esp_wifi_start();
    if (err == ESP_OK) {
        s_wifi_sta.started = true;
    } else {
        s_wifi_sta.connect_on_start = false;
        esp32_wifi_sta_set_state(ESP32_WIFI_STA_STATE_STOPPED);
    }
    return err;
}

esp_err_t esp32_wifi_sta_enter_scan_mode(void)
{
    uint16_t number = CONFIG_ESP32_WIFI_STA_SCAN_LIST_SIZE;
    uint16_t ap_count = 0;
    wifi_ap_record_t ap_records[CONFIG_ESP32_WIFI_STA_SCAN_LIST_SIZE];

    ESP_RETURN_ON_ERROR(esp32_wifi_sta_ensure_driver(), TAG, "Wi-Fi driver init failed");

    memset(ap_records, 0, sizeof(ap_records));
    s_wifi_sta.connect_on_start = false;
    esp32_wifi_sta_clear_ip();
    esp32_wifi_sta_set_state(ESP32_WIFI_STA_STATE_SCAN_MODE);

    if (!s_wifi_sta.started) {
        ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "Wi-Fi start for scan mode failed");
        s_wifi_sta.started = true;
    } else {
        esp_err_t disconnect_err = esp_wifi_disconnect();
        if (disconnect_err != ESP_OK && disconnect_err != ESP_ERR_WIFI_NOT_CONNECT) {
            ESP_LOGW(TAG, "Wi-Fi disconnect before scan mode returned %s", esp_err_to_name(disconnect_err));
        }
    }

    esp_err_t err = esp_wifi_scan_start(NULL, true);
    if (err == ESP_OK) {
        err = esp_wifi_scan_get_ap_num(&ap_count);
    }
    if (err == ESP_OK) {
        err = esp_wifi_scan_get_ap_records(&number, ap_records);
    }
    ESP_RETURN_ON_ERROR(err, TAG, "Wi-Fi scan failed");

    ESP_LOGI(TAG, "scanned %u APs, keeping %u records in scan mode", (unsigned)ap_count, (unsigned)number);
    esp32_wifi_sta_store_scan_records(ap_records, number);
    return ESP_OK;
}

esp_err_t esp32_wifi_sta_get_scan_records(esp32_wifi_sta_scan_record_t *records,
                                          size_t record_capacity,
                                          size_t *record_count)
{
    ESP_RETURN_ON_FALSE(record_count != NULL, ESP_ERR_INVALID_ARG, TAG, "record_count is null");
    ESP_RETURN_ON_FALSE(records != NULL || record_capacity == 0, ESP_ERR_INVALID_ARG, TAG, "records is null");
    *record_count = 0;
    if (!s_wifi_sta.initialized || s_wifi_sta.mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_wifi_sta.mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    size_t count = s_wifi_sta.scan_record_count < record_capacity ? s_wifi_sta.scan_record_count : record_capacity;
    if (count > 0) {
        memcpy(records, s_wifi_sta.scan_records, count * sizeof(records[0]));
    }
    *record_count = s_wifi_sta.scan_record_count;
    xSemaphoreGive(s_wifi_sta.mutex);
    return ESP_OK;
}

esp_err_t esp32_wifi_sta_get_configured_ssid(char *ssid, size_t ssid_size)
{
    ESP_RETURN_ON_FALSE(ssid != NULL && ssid_size > 0, ESP_ERR_INVALID_ARG, TAG, "ssid buffer invalid");
    ESP_RETURN_ON_FALSE(s_wifi_sta.initialized, ESP_ERR_INVALID_STATE, TAG, "Wi-Fi STA not initialized");

    if (xSemaphoreTake(s_wifi_sta.mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    const char *configured_ssid = s_wifi_sta.config.ssid != NULL ? s_wifi_sta.config.ssid : "";
    strlcpy(ssid, configured_ssid, ssid_size);
    xSemaphoreGive(s_wifi_sta.mutex);
    return ESP_OK;
}

bool esp32_wifi_sta_has_configured_ssid(void)
{
    if (CONFIG_ESP32_WIFI_STA_SSID[0] != '\0') {
        return true;
    }
    return wifi_profile_store_has_profiles();
}

esp_err_t esp32_wifi_sta_save_credentials(const char *ssid, const char *password)
{
    return wifi_profile_store_record_success(ssid, password, WIFI_AUTH_OPEN);
}

esp_err_t esp32_wifi_sta_stop(void)
{
    ESP_RETURN_ON_FALSE(s_wifi_sta.initialized, ESP_ERR_INVALID_STATE, TAG, "Wi-Fi STA not initialized");

    if (!s_wifi_sta.started) {
        return ESP_OK;
    }

    s_wifi_sta.connect_on_start = false;
    esp_err_t disconnect_err = esp_wifi_disconnect();
    if (disconnect_err != ESP_OK && disconnect_err != ESP_ERR_WIFI_NOT_CONNECT) {
        ESP_LOGW(TAG, "Wi-Fi disconnect before stop returned %s", esp_err_to_name(disconnect_err));
    }
    ESP_RETURN_ON_ERROR(esp_wifi_stop(), TAG, "Wi-Fi stop failed");
    s_wifi_sta.started = false;
    s_wifi_sta.retry_count = 0;
    esp32_wifi_sta_clear_ip();
    esp32_wifi_sta_set_state(ESP32_WIFI_STA_STATE_STOPPED);
    xEventGroupClearBits(s_wifi_sta.event_group, ESP32_WIFI_STA_CONNECTED_BIT | ESP32_WIFI_STA_FAIL_BIT);
    return ESP_OK;
}

esp_err_t esp32_wifi_sta_wait_connected(TickType_t wait_ticks)
{
    EventBits_t bits;

    ESP_RETURN_ON_FALSE(s_wifi_sta.initialized, ESP_ERR_INVALID_STATE, TAG, "Wi-Fi STA not initialized");

    bits = xEventGroupWaitBits(s_wifi_sta.event_group,
                               ESP32_WIFI_STA_CONNECTED_BIT | ESP32_WIFI_STA_FAIL_BIT,
                               pdFALSE,
                               pdFALSE,
                               wait_ticks);

    if ((bits & ESP32_WIFI_STA_CONNECTED_BIT) != 0) {
        return ESP_OK;
    }
    if ((bits & ESP32_WIFI_STA_FAIL_BIT) != 0) {
        return ESP_FAIL;
    }
    return ESP_ERR_TIMEOUT;
}

esp_err_t esp32_wifi_sta_get_status(esp32_wifi_sta_status_t *status)
{
    ESP_RETURN_ON_FALSE(status != NULL, ESP_ERR_INVALID_ARG, TAG, "status is null");
    ESP_RETURN_ON_FALSE(s_wifi_sta.initialized, ESP_ERR_INVALID_STATE, TAG, "Wi-Fi STA not initialized");

    if (xSemaphoreTake(s_wifi_sta.mutex, portMAX_DELAY) == pdTRUE) {
        status->state = s_wifi_sta.state;
        status->retry_count = s_wifi_sta.retry_count;
        status->has_ip = s_wifi_sta.has_ip;
        status->ip_info = s_wifi_sta.ip_info;
        xSemaphoreGive(s_wifi_sta.mutex);
        return ESP_OK;
    }

    return ESP_ERR_TIMEOUT;
}

bool esp32_wifi_sta_is_connected(void)
{
    esp32_wifi_sta_status_t status;

    if (esp32_wifi_sta_get_status(&status) != ESP_OK) {
        return false;
    }

    return status.state == ESP32_WIFI_STA_STATE_CONNECTED && status.has_ip;
}

esp32_wifi_sta_failure_reason_t esp32_wifi_sta_get_last_failure_reason(void)
{
    return s_wifi_sta.last_failure_reason;
}
