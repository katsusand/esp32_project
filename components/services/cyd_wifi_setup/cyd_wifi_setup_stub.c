#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "app_shell.h"
#include "cyd_display.h"
#include "cyd_input.h"
#include "cyd_wifi_setup.h"

#define TAG "cyd_wifi_setup"

static const app_shell_app_t *s_wifi_setup_return_app;

static esp_err_t cyd_wifi_setup_disabled_enter(void *ctx, const app_shell_app_t *from_app)
{
    (void)ctx;
    s_wifi_setup_return_app = from_app;
    const char *lines[] = {
        "Wi-Fi is disabled",
        "Tap to go back",
    };
    return cyd_display_show_lines("Wi-Fi Setup", lines, 2);
}

static esp_err_t cyd_wifi_setup_disabled_step(void *ctx)
{
    (void)ctx;
    cyd_input_event_t event = { 0 };
    if (cyd_input_read_event(&event, pdMS_TO_TICKS(50)) == ESP_OK &&
        event.type == CYD_INPUT_EVENT_TOUCH &&
        event.data.touch.action == CYD_INPUT_TOUCH_ACTION_RELEASE &&
        s_wifi_setup_return_app != NULL) {
        ESP_RETURN_ON_ERROR(app_shell_switch_to(s_wifi_setup_return_app), TAG, "switch back failed");
    }
    return ESP_OK;
}

static const app_shell_app_t s_cyd_wifi_setup_disabled_app = {
    .id = "wifi_setup_disabled",
    .ctx = NULL,
    .enter = cyd_wifi_setup_disabled_enter,
    .step = cyd_wifi_setup_disabled_step,
};

const app_shell_app_t *cyd_wifi_setup_get_app(void)
{
    return &s_cyd_wifi_setup_disabled_app;
}

void cyd_wifi_setup_set_return_app(const app_shell_app_t *app)
{
    s_wifi_setup_return_app = app;
}

void cyd_wifi_setup_begin_scan_session(void)
{
}

esp_err_t cyd_wifi_setup_poll_scan_session(const cyd_input_event_t *event,
                                           bool *selected,
                                           bool *cancelled,
                                           esp32_wifi_sta_scan_record_t *selected_record)
{
    (void)event;
    (void)selected_record;
    if (selected != NULL) {
        *selected = false;
    }
    if (cancelled != NULL) {
        *cancelled = true;
    }
    return ESP_ERR_NOT_SUPPORTED;
}

void cyd_wifi_setup_begin_password_session(const esp32_wifi_sta_scan_record_t *record)
{
    (void)record;
}

esp_err_t cyd_wifi_setup_poll_password_session(const cyd_input_event_t *event,
                                               cyd_wifi_setup_password_result_t *result)
{
    (void)event;
    if (result != NULL) {
        *result = CYD_WIFI_SETUP_PASSWORD_CANCELLED;
    }
    return ESP_ERR_NOT_SUPPORTED;
}
