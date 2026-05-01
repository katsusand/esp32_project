#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/portmacro.h"
#include "esp_check.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "app_shell.h"
#include "cyd_display.h"
#include "cyd_input.h"

#define TAG "app_shell"
static TaskHandle_t s_app_shell_task_handle;
static portMUX_TYPE s_app_shell_lock = portMUX_INITIALIZER_UNLOCKED;
static const app_shell_app_t *s_app_shell_active_app;
static const app_shell_app_t *s_app_shell_pending_app;
static const app_shell_app_t *s_app_shell_home_app;

static bool app_shell_idle_return_enabled(void)
{
    return CONFIG_APP_SHELL_IDLE_RETURN_TIMEOUT_SECONDS > 0;
}

static bool app_shell_is_home_idle_target(void)
{
    return s_app_shell_home_app != NULL &&
           s_app_shell_active_app != NULL &&
           s_app_shell_active_app != s_app_shell_home_app;
}

bool app_shell_is_idle_timeout_elapsed(void)
{
    TickType_t last_activity_tick = 0;
    TickType_t now = 0;
    TickType_t timeout_ticks = 0;

    if (!app_shell_idle_return_enabled() || !app_shell_is_home_idle_target()) {
        return false;
    }

    last_activity_tick = cyd_input_get_last_activity_tick();
    now = xTaskGetTickCount();
    timeout_ticks = pdMS_TO_TICKS(CONFIG_APP_SHELL_IDLE_RETURN_TIMEOUT_SECONDS * 1000U);
    if (last_activity_tick == 0 || timeout_ticks == 0) {
        return false;
    }

    return (now - last_activity_tick) >= timeout_ticks;
}

static esp_err_t app_shell_apply_pending_switch(void)
{
    const app_shell_app_t *next_app = NULL;
    const app_shell_app_t *from_app = NULL;

    portENTER_CRITICAL(&s_app_shell_lock);
    if (s_app_shell_pending_app != NULL &&
        s_app_shell_pending_app != s_app_shell_active_app) {
        next_app = s_app_shell_pending_app;
        s_app_shell_pending_app = NULL;
    } else {
        s_app_shell_pending_app = NULL;
    }
    portEXIT_CRITICAL(&s_app_shell_lock);

    if (next_app == NULL) {
        return ESP_OK;
    }

    from_app = s_app_shell_active_app;
    if (s_app_shell_active_app != NULL && s_app_shell_active_app->leave != NULL) {
        ESP_RETURN_ON_ERROR(s_app_shell_active_app->leave(s_app_shell_active_app->ctx),
                            TAG,
                            "active app leave failed");
    }

    s_app_shell_active_app = next_app;
    if (s_app_shell_active_app->enter != NULL) {
        ESP_RETURN_ON_ERROR(s_app_shell_active_app->enter(s_app_shell_active_app->ctx, from_app),
                            TAG,
                            "next app enter failed");
    }

    ESP_LOGI(TAG, "switched to app: %s", s_app_shell_active_app->id);
    return ESP_OK;
}

static void app_shell_task(void *arg)
{
    const app_shell_app_t *initial_app = (const app_shell_app_t *)arg;

    ESP_ERROR_CHECK(cyd_display_claim_owner());
    s_app_shell_home_app = initial_app;
    s_app_shell_active_app = initial_app;

    if (s_app_shell_active_app != NULL && s_app_shell_active_app->enter != NULL) {
        ESP_ERROR_CHECK(s_app_shell_active_app->enter(s_app_shell_active_app->ctx, NULL));
    }

    while (true) {
        if (s_app_shell_active_app != NULL && s_app_shell_active_app->step != NULL) {
            ESP_ERROR_CHECK(s_app_shell_active_app->step(s_app_shell_active_app->ctx));
        } else {
            vTaskDelay(pdMS_TO_TICKS(50));
        }

        ESP_ERROR_CHECK(app_shell_apply_pending_switch());
        if (app_shell_request_home_if_idle()) {
            ESP_ERROR_CHECK(app_shell_apply_pending_switch());
        }
    }
}

esp_err_t app_shell_start(const app_shell_app_t *initial_app)
{
    ESP_RETURN_ON_FALSE(initial_app != NULL, ESP_ERR_INVALID_ARG, TAG, "initial app required");

    if (s_app_shell_task_handle != NULL) {
        return ESP_OK;
    }

    BaseType_t task_ok = xTaskCreate(app_shell_task,
                                     "app_shell",
                                     CONFIG_APP_SHELL_TASK_STACK_SIZE,
                                     (void *)initial_app,
                                     CONFIG_APP_SHELL_TASK_PRIORITY,
                                     &s_app_shell_task_handle);
    ESP_RETURN_ON_FALSE(task_ok == pdPASS, ESP_ERR_NO_MEM, TAG, "task create failed");
    return ESP_OK;
}

esp_err_t app_shell_switch_to(const app_shell_app_t *next_app)
{
    ESP_RETURN_ON_FALSE(next_app != NULL, ESP_ERR_INVALID_ARG, TAG, "next app required");

    portENTER_CRITICAL(&s_app_shell_lock);
    s_app_shell_pending_app = next_app;
    portEXIT_CRITICAL(&s_app_shell_lock);
    return ESP_OK;
}

const app_shell_app_t *app_shell_get_active_app(void)
{
    return s_app_shell_active_app;
}

const app_shell_app_t *app_shell_get_home_app(void)
{
    return s_app_shell_home_app;
}

bool app_shell_request_home_if_idle(void)
{
    if (!app_shell_is_idle_timeout_elapsed()) {
        return false;
    }

    ESP_LOGI(TAG,
             "idle timeout reached (%d s), returning to home app: %s",
             CONFIG_APP_SHELL_IDLE_RETURN_TIMEOUT_SECONDS,
             s_app_shell_home_app->id);
    ESP_ERROR_CHECK(app_shell_switch_to(s_app_shell_home_app));
    return true;
}
