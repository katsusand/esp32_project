#ifndef APP_SHELL_H
#define APP_SHELL_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct app_shell_app app_shell_app_t;

typedef esp_err_t (*app_shell_app_enter_fn)(void *ctx, const app_shell_app_t *from_app);
typedef esp_err_t (*app_shell_app_step_fn)(void *ctx);
typedef esp_err_t (*app_shell_app_leave_fn)(void *ctx);
typedef bool (*app_shell_app_idle_return_suppressed_fn)(void *ctx);
typedef bool (*app_shell_home_return_allowed_fn)(void *ctx);

struct app_shell_app {
    const char *id;
    void *ctx;
    app_shell_app_enter_fn enter;
    app_shell_app_step_fn step;
    app_shell_app_leave_fn leave;
    app_shell_app_idle_return_suppressed_fn idle_return_suppressed;
};

esp_err_t app_shell_start(const app_shell_app_t *initial_app);
esp_err_t app_shell_switch_to(const app_shell_app_t *next_app);
const app_shell_app_t *app_shell_get_active_app(void);
const app_shell_app_t *app_shell_get_home_app(void);
void app_shell_set_home_return_allowed_callback(app_shell_home_return_allowed_fn callback, void *ctx);
uint16_t app_shell_get_idle_return_timeout_seconds(void);
esp_err_t app_shell_set_idle_return_timeout_seconds(uint16_t timeout_seconds);
esp_err_t app_shell_save_idle_return_timeout_seconds(void);
bool app_shell_is_idle_timeout_elapsed(void);
bool app_shell_request_home_if_idle(void);

#ifdef __cplusplus
}
#endif

#endif
