#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "app_stack_monitor.h"
#include "cyd_status_led.h"

#define CYD_STATUS_LED_QUEUE_LEN 8
#define CYD_STATUS_LED_TASK_STACK 3072
#define CYD_STATUS_LED_TASK_PRIO 3
#define CYD_STATUS_LED_UPDATE_MS 25

#define CYD_LED_BLINK_SLOW_PERIOD_MS 1000
#define CYD_LED_BLINK_FAST_PERIOD_MS 250
#define CYD_LED_DUTY_ON_NUM 1
#define CYD_LED_DUTY_ON_DEN 2

static const char *TAG = "cyd_status_led";

typedef enum {
    CYD_STATUS_LED_CMD_SET_BASE_PATTERN = 0,
    CYD_STATUS_LED_CMD_TRIGGER_PATTERN,
} cyd_status_led_cmd_id_t;

typedef struct {
    cyd_status_led_cmd_id_t id;
    cyd_led_pattern_t pattern;
} cyd_status_led_cmd_t;

typedef struct {
    cyd_led_pattern_t base_pattern;
    cyd_led_pattern_t overlay_pattern;
    bool overlay_active;
    int64_t base_anchor_us;
    int64_t overlay_anchor_us;
    int64_t overlay_until_us;
    uint8_t current_red;
    uint8_t current_green;
    uint8_t current_blue;
} cyd_status_led_state_t;

static QueueHandle_t s_led_queue;

static const cyd_led_pattern_t s_default_pattern = {
    .color = CYD_LED_COLOR_OFF,
    .effect = CYD_LED_EFFECT_OFF,
    .play = CYD_LED_PLAY_CONTINUOUS,
    .duration_ms = 0,
};

static inline void cyd_status_led_apply_rgb(uint8_t red, uint8_t green, uint8_t blue)
{
    /* CYD onboard RGB LED is active-low. */
    ESP_ERROR_CHECK(gpio_set_level(CONFIG_CYD_STATUS_LED_RED_GPIO, red > 0 ? 0 : 1));
    ESP_ERROR_CHECK(gpio_set_level(CONFIG_CYD_STATUS_LED_GREEN_GPIO, green > 0 ? 0 : 1));
    ESP_ERROR_CHECK(gpio_set_level(CONFIG_CYD_STATUS_LED_BLUE_GPIO, blue > 0 ? 0 : 1));
}

static void cyd_status_led_color_to_rgb(cyd_led_color_t color, uint8_t *red, uint8_t *green, uint8_t *blue)
{
    *red = 0;
    *green = 0;
    *blue = 0;

    switch (color) {
        case CYD_LED_COLOR_RED:
            *red = 255;
            break;
        case CYD_LED_COLOR_GREEN:
            *green = 255;
            break;
        case CYD_LED_COLOR_BLUE:
            *blue = 255;
            break;
        case CYD_LED_COLOR_YELLOW:
            *red = 255;
            *green = 255;
            break;
        case CYD_LED_COLOR_MAGENTA:
            *red = 255;
            *blue = 255;
            break;
        case CYD_LED_COLOR_CYAN:
            *green = 255;
            *blue = 255;
            break;
        case CYD_LED_COLOR_WHITE:
            *red = 255;
            *green = 255;
            *blue = 255;
            break;
        case CYD_LED_COLOR_OFF:
        default:
            break;
    }
}

static uint32_t cyd_status_led_effect_period_ms(cyd_led_effect_t effect)
{
    switch (effect) {
        case CYD_LED_EFFECT_BLINK_FAST:
        case CYD_LED_EFFECT_FLASH_FAST:
            return CYD_LED_BLINK_FAST_PERIOD_MS;
        case CYD_LED_EFFECT_BLINK_SLOW:
        case CYD_LED_EFFECT_FLASH_SLOW:
            return CYD_LED_BLINK_SLOW_PERIOD_MS;
        case CYD_LED_EFFECT_OFF:
        case CYD_LED_EFFECT_ON:
        default:
            return 0;
    }
}

static bool cyd_status_led_effect_phase_on(cyd_led_effect_t effect, int64_t elapsed_us)
{
    uint32_t period_ms = cyd_status_led_effect_period_ms(effect);
    int64_t period_us;
    int64_t phase_us;

    if (period_ms == 0) {
        return effect == CYD_LED_EFFECT_ON;
    }

    period_us = (int64_t)period_ms * 1000LL;
    phase_us = elapsed_us % period_us;
    if (phase_us < 0) {
        phase_us += period_us;
    }

    return phase_us < ((period_us * CYD_LED_DUTY_ON_NUM) / CYD_LED_DUTY_ON_DEN);
}

static void cyd_status_led_render_pattern(
    const cyd_led_pattern_t *pattern,
    int64_t anchor_us,
    int64_t now_us,
    uint8_t *red,
    uint8_t *green,
    uint8_t *blue)
{
    bool phase_on = false;

    *red = 0;
    *green = 0;
    *blue = 0;

    switch (pattern->effect) {
        case CYD_LED_EFFECT_OFF:
            return;
        case CYD_LED_EFFECT_ON:
            cyd_status_led_color_to_rgb(pattern->color, red, green, blue);
            return;
        case CYD_LED_EFFECT_BLINK_SLOW:
        case CYD_LED_EFFECT_BLINK_FAST:
            phase_on = cyd_status_led_effect_phase_on(pattern->effect, now_us - anchor_us);
            if (phase_on) {
                cyd_status_led_color_to_rgb(pattern->color, red, green, blue);
            }
            return;
        case CYD_LED_EFFECT_FLASH_SLOW:
        case CYD_LED_EFFECT_FLASH_FAST:
        default:
            return;
    }
}

static void cyd_status_led_render(cyd_status_led_state_t *state, int64_t now_us, uint8_t *red, uint8_t *green, uint8_t *blue)
{
    bool overlay_phase_on;

    *red = 0;
    *green = 0;
    *blue = 0;

    if (state->overlay_active && now_us >= state->overlay_until_us) {
        state->overlay_active = false;
    }

    if (!state->overlay_active) {
        cyd_status_led_render_pattern(&state->base_pattern, state->base_anchor_us, now_us, red, green, blue);
        return;
    }

    switch (state->overlay_pattern.effect) {
        case CYD_LED_EFFECT_OFF:
            *red = 0;
            *green = 0;
            *blue = 0;
            break;
        case CYD_LED_EFFECT_ON:
        case CYD_LED_EFFECT_BLINK_SLOW:
        case CYD_LED_EFFECT_BLINK_FAST:
            cyd_status_led_render_pattern(&state->overlay_pattern, state->overlay_anchor_us, now_us, red, green, blue);
            break;
        case CYD_LED_EFFECT_FLASH_SLOW:
        case CYD_LED_EFFECT_FLASH_FAST:
            overlay_phase_on = cyd_status_led_effect_phase_on(state->overlay_pattern.effect, now_us - state->overlay_anchor_us);
            if (overlay_phase_on) {
                cyd_status_led_color_to_rgb(state->overlay_pattern.color, red, green, blue);
            }
            break;
        default:
            break;
    }
}

static void cyd_status_led_task(void *arg)
{
    cyd_status_led_state_t state = {
        .base_pattern = {
            .color = CYD_LED_COLOR_OFF,
            .effect = CYD_LED_EFFECT_OFF,
            .play = CYD_LED_PLAY_CONTINUOUS,
            .duration_ms = 0,
        },
        .overlay_pattern = {
            .color = CYD_LED_COLOR_OFF,
            .effect = CYD_LED_EFFECT_OFF,
            .play = CYD_LED_PLAY_CONTINUOUS,
            .duration_ms = 0,
        },
        .overlay_active = false,
        .base_anchor_us = esp_timer_get_time(),
        .overlay_anchor_us = 0,
        .overlay_until_us = 0,
        .current_red = 0,
        .current_green = 0,
        .current_blue = 0,
    };
    cyd_status_led_cmd_t cmd;

    while (true) {
        APP_STACK_MONITOR_CHECK(TAG, "cyd_status_led", 30000);

        if (xQueueReceive(s_led_queue, &cmd, pdMS_TO_TICKS(CYD_STATUS_LED_UPDATE_MS)) == pdTRUE) {
            int64_t now_us = esp_timer_get_time();

            switch (cmd.id) {
                case CYD_STATUS_LED_CMD_SET_BASE_PATTERN:
                    state.base_pattern = cmd.pattern;
                    state.base_pattern.play = CYD_LED_PLAY_CONTINUOUS;
                    state.base_anchor_us = now_us;
                    ESP_LOGI(TAG, "Base pattern updated");
                    break;
                case CYD_STATUS_LED_CMD_TRIGGER_PATTERN:
                    state.overlay_pattern = cmd.pattern;
                    state.overlay_anchor_us = now_us;
                    if (cmd.pattern.play == CYD_LED_PLAY_ONESHOT && cmd.pattern.duration_ms > 0) {
                        state.overlay_active = true;
                        state.overlay_until_us = now_us + ((int64_t)cmd.pattern.duration_ms * 1000LL);
                    } else {
                        state.overlay_active = false;
                        state.overlay_until_us = 0;
                    }
                    ESP_LOGI(TAG, "Overlay pattern triggered");
                    break;
                default:
                    break;
            }
        }

        uint8_t red = 0;
        uint8_t green = 0;
        uint8_t blue = 0;

        cyd_status_led_render(&state, esp_timer_get_time(), &red, &green, &blue);

        if (red != state.current_red || green != state.current_green || blue != state.current_blue) {
            cyd_status_led_apply_rgb(red, green, blue);
            state.current_red = red;
            state.current_green = green;
            state.current_blue = blue;
        }
    }
}

static esp_err_t cyd_status_led_send(cyd_status_led_cmd_t cmd)
{
    ESP_RETURN_ON_FALSE(s_led_queue != NULL, ESP_ERR_INVALID_STATE, TAG, "LED task not initialized");
    return xQueueSend(s_led_queue, &cmd, pdMS_TO_TICKS(100)) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t cyd_status_led_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << CONFIG_CYD_STATUS_LED_RED_GPIO) |
                        (1ULL << CONFIG_CYD_STATUS_LED_GREEN_GPIO) |
                        (1ULL << CONFIG_CYD_STATUS_LED_BLUE_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    if (s_led_queue != NULL) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(gpio_config(&io_conf), TAG, "gpio init failed");
    cyd_status_led_apply_rgb(0, 0, 0);

    s_led_queue = xQueueCreate(CYD_STATUS_LED_QUEUE_LEN, sizeof(cyd_status_led_cmd_t));
    ESP_RETURN_ON_FALSE(s_led_queue != NULL, ESP_ERR_NO_MEM, TAG, "queue alloc failed");

    BaseType_t task_ok = xTaskCreatePinnedToCore(
        cyd_status_led_task,
        "cyd_status_led",
        CYD_STATUS_LED_TASK_STACK,
        NULL,
        CYD_STATUS_LED_TASK_PRIO,
        NULL,
        tskNO_AFFINITY
    );
    ESP_RETURN_ON_FALSE(task_ok == pdPASS, ESP_ERR_NO_MEM, TAG, "task create failed");

    return ESP_OK;
}

esp_err_t cyd_status_led_set_rgb(uint8_t red, uint8_t green, uint8_t blue)
{
    cyd_status_led_apply_rgb(red, green, blue);
    return ESP_OK;
}

esp_err_t cyd_status_led_set_base_pattern(const cyd_led_pattern_t *pattern)
{
    cyd_status_led_cmd_t cmd = {
        .id = CYD_STATUS_LED_CMD_SET_BASE_PATTERN,
        .pattern = s_default_pattern,
    };

    ESP_RETURN_ON_FALSE(pattern != NULL, ESP_ERR_INVALID_ARG, TAG, "pattern is null");
    cmd.pattern = *pattern;
    return cyd_status_led_send(cmd);
}

esp_err_t cyd_status_led_trigger_pattern(const cyd_led_pattern_t *pattern)
{
    cyd_status_led_cmd_t cmd = {
        .id = CYD_STATUS_LED_CMD_TRIGGER_PATTERN,
        .pattern = s_default_pattern,
    };

    ESP_RETURN_ON_FALSE(pattern != NULL, ESP_ERR_INVALID_ARG, TAG, "pattern is null");
    cmd.pattern = *pattern;
    return cyd_status_led_send(cmd);
}
