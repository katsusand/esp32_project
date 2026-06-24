#include "esp_log.h"
#include "cyd_status_led.h"
#include "status_indicator.h"
#include "wifi_connection.h"

#define TAG "status_indicator"

static void status_indicator_apply_wifi_status(const wifi_connection_connectivity_status_t *status,
                                               void *ctx)
{
    (void)ctx;
    if (status == NULL) {
        return;
    }

    cyd_led_pattern_t pattern = {
        .color = CYD_LED_COLOR_RED,
        .effect = CYD_LED_EFFECT_ON,
        .play = CYD_LED_PLAY_CONTINUOUS,
        .duration_ms = 0,
    };

    if (status->credentials_configured) {
        if (status->last_connection_succeeded) {
            pattern.color = CYD_LED_COLOR_GREEN;
        } else {
            pattern.effect = CYD_LED_EFFECT_BLINK_SLOW;
        }
    }

    esp_err_t err = cyd_status_led_set_base_pattern(&pattern);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "status LED update failed: %s", esp_err_to_name(err));
    }
}

esp_err_t status_indicator_start(void)
{
    wifi_connection_set_connectivity_callback(status_indicator_apply_wifi_status, NULL);
    return ESP_OK;
}
