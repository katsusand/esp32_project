#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "app_stack_monitor.h"
#include "radio_manager.h"
#include "sdkconfig.h"
#include "wifi_rssi_history.h"

#define TAG "wifi_rssi_history"
#define WIFI_RSSI_HISTORY_CAPACITY CONFIG_WIFI_RSSI_HISTORY_MAX_SAMPLES

static int16_t s_samples[WIFI_RSSI_HISTORY_CAPACITY];
static uint16_t s_count;
static uint16_t s_revision;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static TaskHandle_t s_task_handle;
static volatile bool s_monitoring_requested;
static bool s_lease_held;
static radio_manager_lease_t s_lease;

/*
 * Samples are kept linear with the newest at the end so the array can be handed
 * to the sparkline widget as-is. Shifting costs a few hundred bytes of memmove
 * per sample, which is cheaper than teaching every reader about ring ordering.
 */
static void wifi_rssi_history_append(int16_t value)
{
    portENTER_CRITICAL(&s_lock);
    if (s_count < WIFI_RSSI_HISTORY_CAPACITY) {
        s_samples[s_count++] = value;
    } else {
        memmove(&s_samples[0], &s_samples[1], (WIFI_RSSI_HISTORY_CAPACITY - 1) * sizeof(s_samples[0]));
        s_samples[WIFI_RSSI_HISTORY_CAPACITY - 1] = value;
    }
    s_revision++;
    portEXIT_CRITICAL(&s_lock);
}

static void wifi_rssi_history_sample_once(void)
{
    wifi_ap_record_t ap_info = { 0 };

    /*
     * Not being associated is normal: the radio may still be coming up, or the
     * AP may have dropped. Record an explicit dropout rather than skipping, so
     * the graph's time axis stays honest instead of silently compressing the
     * outage away.
     */
    if (esp_wifi_sta_get_ap_info(&ap_info) != ESP_OK) {
        wifi_rssi_history_append(WIFI_RSSI_HISTORY_GAP_DBM);
        return;
    }

    wifi_rssi_history_append((int16_t)ap_info.rssi);
}

static void wifi_rssi_history_apply_lease(void)
{
    bool wanted = s_monitoring_requested;

    if (wanted == s_lease_held) {
        return;
    }

    if (wanted) {
        const radio_manager_request_t request = {
            .client = RADIO_MANAGER_CLIENT_RSSI_MONITOR,
            .required = RADIO_MANAGER_CAP_INTERNET,
            .max_hold_ticks = pdMS_TO_TICKS(CONFIG_WIFI_RSSI_HISTORY_RADIO_MAX_HOLD_MS),
        };
        radio_manager_lease_t lease = { 0 };

        /*
         * This blocks until the radio is up, which is exactly why it runs here
         * and not in the caller. radio_manager_acquire() parks the calling task,
         * so calling it from the app_shell step path froze the UI for the whole
         * Wi-Fi bring-up.
         */
        esp_err_t err = radio_manager_acquire(&request,
                                              &lease,
                                              pdMS_TO_TICKS(CONFIG_WIFI_RSSI_HISTORY_RADIO_WAIT_TIMEOUT_MS));
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "radio acquire failed: %s", esp_err_to_name(err));
            return;
        }

        s_lease = lease;
        s_lease_held = true;
        ESP_LOGI(TAG, "monitoring on: radio lease held");
        return;
    }

    esp_err_t err = radio_manager_release(&s_lease);
    s_lease = (radio_manager_lease_t){ 0 };
    s_lease_held = false;
    if (err != ESP_OK) {
        /* max_hold_ticks may have revoked the lease already. Nothing to act on;
           monitoring is off either way. */
        ESP_LOGW(TAG, "radio release failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "monitoring off: radio lease released");
    }
}

static void wifi_rssi_history_task(void *arg)
{
    (void)arg;

    while (true) {
        APP_STACK_MONITOR_CHECK(TAG, "wifi_rssi_history", 30000);

        wifi_rssi_history_apply_lease();

        if (s_lease_held) {
            wifi_rssi_history_sample_once();
        }

        /*
         * A monitoring request wakes the task immediately so the lease attempt
         * starts without waiting out the sampling interval.
         */
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(CONFIG_WIFI_RSSI_HISTORY_SAMPLE_INTERVAL_MS));
    }
}

esp_err_t wifi_rssi_history_start(void)
{
    if (s_task_handle != NULL) {
        return ESP_OK;
    }

    BaseType_t task_ok = xTaskCreate(wifi_rssi_history_task,
                                     "wifi_rssi",
                                     CONFIG_WIFI_RSSI_HISTORY_TASK_STACK_SIZE,
                                     NULL,
                                     CONFIG_WIFI_RSSI_HISTORY_TASK_PRIORITY,
                                     &s_task_handle);
    ESP_RETURN_ON_FALSE(task_ok == pdPASS, ESP_ERR_NO_MEM, TAG, "task create failed");

    ESP_LOGI(TAG,
             "started: interval=%dms capacity=%d samples",
             CONFIG_WIFI_RSSI_HISTORY_SAMPLE_INTERVAL_MS,
             WIFI_RSSI_HISTORY_CAPACITY);
    return ESP_OK;
}

esp_err_t wifi_rssi_history_set_monitoring(bool monitoring)
{
    if (s_task_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (monitoring == s_monitoring_requested) {
        return ESP_OK;
    }

    s_monitoring_requested = monitoring;
    xTaskNotifyGive(s_task_handle);
    return ESP_OK;
}

bool wifi_rssi_history_is_monitoring(void)
{
    return s_monitoring_requested;
}

bool wifi_rssi_history_get(const int16_t **samples, uint16_t *count, uint16_t *revision)
{
    portENTER_CRITICAL(&s_lock);
    uint16_t local_count = s_count;
    uint16_t local_revision = s_revision;
    portEXIT_CRITICAL(&s_lock);

    if (local_count == 0) {
        return false;
    }

    if (samples != NULL) {
        *samples = s_samples;
    }
    if (count != NULL) {
        *count = local_count;
    }
    if (revision != NULL) {
        *revision = local_revision;
    }
    return true;
}

bool wifi_rssi_history_get_latest(int16_t *rssi_dbm)
{
    bool found = false;
    int16_t latest = 0;

    /* Skips trailing dropouts so the caller gets the last real reading rather
       than the gap marker. */
    portENTER_CRITICAL(&s_lock);
    for (uint16_t i = s_count; i > 0; --i) {
        if (s_samples[i - 1] != WIFI_RSSI_HISTORY_GAP_DBM) {
            latest = s_samples[i - 1];
            found = true;
            break;
        }
    }
    portEXIT_CRITICAL(&s_lock);

    if (found && rssi_dbm != NULL) {
        *rssi_dbm = latest;
    }
    return found;
}
