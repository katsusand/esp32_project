#ifndef CYD_STATUS_LED_H
#define CYD_STATUS_LED_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum {
    CYD_LED_COLOR_OFF = 0,
    CYD_LED_COLOR_RED,
    CYD_LED_COLOR_GREEN,
    CYD_LED_COLOR_BLUE,
    CYD_LED_COLOR_YELLOW,
    CYD_LED_COLOR_MAGENTA,
    CYD_LED_COLOR_CYAN,
    CYD_LED_COLOR_WHITE,
} cyd_led_color_t;

typedef enum {
    CYD_LED_EFFECT_OFF = 0,
    CYD_LED_EFFECT_ON,
    CYD_LED_EFFECT_BLINK_SLOW,
    CYD_LED_EFFECT_BLINK_FAST,
    CYD_LED_EFFECT_FLASH_SLOW,
    CYD_LED_EFFECT_FLASH_FAST,
} cyd_led_effect_t;

typedef enum {
    CYD_LED_PLAY_CONTINUOUS = 0,
    CYD_LED_PLAY_ONESHOT,
} cyd_led_play_t;

typedef struct {
    cyd_led_color_t color;
    cyd_led_effect_t effect;
    cyd_led_play_t play;
    uint32_t duration_ms;
} cyd_led_pattern_t;

esp_err_t cyd_status_led_init(void);
esp_err_t cyd_status_led_set_rgb(uint8_t red, uint8_t green, uint8_t blue);
esp_err_t cyd_status_led_set_base_pattern(const cyd_led_pattern_t *pattern);
esp_err_t cyd_status_led_trigger_pattern(const cyd_led_pattern_t *pattern);

#endif
