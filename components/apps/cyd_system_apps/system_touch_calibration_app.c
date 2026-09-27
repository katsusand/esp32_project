#include "esp_check.h"

#include "app_shell.h"
#include "cyd_input.h"
#include "cyd_system_apps.h"

#define TAG "cyd_system_apps"

static const app_shell_app_t *s_touch_calibration_return_app;

/*
 * Always leaves, success or not.
 *
 * English contract: this app has no step(), so it must never stay active. When
 * a failed calibration returned early here, the shell kept a step-less app on
 * a frozen calibration screen with nothing reading input; with IdleReturn set
 * to never, only a power cycle got the user out. app_shell_return_to() falls
 * back to the home app when there is no return app.
 */
static esp_err_t cyd_touch_calibration_app_enter(void *ctx, const app_shell_app_t *from_app)
{
    (void)ctx;

    s_touch_calibration_return_app = from_app;
    esp_err_t err = cyd_input_run_touch_calibration();
    (void)cyd_input_discard_pending_events();
    ESP_RETURN_ON_ERROR(app_shell_return_to(s_touch_calibration_return_app), TAG, "switch back failed");
    ESP_RETURN_ON_ERROR(err, TAG, "touch calibration failed");
    return ESP_OK;
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
