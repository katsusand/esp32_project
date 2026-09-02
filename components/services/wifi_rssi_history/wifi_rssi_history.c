#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "app_stack_monitor.h"
#include "sdkconfig.h"
#include "wifi_rssi_history.h"

#define TAG "wifi_rssi_history"
#define WIFI_RSSI_HISTORY_CAPACITY CONFIG_WIFI_RSSI_HISTORY_MAX_SAMPLES

static int16_t s_samples[WIFI_RSSI_HISTORY_CAPACITY];
static uint16_t s_count;
static uint16_t s_revision;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static TaskHandle_t s_task_handle;

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

    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        wifi_rssi_history_append((int16_t)ap_info.rssi);
        return;
    }

    /*
     * Not associated. Record a single marker for the break rather than one per
     * interval: radio_manager can keep Wi-Fi off for hours, and one gap per
     * second would scroll the real history out of the buffer it exists to show.
     * A leading gap before any real reading is just noise, so skip that too.
     */
    portENTER_CRITICAL(&s_lock);
    bool want_gap = s_count > 0 && s_samples[s_count - 1] != WIFI_RSSI_HISTORY_GAP_DBM;
    portEXIT_CRITICAL(&s_lock);

    if (want_gap) {
        wifi_rssi_history_append(WIFI_RSSI_HISTORY_GAP_DBM);
    }
}

static void wifi_rssi_history_task(void *arg)
{
    (void)arg;

    while (true) {
        APP_STACK_MONITOR_CHECK(TAG, "wifi_rssi_history", 30000);

        wifi_rssi_history_sample_once();
        vTaskDelay(pdMS_TO_TICKS(CONFIG_WIFI_RSSI_HISTORY_SAMPLE_INTERVAL_MS));
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
    /*
     * Deliberately does NOT skip over a trailing break marker. The newest sample
     * being a break is exactly how "the station is not associated right now" is
     * represented, and reporting the last real reading instead would show a
     * stale signal strength as if it were current.
     */
    portENTER_CRITICAL(&s_lock);
    bool has_current = s_count > 0 && s_samples[s_count - 1] != WIFI_RSSI_HISTORY_GAP_DBM;
    int16_t latest = has_current ? s_samples[s_count - 1] : 0;
    portEXIT_CRITICAL(&s_lock);

    if (has_current && rssi_dbm != NULL) {
        *rssi_dbm = latest;
    }
    return has_current;
}
