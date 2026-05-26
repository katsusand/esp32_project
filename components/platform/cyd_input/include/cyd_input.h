#ifndef CYD_INPUT_H
#define CYD_INPUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CYD_INPUT_EVENT_TOUCH = 0,
    CYD_INPUT_EVENT_GPIO_BUTTON,
    CYD_INPUT_EVENT_ROTARY_ENCODER,
} cyd_input_event_type_t;

typedef enum {
    CYD_INPUT_SOURCE_TOUCH = 0,
    CYD_INPUT_SOURCE_BOOT_BUTTON = 1,
} cyd_input_source_id_t;

typedef enum {
    CYD_INPUT_TOUCH_ACTION_PRESS = 0,
    CYD_INPUT_TOUCH_ACTION_RELEASE,
    CYD_INPUT_TOUCH_ACTION_LONG_PRESS,
    CYD_INPUT_TOUCH_ACTION_REPEAT,
} cyd_input_touch_action_t;

typedef enum {
    CYD_INPUT_BUTTON_ACTION_PRESS = 0,
    CYD_INPUT_BUTTON_ACTION_RELEASE,
    CYD_INPUT_BUTTON_ACTION_CLICK,
    CYD_INPUT_BUTTON_ACTION_DOUBLE_CLICK,
    CYD_INPUT_BUTTON_ACTION_LONG_PRESS,
} cyd_input_button_action_t;

typedef struct {
    bool pressed;
    int16_t x;
    int16_t y;
    TickType_t tick;
} cyd_input_touch_state_t;

typedef struct {
    cyd_input_event_type_t type;
    uint8_t source_id;
    TickType_t tick;
    union {
        struct {
            cyd_input_touch_action_t action;
            bool pressed;
            int16_t x;
            int16_t y;
            uint8_t hold_ticks;
        } touch;
        struct {
            cyd_input_button_action_t action;
            bool pressed;
            uint8_t hold_seconds;
        } button;
        struct {
            int16_t delta;
            bool button_pressed;
        } encoder;
    } data;
} cyd_input_event_t;

esp_err_t cyd_input_init(void);
esp_err_t cyd_input_get_touch_state(cyd_input_touch_state_t *state);
esp_err_t cyd_input_get_touch_irq_level(int *level);
esp_err_t cyd_input_get_mode_button_touch(size_t button_count, size_t *button_index, bool *pressed);
esp_err_t cyd_input_read_event(cyd_input_event_t *event, TickType_t wait_ticks);
TickType_t cyd_input_get_last_activity_tick(void);
bool cyd_input_has_touch_calibration(void);
bool cyd_input_has_saved_touch_calibration(void);
esp_err_t cyd_input_discard_pending_events(void);
esp_err_t cyd_input_run_touch_calibration(void);
esp_err_t cyd_input_clear_touch_calibration(void);

#ifdef __cplusplus
}
#endif

#endif
