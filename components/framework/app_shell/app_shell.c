#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/portmacro.h"
#include "esp_check.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_health.h"
#include "nvs_schema.h"
#include "sdkconfig.h"
#include "app_stack_monitor.h"
#include "app_shell.h"
#include "cyd_display.h"
#include "cyd_input.h"

/* This component owns this data, so the namespace is declared here.
   The "sys_" prefix is what lets nvs_schema classify it by scanning flash. */
NVS_SCHEMA_DECLARE_NS(NVS_NS, "sys_shell");
static const nvs_key_descriptor_t NVS_KEY_APP_SHELL_CONFIG = {
    .ns = NVS_NS,
    .key = "config_v1",
};

#define TAG "app_shell"
#define APP_SHELL_CONFIG_VERSION 1U
#define APP_SHELL_MAX_SWITCH_CHAIN 4

typedef struct {
    uint32_t version;
    uint16_t idle_timeout_seconds;
    uint16_t reserved;
} app_shell_disk_t;

static TaskHandle_t s_app_shell_task_handle;
static portMUX_TYPE s_app_shell_lock = portMUX_INITIALIZER_UNLOCKED;
static const app_shell_app_t *s_app_shell_active_app;
static const app_shell_app_t *s_app_shell_pending_app;
static const app_shell_app_t *s_app_shell_home_app;
static app_shell_home_return_allowed_fn s_app_shell_home_return_allowed_callback;
static void *s_app_shell_home_return_allowed_ctx;
static bool s_app_shell_idle_timeout_loaded;
static uint16_t s_app_shell_idle_timeout_seconds = CONFIG_APP_SHELL_IDLE_RETURN_TIMEOUT_SECONDS;
static uint32_t s_app_shell_consecutive_errors;
static uint32_t s_app_shell_total_errors;
static app_shell_error_t s_app_shell_last_error;

static const char *app_shell_stage_name(app_shell_stage_t stage)
{
    switch (stage) {
    case APP_SHELL_STAGE_ENTER:
        return "enter";
    case APP_SHELL_STAGE_STEP:
        return "step";
    case APP_SHELL_STAGE_LEAVE:
        return "leave";
    default:
        return "unknown";
    }
}

/*
 * Central app failure policy.
 *
 * English contract: an app returning a non-OK esp_err_t is an expected runtime
 * condition (I2C NACK, UART overflow, sensor timeout), not a programming error,
 * so it must never reach ESP_ERROR_CHECK. The backoff below is mandatory: a
 * step() that fails immediately would otherwise spin this task at its priority
 * and starve everything else.
 */
static app_shell_error_action_t app_shell_handle_app_error(const app_shell_app_t *app,
                                                          app_shell_stage_t stage,
                                                          esp_err_t err)
{
    app_shell_error_action_t action = APP_SHELL_ON_ERROR_CONTINUE;

    /* s_app_shell_consecutive_errors is shell-task private; the reported
       counters are also read by diagnostics UI, so they move together. */
    s_app_shell_consecutive_errors++;

    app_shell_error_t error = {
        .app_id = (app != NULL && app->id != NULL) ? app->id : "(unknown)",
        .stage = stage,
        .err = err,
        .consecutive_errors = s_app_shell_consecutive_errors,
    };

    portENTER_CRITICAL(&s_app_shell_lock);
    s_app_shell_total_errors++;
    s_app_shell_last_error = error;
    portEXIT_CRITICAL(&s_app_shell_lock);

    ESP_LOGE(TAG,
             "app '%s' %s failed: %s (consecutive=%u total=%u)",
             error.app_id,
             app_shell_stage_name(stage),
             esp_err_to_name(err),
             (unsigned)s_app_shell_consecutive_errors,
             (unsigned)s_app_shell_total_errors);

    if (app != NULL && app->on_error != NULL) {
        action = app->on_error(app->ctx, &error);
    } else if (s_app_shell_consecutive_errors >= CONFIG_APP_SHELL_MAX_CONSECUTIVE_ERRORS) {
        action = APP_SHELL_ON_ERROR_RETURN_HOME;
    }

    vTaskDelay(pdMS_TO_TICKS(CONFIG_APP_SHELL_ERROR_BACKOFF_MS));
    return action;
}

static void app_shell_clear_consecutive_errors(void)
{
    s_app_shell_consecutive_errors = 0;
}

/*
 * Returning home is only useful when a *different* app is failing. When the
 * home app itself fails we keep it active and rely on the backoff, because
 * switching home to home would just re-run the same failing code.
 */
static void app_shell_recover_to_home(const app_shell_app_t *failed_app)
{
    if (s_app_shell_home_app == NULL || failed_app == s_app_shell_home_app) {
        return;
    }

    ESP_LOGW(TAG,
             "app '%s' exceeded the error budget, returning to home app: %s",
             (failed_app != NULL && failed_app->id != NULL) ? failed_app->id : "(unknown)",
             s_app_shell_home_app->id);
    app_shell_clear_consecutive_errors();
    (void)app_shell_switch_to(s_app_shell_home_app);
}

static esp_err_t app_shell_load_idle_return_timeout_blob(uint16_t *timeout_seconds)
{
    nvs_handle_t handle = 0;
    app_shell_disk_t disk = { 0 };
    size_t disk_size = sizeof(disk);

    ESP_RETURN_ON_FALSE(timeout_seconds != NULL, ESP_ERR_INVALID_ARG, TAG, "timeout pointer is null");

    esp_err_t err = nvs_open_descriptor(NVS_KEY_APP_SHELL_CONFIG.ns, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_get_blob(handle, NVS_KEY_APP_SHELL_CONFIG.key, &disk, &disk_size);
    nvs_close(handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_NVS_INVALID_LENGTH) {
            nvs_health_report_invalid(&NVS_KEY_APP_SHELL_CONFIG, err, "invalid app shell blob length");
        }
        return err;
    }
    if (disk_size != sizeof(disk) || disk.version != APP_SHELL_CONFIG_VERSION) {
        nvs_health_report_invalid(&NVS_KEY_APP_SHELL_CONFIG, ESP_ERR_INVALID_VERSION, "invalid app shell blob");
        return ESP_ERR_INVALID_VERSION;
    }

    *timeout_seconds = disk.idle_timeout_seconds;
    return ESP_OK;
}

static esp_err_t app_shell_write_idle_return_timeout_blob(uint16_t timeout_seconds)
{
    nvs_handle_t handle = 0;
    app_shell_disk_t disk = {
        .version = APP_SHELL_CONFIG_VERSION,
        .idle_timeout_seconds = timeout_seconds,
    };

    ESP_RETURN_ON_ERROR(nvs_open_descriptor(NVS_KEY_APP_SHELL_CONFIG.ns, NVS_READWRITE, &handle),
                        TAG,
                        "open NVS failed");
    esp_err_t err = nvs_set_blob(handle, NVS_KEY_APP_SHELL_CONFIG.key, &disk, sizeof(disk));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

static esp_err_t app_shell_load_idle_return_timeout_if_needed(void)
{
    portENTER_CRITICAL(&s_app_shell_lock);
    bool loaded = s_app_shell_idle_timeout_loaded;
    portEXIT_CRITICAL(&s_app_shell_lock);
    if (loaded) {
        return ESP_OK;
    }

    uint16_t timeout_seconds = CONFIG_APP_SHELL_IDLE_RETURN_TIMEOUT_SECONDS;
    esp_err_t err = app_shell_load_idle_return_timeout_blob(&timeout_seconds);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        return err;
    }

    portENTER_CRITICAL(&s_app_shell_lock);
    if (!s_app_shell_idle_timeout_loaded) {
        s_app_shell_idle_timeout_seconds = timeout_seconds;
        s_app_shell_idle_timeout_loaded = true;
    }
    portEXIT_CRITICAL(&s_app_shell_lock);
    return ESP_OK;
}

static bool app_shell_idle_return_enabled(void)
{
    return app_shell_get_idle_return_timeout_seconds() > 0;
}

static bool app_shell_is_idle_return_suppressed(void)
{
    const app_shell_app_t *active_app = s_app_shell_active_app;
    return active_app != NULL &&
           active_app->idle_return_suppressed != NULL &&
           active_app->idle_return_suppressed(active_app->ctx);
}

static bool app_shell_is_home_idle_target(void)
{
    return s_app_shell_home_app != NULL &&
           s_app_shell_active_app != NULL &&
           s_app_shell_active_app != s_app_shell_home_app;
}

static bool app_shell_can_return_home_now(void)
{
    if (!cyd_input_has_saved_touch_calibration()) {
        return false;
    }

    portENTER_CRITICAL(&s_app_shell_lock);
    app_shell_home_return_allowed_fn callback = s_app_shell_home_return_allowed_callback;
    void *ctx = s_app_shell_home_return_allowed_ctx;
    portEXIT_CRITICAL(&s_app_shell_lock);

    if (callback != NULL && !callback(ctx)) {
        return false;
    }

    return true;
}

bool app_shell_is_idle_timeout_elapsed(void)
{
    TickType_t last_activity_tick = 0;
    TickType_t now = 0;
    TickType_t timeout_ticks = 0;

    if (!app_shell_idle_return_enabled() ||
        !app_shell_is_home_idle_target() ||
        app_shell_is_idle_return_suppressed()) {
        return false;
    }

    last_activity_tick = cyd_input_get_last_activity_tick();
    now = xTaskGetTickCount();
    timeout_ticks = pdMS_TO_TICKS((uint32_t)app_shell_get_idle_return_timeout_seconds() * 1000U);
    if (last_activity_tick == 0 || timeout_ticks == 0) {
        return false;
    }

    return (now - last_activity_tick) >= timeout_ticks;
}

static bool app_shell_apply_one_pending_switch(void)
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
        return false;
    }

    from_app = s_app_shell_active_app;
    if (from_app != NULL && from_app->leave != NULL) {
        esp_err_t leave_err = from_app->leave(from_app->ctx);
        if (leave_err != ESP_OK) {
            /*
             * A failing leave() must not trap the user inside the app, so the
             * switch proceeds regardless of the requested action.
             */
            (void)app_shell_handle_app_error(from_app, APP_SHELL_STAGE_LEAVE, leave_err);
        }
    }

    s_app_shell_active_app = next_app;
    if (s_app_shell_active_app->enter != NULL) {
        esp_err_t enter_err = s_app_shell_active_app->enter(s_app_shell_active_app->ctx, from_app);
        if (enter_err != ESP_OK) {
            if (app_shell_handle_app_error(s_app_shell_active_app, APP_SHELL_STAGE_ENTER, enter_err) ==
                APP_SHELL_ON_ERROR_RETURN_HOME) {
                app_shell_recover_to_home(s_app_shell_active_app);
            }
            return true;
        }
    }

    app_shell_clear_consecutive_errors();
    ESP_LOGI(TAG, "switched to app: %s", s_app_shell_active_app->id);
    return true;
}

/*
 * A failing enter() can itself queue a home switch, and that switch must land
 * before the next step() runs on a half-entered app. The chain is bounded so a
 * pair of apps that bounce to each other cannot livelock the shell task.
 */
static void app_shell_apply_pending_switch(void)
{
    for (uint8_t i = 0; i < APP_SHELL_MAX_SWITCH_CHAIN; i++) {
        if (!app_shell_apply_one_pending_switch()) {
            return;
        }
    }
    ESP_LOGW(TAG, "pending app switch chain exceeded %d hops", APP_SHELL_MAX_SWITCH_CHAIN);
}

static void app_shell_task(void *arg)
{
    const app_shell_app_t *initial_app = (const app_shell_app_t *)arg;

    ESP_ERROR_CHECK(cyd_display_claim_owner());
    s_app_shell_home_app = initial_app;
    s_app_shell_active_app = initial_app;

    if (s_app_shell_active_app != NULL && s_app_shell_active_app->enter != NULL) {
        esp_err_t enter_err = s_app_shell_active_app->enter(s_app_shell_active_app->ctx, NULL);
        if (enter_err != ESP_OK) {
            /*
             * The initial app is also the home app, so there is nowhere to fall
             * back to. Keep the shell alive and let the step backoff throttle it
             * instead of panicking into a boot loop.
             */
            (void)app_shell_handle_app_error(s_app_shell_active_app, APP_SHELL_STAGE_ENTER, enter_err);
        }
    }

    while (true) {
        APP_STACK_MONITOR_CHECK(TAG, "app_shell", 30000);

        const app_shell_app_t *active_app = s_app_shell_active_app;
        if (active_app != NULL && active_app->step != NULL) {
            esp_err_t step_err = active_app->step(active_app->ctx);
            if (step_err == ESP_OK) {
                app_shell_clear_consecutive_errors();
            } else if (app_shell_handle_app_error(active_app, APP_SHELL_STAGE_STEP, step_err) ==
                       APP_SHELL_ON_ERROR_RETURN_HOME) {
                app_shell_recover_to_home(active_app);
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(50));
        }

        app_shell_apply_pending_switch();
        if (app_shell_request_home_if_idle()) {
            app_shell_apply_pending_switch();
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

esp_err_t app_shell_return_to(const app_shell_app_t *return_app)
{
    const app_shell_app_t *target = (return_app != NULL) ? return_app : s_app_shell_home_app;

    ESP_RETURN_ON_FALSE(target != NULL, ESP_ERR_INVALID_STATE, TAG, "no return target and no home app");
    if (return_app == NULL) {
        ESP_LOGD(TAG, "no return app recorded, falling back to home: %s", target->id);
    }
    return app_shell_switch_to(target);
}

const app_shell_app_t *app_shell_get_active_app(void)
{
    return s_app_shell_active_app;
}

const app_shell_app_t *app_shell_get_home_app(void)
{
    return s_app_shell_home_app;
}

void app_shell_set_home_return_allowed_callback(app_shell_home_return_allowed_fn callback, void *ctx)
{
    portENTER_CRITICAL(&s_app_shell_lock);
    s_app_shell_home_return_allowed_callback = callback;
    s_app_shell_home_return_allowed_ctx = ctx;
    portEXIT_CRITICAL(&s_app_shell_lock);
}

uint16_t app_shell_get_idle_return_timeout_seconds(void)
{
    esp_err_t err = app_shell_load_idle_return_timeout_if_needed();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "load idle return timeout failed: %s", esp_err_to_name(err));
    }

    portENTER_CRITICAL(&s_app_shell_lock);
    uint16_t timeout_seconds = s_app_shell_idle_timeout_seconds;
    portEXIT_CRITICAL(&s_app_shell_lock);
    return timeout_seconds;
}

esp_err_t app_shell_set_idle_return_timeout_seconds(uint16_t timeout_seconds)
{
    ESP_RETURN_ON_ERROR(app_shell_load_idle_return_timeout_if_needed(), TAG, "load idle timeout failed");
    portENTER_CRITICAL(&s_app_shell_lock);
    s_app_shell_idle_timeout_seconds = timeout_seconds;
    portEXIT_CRITICAL(&s_app_shell_lock);
    return ESP_OK;
}

esp_err_t app_shell_save_idle_return_timeout_seconds(void)
{
    ESP_RETURN_ON_ERROR(app_shell_load_idle_return_timeout_if_needed(), TAG, "load idle timeout failed");
    uint16_t timeout_seconds = app_shell_get_idle_return_timeout_seconds();
    return app_shell_write_idle_return_timeout_blob(timeout_seconds);
}

bool app_shell_request_home_if_idle(void)
{
    if (!app_shell_is_idle_timeout_elapsed()) {
        return false;
    }
    if (!app_shell_can_return_home_now()) {
        return false;
    }

    ESP_LOGI(TAG,
             "idle timeout reached (%d s), returning to home app: %s",
             app_shell_get_idle_return_timeout_seconds(),
             s_app_shell_home_app->id);
    (void)app_shell_switch_to(s_app_shell_home_app);
    return true;
}

bool app_shell_get_last_error(app_shell_error_t *error)
{
    if (error == NULL) {
        return false;
    }

    portENTER_CRITICAL(&s_app_shell_lock);
    app_shell_error_t last_error = s_app_shell_last_error;
    uint32_t total_errors = s_app_shell_total_errors;
    portEXIT_CRITICAL(&s_app_shell_lock);

    if (total_errors == 0) {
        return false;
    }

    *error = last_error;
    return true;
}

uint32_t app_shell_get_total_error_count(void)
{
    portENTER_CRITICAL(&s_app_shell_lock);
    uint32_t total_errors = s_app_shell_total_errors;
    portEXIT_CRITICAL(&s_app_shell_lock);
    return total_errors;
}
