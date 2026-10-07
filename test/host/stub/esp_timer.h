#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
/* Declarations only; a test that needs timers defines them and moves the clock by hand. */
typedef struct esp_timer *esp_timer_handle_t;
typedef void (*esp_timer_cb_t)(void *arg);
typedef struct {
    esp_timer_cb_t callback;
    void *arg;
    const char *name;
} esp_timer_create_args_t;
int64_t esp_timer_get_time(void);
esp_err_t esp_timer_create(const esp_timer_create_args_t *args, esp_timer_handle_t *out_handle);
esp_err_t esp_timer_start_once(esp_timer_handle_t timer, uint64_t timeout_us);
esp_err_t esp_timer_stop(esp_timer_handle_t timer);
bool esp_timer_is_active(esp_timer_handle_t timer);
