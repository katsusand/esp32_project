#ifndef APP_SHELL_H
#define APP_SHELL_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct app_shell_app app_shell_app_t;

/*
 * Lifecycle stage that reported an error.
 * English: used only to describe a failure to `on_error` and the shell log.
 */
typedef enum {
    APP_SHELL_STAGE_ENTER = 0,
    APP_SHELL_STAGE_STEP,
    APP_SHELL_STAGE_LEAVE,
} app_shell_stage_t;

/*
 * What the shell should do after an app callback returned a non-OK esp_err_t.
 *
 * English contract: a failing app never panics the device. `CONTINUE` keeps the
 * app active and retries on the next loop iteration; `RETURN_HOME` abandons the
 * app and switches back to the home app. The shell always applies a short
 * backoff before continuing so a permanently failing app cannot starve other
 * tasks.
 */
typedef enum {
    APP_SHELL_ON_ERROR_CONTINUE = 0,
    APP_SHELL_ON_ERROR_RETURN_HOME,
} app_shell_error_action_t;

typedef struct {
    const char *app_id;
    app_shell_stage_t stage;
    esp_err_t err;
    uint32_t consecutive_errors;
} app_shell_error_t;

typedef esp_err_t (*app_shell_app_enter_fn)(void *ctx, const app_shell_app_t *from_app);
typedef esp_err_t (*app_shell_app_step_fn)(void *ctx);
typedef esp_err_t (*app_shell_app_leave_fn)(void *ctx);
typedef bool (*app_shell_app_idle_return_suppressed_fn)(void *ctx);
typedef bool (*app_shell_home_return_allowed_fn)(void *ctx);
typedef app_shell_error_action_t (*app_shell_app_error_fn)(void *ctx, const app_shell_error_t *error);

struct app_shell_app {
    const char *id;
    void *ctx;
    app_shell_app_enter_fn enter;
    app_shell_app_step_fn step;
    app_shell_app_leave_fn leave;
    app_shell_app_idle_return_suppressed_fn idle_return_suppressed;
    /*
     * Optional. When NULL the shell applies the default policy: keep retrying
     * until CONFIG_APP_SHELL_MAX_CONSECUTIVE_ERRORS consecutive failures, then
     * return to the home app.
     */
    app_shell_app_error_fn on_error;
};

esp_err_t app_shell_start(const app_shell_app_t *initial_app);
esp_err_t app_shell_switch_to(const app_shell_app_t *next_app);

/*
 * Back navigation with a guaranteed destination.
 *
 * Apps remember the `from_app` handed to enter() and return there. That pointer
 * is NULL whenever the app was not reached from another app -- when it is the
 * initial app, or when a product routes straight into it at boot. Switching to
 * NULL silently does nothing, which strands the user on a screen whose back
 * button appears to be broken.
 *
 * English contract: falls back to the home app, so a back control always has
 * somewhere to go. Prefer this over app_shell_switch_to() for back buttons.
 */
esp_err_t app_shell_return_to(const app_shell_app_t *return_app);
const app_shell_app_t *app_shell_get_active_app(void);
const app_shell_app_t *app_shell_get_home_app(void);
void app_shell_set_home_return_allowed_callback(app_shell_home_return_allowed_fn callback, void *ctx);
uint16_t app_shell_get_idle_return_timeout_seconds(void);
esp_err_t app_shell_set_idle_return_timeout_seconds(uint16_t timeout_seconds);
esp_err_t app_shell_save_idle_return_timeout_seconds(void);
bool app_shell_is_idle_timeout_elapsed(void);
bool app_shell_request_home_if_idle(void);

/*
 * Diagnostics. App failures no longer reboot the device, so they must stay
 * visible somewhere. `app_shell_get_last_error` returns false when no app error
 * has been recorded since boot.
 */
bool app_shell_get_last_error(app_shell_error_t *error);
uint32_t app_shell_get_total_error_count(void);

#ifdef __cplusplus
}
#endif

#endif
