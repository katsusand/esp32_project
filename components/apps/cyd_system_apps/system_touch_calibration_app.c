#include "esp_check.h"
#include "freertos/FreeRTOS.h"

#include "app_shell.h"
#include "cyd_input.h"
#include "cyd_system_apps.h"

#define TAG "cyd_system_apps"

static const app_shell_app_t *s_touch_calibration_return_app;

/*
 * Set by system_touch_calibration_open_over_active(): the interrupted app and
 * the from_app it was entered with, so it can be put back as it was.
 */
static portMUX_TYPE s_touch_calibration_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_touch_calibration_over_pending;
static const app_shell_app_t *s_touch_calibration_over_app;
static const app_shell_app_t *s_touch_calibration_over_from;

static const app_shell_app_t s_cyd_touch_calibration_shell_app;

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

    portENTER_CRITICAL(&s_touch_calibration_lock);
    bool over = s_touch_calibration_over_pending && s_touch_calibration_over_app == from_app;
    const app_shell_app_t *over_from = s_touch_calibration_over_from;
    s_touch_calibration_over_pending = false;
    s_touch_calibration_over_app = NULL;
    s_touch_calibration_over_from = NULL;
    portEXIT_CRITICAL(&s_touch_calibration_lock);

    s_touch_calibration_return_app = from_app;
    esp_err_t err = cyd_input_run_touch_calibration();
    (void)cyd_input_discard_pending_events();
    if (over && from_app != NULL) {
        ESP_RETURN_ON_ERROR(app_shell_switch_to_from(from_app, over_from), TAG, "switch back failed");
    } else {
        ESP_RETURN_ON_ERROR(app_shell_return_to(s_touch_calibration_return_app), TAG, "switch back failed");
    }
    ESP_RETURN_ON_ERROR(err, TAG, "touch calibration failed");
    return ESP_OK;
}

esp_err_t system_touch_calibration_open_over_active(void)
{
    const app_shell_app_t *active = NULL;
    const app_shell_app_t *active_from = NULL;
    app_shell_get_active_app_and_from(&active, &active_from);
    if (active == &s_cyd_touch_calibration_shell_app) {
        return ESP_OK;
    }

    portENTER_CRITICAL(&s_touch_calibration_lock);
    s_touch_calibration_over_pending = true;
    s_touch_calibration_over_app = active;
    s_touch_calibration_over_from = active_from;
    portEXIT_CRITICAL(&s_touch_calibration_lock);
    return app_shell_switch_to_from(&s_cyd_touch_calibration_shell_app, active);
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
