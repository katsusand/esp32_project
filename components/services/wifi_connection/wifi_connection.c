#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "esp32_wifi_sta.h"
#include "wifi_connection.h"
#include "wifi_profile_store.h"

#ifndef CONFIG_ESP32_WIFI_STA_MAX_RETRY
#define CONFIG_ESP32_WIFI_STA_MAX_RETRY 5
#endif
#ifndef CONFIG_ESP32_WIFI_STA_RETRY_DELAY_MS
#define CONFIG_ESP32_WIFI_STA_RETRY_DELAY_MS 1000
#endif
#ifndef CONFIG_ESP32_WIFI_STA_SCAN_LIST_SIZE
#define CONFIG_ESP32_WIFI_STA_SCAN_LIST_SIZE 30
#endif
#ifndef CONFIG_ESP32_WIFI_STA_SAE_H2E_IDENTIFIER
#define CONFIG_ESP32_WIFI_STA_SAE_H2E_IDENTIFIER ""
#endif

#define TAG "wifi_connection"

typedef struct {
    wifi_profile_store_entry_t profile;
    esp32_wifi_sta_scan_record_t scan_record;
} wifi_connection_candidate_t;

static portMUX_TYPE s_progress_lock = portMUX_INITIALIZER_UNLOCKED;
static wifi_connection_progress_t s_progress = {
    .phase = WIFI_CONNECTION_PROGRESS_IDLE,
};

static void wifi_connection_set_progress(wifi_connection_progress_phase_t phase,
                                         const char *ssid)
{
    portENTER_CRITICAL(&s_progress_lock);
    s_progress.phase = phase;
    if (ssid != NULL) {
        snprintf(s_progress.ssid, sizeof(s_progress.ssid), "%.32s", ssid);
    } else {
        s_progress.ssid[0] = '\0';
    }
    portEXIT_CRITICAL(&s_progress_lock);
}

static void wifi_connection_stop_sta_if_initialized(void)
{
    esp32_wifi_sta_status_t status = { 0 };

    if (esp32_wifi_sta_get_status(&status) == ESP_OK) {
        esp_err_t err = esp32_wifi_sta_stop();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Wi-Fi STA stop before retry failed: %s", esp_err_to_name(err));
        }
    }
}

static esp_err_t wifi_connection_connect_fresh(const esp32_wifi_sta_config_t *config,
                                                TickType_t wait_ticks,
                                                esp32_wifi_sta_failure_reason_t *failure_reason)
{
    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG, "Wi-Fi config is null");

    esp32_wifi_sta_config_t single_attempt_config = *config;
    const uint32_t attempt_count = wait_ticks > 0 ? (uint32_t)config->max_retry + 1U : 1U;
    esp_err_t last_err = ESP_FAIL;

    single_attempt_config.max_retry = 0;
    if (failure_reason != NULL) {
        *failure_reason = ESP32_WIFI_STA_FAILURE_NONE;
    }

    for (uint32_t attempt = 0; attempt < attempt_count; ++attempt) {
        wifi_connection_stop_sta_if_initialized();
        if (attempt > 0 && CONFIG_ESP32_WIFI_STA_RETRY_DELAY_MS > 0) {
            vTaskDelay(pdMS_TO_TICKS(CONFIG_ESP32_WIFI_STA_RETRY_DELAY_MS));
        }

        last_err = esp32_wifi_sta_init_with_config(&single_attempt_config);
        if (last_err == ESP_OK) {
            last_err = esp32_wifi_sta_start();
        }
        if (last_err == ESP_OK && wait_ticks > 0) {
            last_err = esp32_wifi_sta_wait_connected(wait_ticks);
        }
        if (last_err == ESP_OK) {
            return ESP_OK;
        }

        esp32_wifi_sta_failure_reason_t reason = esp32_wifi_sta_get_last_failure_reason();
        if (reason == ESP32_WIFI_STA_FAILURE_NONE) {
            reason = last_err == ESP_ERR_TIMEOUT ?
                     ESP32_WIFI_STA_FAILURE_TIMEOUT :
                     ESP32_WIFI_STA_FAILURE_CONNECT;
        }
        if (failure_reason != NULL) {
            *failure_reason = reason;
        }
        ESP_LOGW(TAG,
                 "fresh connection attempt failed: attempt=%u/%u err=%s reason=%d",
                 (unsigned)(attempt + 1U),
                 (unsigned)attempt_count,
                 esp_err_to_name(last_err),
                 (int)reason);
    }

    wifi_connection_stop_sta_if_initialized();
    return last_err;
}

esp_err_t wifi_connection_connect_and_save(const char *ssid,
                                           const char *password,
                                           wifi_auth_mode_t authmode,
                                           TickType_t wait_ticks,
                                           esp32_wifi_sta_failure_reason_t *failure_reason)
{
    ESP_RETURN_ON_FALSE(ssid != NULL && ssid[0] != '\0',
                        ESP_ERR_INVALID_ARG,
                        TAG,
                        "Wi-Fi SSID is empty");

    esp32_wifi_sta_config_t config = {
        .ssid = ssid,
        .password = password,
        .max_retry = CONFIG_ESP32_WIFI_STA_MAX_RETRY,
        .authmode_threshold = WIFI_AUTH_OPEN,
        .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
        .sae_h2e_identifier = CONFIG_ESP32_WIFI_STA_SAE_H2E_IDENTIFIER,
    };

    wifi_connection_set_progress(WIFI_CONNECTION_PROGRESS_CONNECTING, ssid);
    esp_err_t err = wifi_connection_connect_fresh(&config, wait_ticks, failure_reason);
    if (err == ESP_OK) {
        err = wifi_profile_store_record_success(ssid, password, authmode);
    }
    wifi_connection_set_progress(WIFI_CONNECTION_PROGRESS_IDLE, NULL);
    return err;
}

static int wifi_connection_compare_candidates(const wifi_connection_candidate_t *lhs,
                                               const wifi_connection_candidate_t *rhs)
{
    if (lhs->profile.last_success_seq != rhs->profile.last_success_seq) {
        return lhs->profile.last_success_seq > rhs->profile.last_success_seq ? -1 : 1;
    }
    if (lhs->scan_record.rssi != rhs->scan_record.rssi) {
        return lhs->scan_record.rssi > rhs->scan_record.rssi ? -1 : 1;
    }
    return strncmp(lhs->profile.ssid, rhs->profile.ssid, sizeof(lhs->profile.ssid));
}

static void wifi_connection_sort_candidates(wifi_connection_candidate_t *candidates,
                                            size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        for (size_t j = i + 1; j < count; ++j) {
            if (wifi_connection_compare_candidates(&candidates[i], &candidates[j]) > 0) {
                wifi_connection_candidate_t tmp = candidates[i];
                candidates[i] = candidates[j];
                candidates[j] = tmp;
            }
        }
    }
}

static size_t wifi_connection_collect_candidates(wifi_connection_candidate_t *candidates,
                                                 size_t capacity,
                                                 esp32_wifi_sta_failure_reason_t *failure_reason)
{
    wifi_profile_store_entry_t profiles[WIFI_PROFILE_STORE_MAX_ENTRIES] = { 0 };
    esp32_wifi_sta_scan_record_t records[CONFIG_ESP32_WIFI_STA_SCAN_LIST_SIZE] = { 0 };
    size_t profile_count = 0;
    size_t record_count = 0;
    size_t candidate_count = 0;

    if (wifi_profile_store_load_entries(profiles,
                                        WIFI_PROFILE_STORE_MAX_ENTRIES,
                                        &profile_count) != ESP_OK ||
        profile_count == 0) {
        *failure_reason = ESP32_WIFI_STA_FAILURE_NO_SAVED_PROFILE;
        return 0;
    }

    wifi_connection_set_progress(WIFI_CONNECTION_PROGRESS_SEARCHING, NULL);
    if (esp32_wifi_sta_enter_scan_mode() != ESP_OK ||
        esp32_wifi_sta_get_scan_records(records,
                                        CONFIG_ESP32_WIFI_STA_SCAN_LIST_SIZE,
                                        &record_count) != ESP_OK) {
        *failure_reason = ESP32_WIFI_STA_FAILURE_TIMEOUT;
        return 0;
    }

    for (size_t i = 0; i < profile_count && candidate_count < capacity; ++i) {
        for (size_t j = 0; j < record_count; ++j) {
            if (strncmp(profiles[i].ssid, records[j].ssid, sizeof(profiles[i].ssid)) == 0) {
                candidates[candidate_count].profile = profiles[i];
                candidates[candidate_count].scan_record = records[j];
                ++candidate_count;
                break;
            }
        }
    }

    wifi_connection_sort_candidates(candidates, candidate_count);
    if (candidate_count == 0) {
        *failure_reason = ESP32_WIFI_STA_FAILURE_NO_AP_IN_RANGE;
    }
    return candidate_count;
}

static esp_err_t wifi_connection_connect_candidate(const wifi_connection_candidate_t *candidate,
                                                   TickType_t wait_ticks,
                                                   esp32_wifi_sta_failure_reason_t *failure_reason)
{
    esp32_wifi_sta_config_t config = {
        .ssid = candidate->profile.ssid,
        .password = candidate->profile.password,
        .max_retry = CONFIG_ESP32_WIFI_STA_MAX_RETRY,
        .authmode_threshold = WIFI_AUTH_OPEN,
        .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
        .sae_h2e_identifier = CONFIG_ESP32_WIFI_STA_SAE_H2E_IDENTIFIER,
    };

    wifi_connection_set_progress(WIFI_CONNECTION_PROGRESS_CONNECTING,
                                 candidate->profile.ssid);
    esp_err_t err = wifi_connection_connect_fresh(&config, wait_ticks, failure_reason);
    if (err == ESP_OK) {
        err = wifi_profile_store_record_success(candidate->profile.ssid,
                                                candidate->profile.password,
                                                candidate->scan_record.authmode);
    }
    return err;
}

esp_err_t wifi_connection_connect_configured(TickType_t wait_ticks,
                                              esp32_wifi_sta_failure_reason_t *failure_reason)
{
    wifi_connection_candidate_t candidates[WIFI_PROFILE_STORE_MAX_ENTRIES] = { 0 };
    esp32_wifi_sta_failure_reason_t reason = ESP32_WIFI_STA_FAILURE_NONE;
    size_t count = wifi_connection_collect_candidates(candidates,
                                                      WIFI_PROFILE_STORE_MAX_ENTRIES,
                                                      &reason);

    if (failure_reason != NULL) {
        *failure_reason = reason;
    }
    if (count == 0) {
        wifi_connection_set_progress(WIFI_CONNECTION_PROGRESS_IDLE, NULL);
        return reason == ESP32_WIFI_STA_FAILURE_NO_SAVED_PROFILE ? ESP_ERR_NOT_FOUND : ESP_FAIL;
    }

    for (size_t i = 0; i < count; ++i) {
        esp_err_t err = wifi_connection_connect_candidate(&candidates[i], wait_ticks, &reason);
        if (failure_reason != NULL) {
            *failure_reason = reason;
        }
        if (err == ESP_OK) {
            wifi_connection_set_progress(WIFI_CONNECTION_PROGRESS_IDLE, NULL);
            return ESP_OK;
        }
    }

    wifi_connection_set_progress(WIFI_CONNECTION_PROGRESS_IDLE, NULL);
    return ESP_FAIL;
}

wifi_connection_progress_t wifi_connection_get_progress(void)
{
    wifi_connection_progress_t progress;

    portENTER_CRITICAL(&s_progress_lock);
    progress = s_progress;
    portEXIT_CRITICAL(&s_progress_lock);
    return progress;
}
