#ifndef APP_SHELL_H
#define APP_SHELL_H

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct app_shell_app app_shell_app_t;

typedef esp_err_t (*app_shell_app_enter_fn)(void *ctx, const app_shell_app_t *from_app);
typedef esp_err_t (*app_shell_app_step_fn)(void *ctx);
typedef esp_err_t (*app_shell_app_leave_fn)(void *ctx);

struct app_shell_app {
    const char *id;
    void *ctx;
    app_shell_app_enter_fn enter;
    app_shell_app_step_fn step;
    app_shell_app_leave_fn leave;
};

esp_err_t app_shell_start(const app_shell_app_t *initial_app);
esp_err_t app_shell_switch_to(const app_shell_app_t *next_app);
const app_shell_app_t *app_shell_get_active_app(void);
const app_shell_app_t *app_shell_get_home_app(void);
bool app_shell_is_idle_timeout_elapsed(void);
bool app_shell_request_home_if_idle(void);

#ifdef __cplusplus
}
#endif

#endif
