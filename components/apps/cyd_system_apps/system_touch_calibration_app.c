#include "esp_check.h"

#include "app_shell.h"
#include "cyd_input.h"
#include "cyd_system_apps.h"

#define TAG "cyd_system_apps"

static const app_shell_app_t *s_touch_calibration_return_app;

static esp_err_t cyd_touch_calibration_app_enter(void *ctx, const app_shell_app_t *from_app)
{
    (void)ctx;

    ESP_RETURN_ON_FALSE(from_app != NULL, ESP_ERR_INVALID_STATE, TAG, "touch calibration return app not set");
    s_touch_calibration_return_app = from_app;
    ESP_RETURN_ON_ERROR(cyd_input_run_touch_calibration(), TAG, "touch calibration failed");
    ESP_RETURN_ON_ERROR(cyd_input_discard_pending_events(), TAG, "discard input events failed");
    return app_shell_return_to(s_touch_calibration_return_app);
}

static const app_shell_app_t s_cyd_touch_calibration_shell_app = {
    .id = "touch_calibration",
    .ctx = NULL,
    .enter = cyd_touch_calibration_app_enter,
    .step = NULL,
    .leave = NULL,
};

const app_shell_app_t *system_touch_calibration_app_get_app(void)
{
    return &s_cyd_touch_calibration_shell_app;
}
