#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_log.h"
#include "nvs.h"
#include "sdkconfig.h"
#include "app_stack_monitor.h"
#include "cyd_display.h"
#include "cyd_input.h"

#ifndef CONFIG_CYD_INPUT_LONG_PRESS_REPEAT_MS
#define CONFIG_CYD_INPUT_LONG_PRESS_REPEAT_MS 1000
#endif
#ifndef CONFIG_CYD_TOUCH_ENABLED
#define CONFIG_CYD_TOUCH_ENABLED 0
#endif
#ifndef CONFIG_CYD_TOUCH_PIN_INT
#define CONFIG_CYD_TOUCH_PIN_INT -1
#endif
#ifndef CONFIG_CYD_TOUCH_POLL_PERIOD_MS
#define CONFIG_CYD_TOUCH_POLL_PERIOD_MS 10
#endif
#ifndef CONFIG_CYD_TOUCH_USE_IRQ
#define CONFIG_CYD_TOUCH_USE_IRQ 0
#endif
#ifndef CONFIG_CYD_TOUCH_IDLE_POLL_PERIOD_MS
#define CONFIG_CYD_TOUCH_IDLE_POLL_PERIOD_MS 250
#endif
#ifndef CONFIG_CYD_TOUCH_LOG_EVENTS
#define CONFIG_CYD_TOUCH_LOG_EVENTS 0
#endif
#ifndef CONFIG_CYD_TOUCH_LOG_IRQ_LEVEL
#define CONFIG_CYD_TOUCH_LOG_IRQ_LEVEL 0
#endif
#ifndef CONFIG_CYD_TOUCH_USE_NVS_CALIBRATION
#define CONFIG_CYD_TOUCH_USE_NVS_CALIBRATION 0
#endif
#ifndef CONFIG_CYD_TOUCH_RUN_CALIBRATION_ON_BOOT
#define CONFIG_CYD_TOUCH_RUN_CALIBRATION_ON_BOOT 0
#endif
#ifndef CONFIG_CYD_INPUT_BINARY_STABLE_COUNT
#define CONFIG_CYD_INPUT_BINARY_STABLE_COUNT 2
#endif
#ifndef CONFIG_CYD_BOOT_BUTTON_ENABLED
#define CONFIG_CYD_BOOT_BUTTON_ENABLED 0
#endif
#ifndef CONFIG_CYD_BOOT_BUTTON_GPIO
#define CONFIG_CYD_BOOT_BUTTON_GPIO 0
#endif
#ifndef CONFIG_CYD_BOOT_BUTTON_ENABLE_INTERNAL_PULLUP
#define CONFIG_CYD_BOOT_BUTTON_ENABLE_INTERNAL_PULLUP 1
#endif
#ifndef CONFIG_CYD_BOOT_BUTTON_LONG_PRESS_MAX_SECONDS
#define CONFIG_CYD_BOOT_BUTTON_LONG_PRESS_MAX_SECONDS 15
#endif
#ifndef CONFIG_CYD_BOOT_BUTTON_DOUBLE_CLICK_TIMEOUT_MS
#define CONFIG_CYD_BOOT_BUTTON_DOUBLE_CLICK_TIMEOUT_MS 500
#endif

typedef struct {
    uint32_t magic;
    uint16_t params[8];
} cyd_touch_calibration_store_t;

typedef struct {
    bool initialized;
    bool touch_calibration_loaded;
    bool touch_stable_pressed;
    bool last_touch_sample_pressed;
    bool touch_long_press_reported;
    bool touch_suppress_click_on_release;
    bool boot_button_stable_pressed;
    bool boot_button_last_sample_pressed;
    bool boot_button_click_pending;
    uint8_t touch_long_press_stage;
    uint8_t boot_button_stable_count;
    uint8_t boot_button_long_press_seconds;
    TickType_t touch_press_tick;
    TickType_t boot_button_press_tick;
    TickType_t boot_button_release_tick;
    int16_t last_touch_x;
    int16_t last_touch_y;
    cyd_input_touch_state_t touch_state;
    SemaphoreHandle_t mutex;
    QueueHandle_t event_queue;
    TaskHandle_t task_handle;
} cyd_input_state_t;

static const char *TAG = "cyd_input";
static const char *NVS_NAMESPACE = "cyd_display";
static const char *NVS_TOUCH_CAL_KEY = "touch_cal";
static const uint32_t TOUCH_CAL_MAGIC = 0x43594454;

static cyd_input_state_t s_input = {
    .initialized = false,
    .touch_calibration_loaded = false,
    .touch_stable_pressed = false,
    .last_touch_sample_pressed = false,
    .touch_long_press_reported = false,
    .touch_suppress_click_on_release = false,
    .boot_button_stable_pressed = false,
    .boot_button_last_sample_pressed = false,
    .boot_button_click_pending = false,
    .touch_long_press_stage = 0,
    .boot_button_stable_count = 0,
    .boot_button_long_press_seconds = 0,
    .touch_press_tick = 0,
    .boot_button_press_tick = 0,
    .boot_button_release_tick = 0,
    .last_touch_x = 0,
    .last_touch_y = 0,
    .touch_state = {
        .pressed = false,
        .x = 0,
        .y = 0,
        .tick = 0,
    },
    .mutex = NULL,
    .event_queue = NULL,
    .task_handle = NULL,
};

static esp_err_t cyd_input_check_ready(void)
{
    ESP_RETURN_ON_FALSE(s_input.initialized, ESP_ERR_INVALID_STATE, TAG, "input not initialized");
    return ESP_OK;
}

static int cyd_input_touch_irq_level(void)
{
    if (CONFIG_CYD_TOUCH_PIN_INT < 0) {
        return -1;
    }
    return gpio_get_level((gpio_num_t)CONFIG_CYD_TOUCH_PIN_INT);
}

static bool cyd_input_touch_irq_enabled(void)
{
    return CONFIG_CYD_TOUCH_USE_IRQ && CONFIG_CYD_TOUCH_PIN_INT >= 0;
}

static bool cyd_input_touch_sampling_active(void)
{
    return s_input.last_touch_sample_pressed ||
           s_input.touch_stable_pressed;
}

static bool cyd_input_boot_button_enabled(void)
{
    return CONFIG_CYD_BOOT_BUTTON_ENABLED && CONFIG_CYD_BOOT_BUTTON_GPIO >= 0;
}

static bool cyd_input_task_needed(void)
{
    return CONFIG_CYD_TOUCH_ENABLED || cyd_input_boot_button_enabled();
}

static bool cyd_input_boot_button_read_pressed(void)
{
    if (!cyd_input_boot_button_enabled()) {
        return false;
    }
    return gpio_get_level((gpio_num_t)CONFIG_CYD_BOOT_BUTTON_GPIO) == 0;
}

static void IRAM_ATTR cyd_input_touch_irq_isr(void *arg)
{
    (void)arg;
    BaseType_t task_woken = pdFALSE;

    if (s_input.task_handle != NULL) {
        vTaskNotifyGiveFromISR(s_input.task_handle, &task_woken);
    }
    if (task_woken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

static const char *cyd_input_touch_action_name(cyd_input_touch_action_t action)
{
    switch (action) {
    case CYD_INPUT_TOUCH_ACTION_PRESS:
        return "press";
    case CYD_INPUT_TOUCH_ACTION_RELEASE:
        return "release";
    case CYD_INPUT_TOUCH_ACTION_LONG_PRESS:
        return "long_press";
    default:
        return "unknown";
    }
}

static const char *cyd_input_button_action_name(cyd_input_button_action_t action)
{
    switch (action) {
    case CYD_INPUT_BUTTON_ACTION_PRESS:
        return "press";
    case CYD_INPUT_BUTTON_ACTION_RELEASE:
        return "release";
    case CYD_INPUT_BUTTON_ACTION_CLICK:
        return "click";
    case CYD_INPUT_BUTTON_ACTION_DOUBLE_CLICK:
        return "double_click";
    case CYD_INPUT_BUTTON_ACTION_LONG_PRESS:
        return "long_press";
    default:
        return "unknown";
    }
}

static void cyd_input_log_touch_event(cyd_input_touch_action_t action,
                                      bool pressed,
                                      int16_t x,
                                      int16_t y,
                                      uint8_t hold_ticks)
{
#if CONFIG_CYD_TOUCH_LOG_EVENTS
    if (CONFIG_CYD_TOUCH_LOG_IRQ_LEVEL) {
        ESP_LOGI(TAG,
                 "touch action=%s pressed=%d x=%d y=%d hold=%u irq=%d",
                 cyd_input_touch_action_name(action),
                 pressed,
                 x,
                 y,
                 (unsigned)hold_ticks,
                 cyd_input_touch_irq_level());
    } else {
        ESP_LOGI(TAG,
                 "touch action=%s pressed=%d x=%d y=%d hold=%u",
                 cyd_input_touch_action_name(action),
                 pressed,
                 x,
                 y,
                 (unsigned)hold_ticks);
    }
#else
    (void)action;
    (void)pressed;
    (void)x;
    (void)y;
    (void)hold_ticks;
#endif
}

static void cyd_input_log_touch_raw_change(bool pressed, int16_t x, int16_t y, int16_t last_x, int16_t last_y)
{
#if CONFIG_CYD_TOUCH_LOG_EVENTS
    if (CONFIG_CYD_TOUCH_LOG_IRQ_LEVEL) {
        if (pressed) {
            ESP_LOGI(TAG,
                     "touch raw pressed=1 x=%d y=%d irq=%d",
                     x,
                     y,
                     cyd_input_touch_irq_level());
        } else {
            ESP_LOGI(TAG,
                     "touch raw pressed=0 last_x=%d last_y=%d irq=%d",
                     last_x,
                     last_y,
                     cyd_input_touch_irq_level());
        }
    } else {
        if (pressed) {
            ESP_LOGI(TAG, "touch raw pressed=1 x=%d y=%d", x, y);
        } else {
            ESP_LOGI(TAG, "touch raw pressed=0 last_x=%d last_y=%d", last_x, last_y);
        }
    }
#else
    (void)pressed;
    (void)x;
    (void)y;
    (void)last_x;
    (void)last_y;
#endif
}

static void cyd_input_queue_event(const cyd_input_event_t *event, const char *event_name)
{
    if (s_input.event_queue == NULL || event == NULL) {
        return;
    }

    if (xQueueSendToBack(s_input.event_queue, event, 0) == pdTRUE) {
        return;
    }

    cyd_input_event_t dropped = { 0 };
    (void)xQueueReceive(s_input.event_queue, &dropped, 0);
    if (xQueueSendToBack(s_input.event_queue, event, 0) != pdTRUE) {
        ESP_LOGW(TAG, "input event queue full; dropping newest %s event", event_name);
        return;
    }

    ESP_LOGW(TAG, "input event queue full; dropped oldest event for %s event", event_name);
}

static void cyd_input_queue_touch_event(cyd_input_touch_action_t action,
                                        bool pressed,
                                        int16_t x,
                                        int16_t y,
                                        uint8_t hold_ticks,
                                        TickType_t tick)
{
    cyd_input_event_t event = {
        .type = CYD_INPUT_EVENT_TOUCH,
        .source_id = CYD_INPUT_SOURCE_TOUCH,
        .tick = tick,
        .data.touch = {
            .action = action,
            .pressed = pressed,
            .x = x,
            .y = y,
            .hold_ticks = hold_ticks,
        },
    };

    cyd_input_queue_event(&event, "touch");
}

static void cyd_input_emit_boot_button_event(cyd_input_button_action_t action,
                                             bool pressed,
                                             uint8_t hold_seconds,
                                             TickType_t tick)
{
    cyd_input_event_t event = {
        .type = CYD_INPUT_EVENT_GPIO_BUTTON,
        .source_id = CYD_INPUT_SOURCE_BOOT_BUTTON,
        .tick = tick,
        .data.button = {
            .action = action,
            .pressed = pressed,
            .hold_seconds = hold_seconds,
        },
    };

    ESP_LOGI(TAG,
             "boot button action=%s pressed=%d hold_seconds=%u gpio=%d",
             cyd_input_button_action_name(action),
             pressed,
             (unsigned)hold_seconds,
             CONFIG_CYD_BOOT_BUTTON_GPIO);
    cyd_input_queue_event(&event, "boot button");
}

static void cyd_input_update_touch_state(bool pressed, int16_t x, int16_t y, TickType_t tick)
{
    if (xSemaphoreTake(s_input.mutex, portMAX_DELAY) == pdTRUE) {
        s_input.touch_state.pressed = pressed;
        s_input.touch_state.x = pressed ? x : 0;
        s_input.touch_state.y = pressed ? y : 0;
        s_input.touch_state.tick = tick;
        xSemaphoreGive(s_input.mutex);
    }
}

static void cyd_input_emit_touch_event(cyd_input_touch_action_t action,
                                       bool pressed,
                                       int16_t x,
                                       int16_t y,
                                       uint8_t hold_ticks,
                                       TickType_t tick)
{
    cyd_input_log_touch_event(action, pressed, x, y, hold_ticks);
    cyd_input_queue_touch_event(action, pressed, x, y, hold_ticks, tick);
}

static void cyd_input_handle_boot_button_short_release(TickType_t now)
{
    if (s_input.boot_button_click_pending) {
        s_input.boot_button_click_pending = false;
        cyd_input_emit_boot_button_event(CYD_INPUT_BUTTON_ACTION_DOUBLE_CLICK, false, 0, now);
        return;
    }

    s_input.boot_button_click_pending = true;
    s_input.boot_button_release_tick = now;
}

static void cyd_input_finalize_boot_button_click(TickType_t now)
{
    if (!s_input.boot_button_click_pending) {
        return;
    }

    TickType_t timeout_ticks = pdMS_TO_TICKS(CONFIG_CYD_BOOT_BUTTON_DOUBLE_CLICK_TIMEOUT_MS);
    if (timeout_ticks == 0) {
        timeout_ticks = 1;
    }
    if ((now - s_input.boot_button_release_tick) < timeout_ticks) {
        return;
    }

    s_input.boot_button_click_pending = false;
    cyd_input_emit_boot_button_event(CYD_INPUT_BUTTON_ACTION_CLICK, false, 0, now);
}

static void cyd_input_flush_boot_button_click(TickType_t now)
{
    if (!s_input.boot_button_click_pending) {
        return;
    }

    s_input.boot_button_click_pending = false;
    cyd_input_emit_boot_button_event(CYD_INPUT_BUTTON_ACTION_CLICK, false, 0, now);
}

static void cyd_input_handle_stable_touch_change(bool pressed, int16_t x, int16_t y, TickType_t now)
{
    s_input.touch_stable_pressed = pressed;

    if (pressed) {
        s_input.last_touch_x = x;
        s_input.last_touch_y = y;
        cyd_input_update_touch_state(true, x, y, now);
        s_input.touch_press_tick = now;
        s_input.touch_long_press_reported = false;
        s_input.touch_suppress_click_on_release = false;
        s_input.touch_long_press_stage = 0;
        cyd_input_emit_touch_event(CYD_INPUT_TOUCH_ACTION_PRESS, true, x, y, 0, now);
        return;
    }

    int16_t release_x = s_input.last_touch_x;
    int16_t release_y = s_input.last_touch_y;
    cyd_input_update_touch_state(false, release_x, release_y, now);
    cyd_input_emit_touch_event(CYD_INPUT_TOUCH_ACTION_RELEASE, false, release_x, release_y, 0, now);

    s_input.touch_long_press_reported = false;
    s_input.touch_suppress_click_on_release = false;
    s_input.touch_long_press_stage = 0;
}

static void cyd_input_handle_touch_hold(TickType_t now)
{
    TickType_t elapsed;
    TickType_t initial_ticks;
    TickType_t repeat_ticks;
    uint8_t hold_stage;

    if (!s_input.touch_stable_pressed) {
        return;
    }

    initial_ticks = pdMS_TO_TICKS(CONFIG_CYD_INPUT_LONG_PRESS_MS);
    repeat_ticks = pdMS_TO_TICKS(CONFIG_CYD_INPUT_LONG_PRESS_REPEAT_MS);
    if (initial_ticks == 0) {
        initial_ticks = 1;
    }
    if (repeat_ticks == 0) {
        repeat_ticks = 1;
    }

    elapsed = now - s_input.touch_press_tick;
    if (elapsed < initial_ticks) {
        return;
    }

    hold_stage = 1u + (uint8_t)((elapsed - initial_ticks) / repeat_ticks);
    if (hold_stage > 15u) {
        hold_stage = 15u;
    }

    if (s_input.touch_long_press_stage >= hold_stage) {
        return;
    }

    s_input.touch_long_press_reported = true;
    s_input.touch_suppress_click_on_release = true;
    s_input.touch_long_press_stage = hold_stage;
    cyd_input_emit_touch_event(CYD_INPUT_TOUCH_ACTION_LONG_PRESS,
                               true,
                               s_input.last_touch_x,
                               s_input.last_touch_y,
                               hold_stage,
                               now);
}

static void cyd_input_handle_boot_button_sample(TickType_t now)
{
    if (!cyd_input_boot_button_enabled()) {
        return;
    }

    bool pressed = cyd_input_boot_button_read_pressed();
    if (pressed == s_input.boot_button_last_sample_pressed) {
        if (s_input.boot_button_stable_count < CONFIG_CYD_INPUT_BINARY_STABLE_COUNT) {
            ++s_input.boot_button_stable_count;
        }
    } else {
        s_input.boot_button_last_sample_pressed = pressed;
        s_input.boot_button_stable_count = 1;
    }

    if (s_input.boot_button_stable_count >= CONFIG_CYD_INPUT_BINARY_STABLE_COUNT &&
        pressed != s_input.boot_button_stable_pressed) {
        s_input.boot_button_stable_pressed = pressed;
        if (pressed) {
            s_input.boot_button_press_tick = now;
            s_input.boot_button_long_press_seconds = 0;
            cyd_input_emit_boot_button_event(CYD_INPUT_BUTTON_ACTION_PRESS, true, 0, now);
        } else {
            uint8_t hold_seconds = s_input.boot_button_long_press_seconds;
            cyd_input_emit_boot_button_event(CYD_INPUT_BUTTON_ACTION_RELEASE, false, hold_seconds, now);
            if (hold_seconds == 0) {
                cyd_input_handle_boot_button_short_release(now);
            } else {
                s_input.boot_button_click_pending = false;
            }
            s_input.boot_button_press_tick = 0;
            s_input.boot_button_long_press_seconds = 0;
        }
        return;
    }

    if (!s_input.boot_button_stable_pressed) {
        return;
    }

    TickType_t elapsed_ticks = now - s_input.boot_button_press_tick;
    TickType_t second_ticks = pdMS_TO_TICKS(1000);
    if (second_ticks == 0) {
        second_ticks = 1;
    }

    uint8_t hold_seconds = (uint8_t)(elapsed_ticks / second_ticks);
    if (hold_seconds > CONFIG_CYD_BOOT_BUTTON_LONG_PRESS_MAX_SECONDS) {
        hold_seconds = CONFIG_CYD_BOOT_BUTTON_LONG_PRESS_MAX_SECONDS;
    }
    if (hold_seconds == 0 || hold_seconds <= s_input.boot_button_long_press_seconds) {
        return;
    }

    s_input.boot_button_long_press_seconds = hold_seconds;
    cyd_input_flush_boot_button_click(now);
    cyd_input_emit_boot_button_event(CYD_INPUT_BUTTON_ACTION_LONG_PRESS, true, hold_seconds, now);
}

static void cyd_input_task(void *arg)
{
    (void)arg;
    const TickType_t poll_ticks = pdMS_TO_TICKS(CONFIG_CYD_TOUCH_POLL_PERIOD_MS) > 0
                                      ? pdMS_TO_TICKS(CONFIG_CYD_TOUCH_POLL_PERIOD_MS)
                                      : 1;
    const TickType_t idle_poll_ticks = pdMS_TO_TICKS(CONFIG_CYD_TOUCH_IDLE_POLL_PERIOD_MS) > 0
                                           ? pdMS_TO_TICKS(CONFIG_CYD_TOUCH_IDLE_POLL_PERIOD_MS)
                                           : 1;
    TickType_t last_wake = xTaskGetTickCount();
    bool boot_low_guard_active = cyd_input_touch_irq_enabled() && cyd_input_touch_irq_level() == 0;
    bool boot_low_logged = false;

    while (true) {
        APP_STACK_MONITOR_CHECK(TAG, "cyd_input", 30000);

        if (boot_low_guard_active) {
            if (cyd_input_touch_irq_level() != 0) {
                boot_low_guard_active = false;
                last_wake = xTaskGetTickCount();
                continue;
            }
            if (!boot_low_logged) {
                ESP_LOGI(TAG, "touch IRQ is held low; deferring touch sampling until release");
                boot_low_logged = true;
            }
            cyd_input_handle_boot_button_sample(xTaskGetTickCount());
            vTaskDelay(idle_poll_ticks);
            last_wake = xTaskGetTickCount();
            continue;
        }

#if CONFIG_CYD_TOUCH_ENABLED
        if (cyd_input_touch_irq_enabled() && !cyd_input_touch_sampling_active()) {
            gpio_intr_enable((gpio_num_t)CONFIG_CYD_TOUCH_PIN_INT);
            if (cyd_input_touch_irq_level() != 0) {
                TickType_t wait_ticks = cyd_input_boot_button_enabled() ? poll_ticks : idle_poll_ticks;
                (void)ulTaskNotifyTake(pdTRUE, wait_ticks);
            }
            gpio_intr_disable((gpio_num_t)CONFIG_CYD_TOUCH_PIN_INT);
            (void)ulTaskNotifyTake(pdTRUE, 0);
            last_wake = xTaskGetTickCount();
        }

        int16_t raw_x = 0;
        int16_t raw_y = 0;
        bool raw_pressed = false;
        esp_err_t err = cyd_display_get_touch_raw(&raw_x, &raw_y, &raw_pressed);
        TickType_t now = xTaskGetTickCount();

        if (err == ESP_OK) {
            if (raw_pressed != s_input.last_touch_sample_pressed) {
                cyd_input_log_touch_raw_change(raw_pressed, raw_x, raw_y, s_input.last_touch_x, s_input.last_touch_y);
            }
            if (raw_pressed == s_input.last_touch_sample_pressed && raw_pressed != s_input.touch_stable_pressed) {
                cyd_input_handle_stable_touch_change(raw_pressed, raw_x, raw_y, now);
            }

            s_input.last_touch_sample_pressed = raw_pressed;

            if (raw_pressed) {
                s_input.last_touch_x = raw_x;
                s_input.last_touch_y = raw_y;
            }

            if (s_input.touch_stable_pressed && raw_pressed) {
                if (xSemaphoreTake(s_input.mutex, portMAX_DELAY) == pdTRUE) {
                    s_input.touch_state.x = raw_x;
                    s_input.touch_state.y = raw_y;
                    s_input.touch_state.tick = now;
                    xSemaphoreGive(s_input.mutex);
                }
            }

            cyd_input_handle_touch_hold(now);
        } else if (err != ESP_ERR_NOT_SUPPORTED) {
            ESP_LOGW(TAG, "touch poll failed: %s", esp_err_to_name(err));
        }
#endif

        cyd_input_handle_boot_button_sample(xTaskGetTickCount());
        cyd_input_finalize_boot_button_click(xTaskGetTickCount());

        vTaskDelayUntil(&last_wake, poll_ticks);
    }
}

static esp_err_t cyd_input_boot_button_configure(void)
{
    if (!cyd_input_boot_button_enabled()) {
        return ESP_OK;
    }

    gpio_config_t boot_button_cfg = {
        .pin_bit_mask = 1ULL << CONFIG_CYD_BOOT_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = CONFIG_CYD_BOOT_BUTTON_ENABLE_INTERNAL_PULLUP ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&boot_button_cfg), TAG, "BOOT button GPIO config failed");

    bool pressed = cyd_input_boot_button_read_pressed();
    s_input.boot_button_stable_pressed = pressed;
    s_input.boot_button_last_sample_pressed = pressed;
    s_input.boot_button_stable_count = CONFIG_CYD_INPUT_BINARY_STABLE_COUNT;
    s_input.boot_button_long_press_seconds = 0;
    s_input.boot_button_press_tick = pressed ? xTaskGetTickCount() : 0;
    s_input.boot_button_release_tick = 0;
    s_input.boot_button_click_pending = false;

    ESP_LOGI(TAG,
             "CYD BOOT button configured: gpio=%d active_low=1 pullup=%d initial_pressed=%d",
             CONFIG_CYD_BOOT_BUTTON_GPIO,
             CONFIG_CYD_BOOT_BUTTON_ENABLE_INTERNAL_PULLUP,
             pressed);
    return ESP_OK;
}

static esp_err_t cyd_input_touch_calibration_load_from_nvs(void)
{
#if CONFIG_CYD_TOUCH_USE_NVS_CALIBRATION
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }

    cyd_touch_calibration_store_t blob = { 0 };
    size_t required_size = sizeof(blob);
    err = nvs_get_blob(nvs_handle, NVS_TOUCH_CAL_KEY, &blob, &required_size);
    nvs_close(nvs_handle);
    if (err != ESP_OK) {
        return err;
    }
    if (required_size != sizeof(blob) || blob.magic != TOUCH_CAL_MAGIC) {
        return ESP_ERR_INVALID_SIZE;
    }

    ESP_RETURN_ON_ERROR(cyd_display_apply_touch_calibration(blob.params, 8), TAG, "apply touch calibration failed");
    s_input.touch_calibration_loaded = true;
    ESP_LOGI(TAG, "loaded touch calibration from NVS");
    return ESP_OK;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

static esp_err_t cyd_input_touch_calibration_save_to_nvs(const uint16_t *params)
{
#if CONFIG_CYD_TOUCH_USE_NVS_CALIBRATION
    cyd_touch_calibration_store_t blob = {
        .magic = TOUCH_CAL_MAGIC,
    };

    memcpy(blob.params, params, sizeof(blob.params));

    nvs_handle_t nvs_handle;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle), TAG, "open NVS failed");
    esp_err_t err = nvs_set_blob(nvs_handle, NVS_TOUCH_CAL_KEY, &blob, sizeof(blob));
    if (err == ESP_OK) {
        err = nvs_commit(nvs_handle);
    }
    nvs_close(nvs_handle);
    ESP_RETURN_ON_ERROR(err, TAG, "save touch calibration failed");
    s_input.touch_calibration_loaded = true;
    ESP_LOGI(TAG, "saved touch calibration to NVS");
    return ESP_OK;
#else
    (void)params;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t cyd_input_init(void)
{
    if (s_input.initialized) {
        return ESP_OK;
    }

    s_input.mutex = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_input.mutex != NULL, ESP_ERR_NO_MEM, TAG, "mutex create failed");

    s_input.event_queue = xQueueCreate(CONFIG_CYD_INPUT_EVENT_QUEUE_LENGTH, sizeof(cyd_input_event_t));
    ESP_RETURN_ON_FALSE(s_input.event_queue != NULL, ESP_ERR_NO_MEM, TAG, "queue create failed");

    ESP_RETURN_ON_ERROR(cyd_input_boot_button_configure(), TAG, "BOOT button init failed");

#if CONFIG_CYD_TOUCH_ENABLED
    if (CONFIG_CYD_TOUCH_PIN_INT >= 0) {
        gpio_config_t int_cfg = {
            .pin_bit_mask = 1ULL << CONFIG_CYD_TOUCH_PIN_INT,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = CONFIG_CYD_TOUCH_USE_IRQ ? GPIO_INTR_NEGEDGE : GPIO_INTR_DISABLE,
        };
        ESP_RETURN_ON_ERROR(gpio_config(&int_cfg), TAG, "touch IRQ GPIO config failed");
    }

    esp_err_t touch_cal_err = cyd_input_touch_calibration_load_from_nvs();
    if (touch_cal_err != ESP_OK &&
        touch_cal_err != ESP_ERR_NVS_NOT_FOUND &&
        touch_cal_err != ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG, "touch calibration load skipped: %s", esp_err_to_name(touch_cal_err));
    }

#if CONFIG_CYD_TOUCH_RUN_CALIBRATION_ON_BOOT
    if (!s_input.touch_calibration_loaded) {
        ESP_LOGI(TAG, "no saved touch calibration, starting calibration flow");
        ESP_RETURN_ON_ERROR(cyd_input_run_touch_calibration(), TAG, "touch calibration failed");
    }
#endif

    if (cyd_input_touch_irq_enabled()) {
        esp_err_t isr_err = gpio_install_isr_service(0);
        if (isr_err != ESP_OK && isr_err != ESP_ERR_INVALID_STATE) {
            return isr_err;
        }
        ESP_RETURN_ON_ERROR(gpio_isr_handler_add((gpio_num_t)CONFIG_CYD_TOUCH_PIN_INT,
                                                 cyd_input_touch_irq_isr,
                                                 NULL),
                            TAG,
                            "touch IRQ handler add failed");
        gpio_intr_disable((gpio_num_t)CONFIG_CYD_TOUCH_PIN_INT);
    }

    ESP_LOGI(TAG,
             "CYD touch input task started: poll=%d ms idle_poll=%d ms queue=%d irq_wake=%d irq_gpio=%d log=%d irq_log=%d",
             CONFIG_CYD_TOUCH_POLL_PERIOD_MS,
             CONFIG_CYD_TOUCH_IDLE_POLL_PERIOD_MS,
             CONFIG_CYD_INPUT_EVENT_QUEUE_LENGTH,
             cyd_input_touch_irq_enabled(),
             CONFIG_CYD_TOUCH_PIN_INT,
             CONFIG_CYD_TOUCH_LOG_EVENTS,
             CONFIG_CYD_TOUCH_LOG_IRQ_LEVEL);
#endif

    if (cyd_input_task_needed()) {
        BaseType_t task_ok = xTaskCreate(cyd_input_task, "cyd_input", 4096, NULL, 4, &s_input.task_handle);
        ESP_RETURN_ON_FALSE(task_ok == pdPASS, ESP_ERR_NO_MEM, TAG, "input task create failed");
    }

    s_input.initialized = true;
    return ESP_OK;
}

esp_err_t cyd_input_get_touch_state(cyd_input_touch_state_t *state)
{
    ESP_RETURN_ON_ERROR(cyd_input_check_ready(), TAG, "input unavailable");
    ESP_RETURN_ON_FALSE(state != NULL, ESP_ERR_INVALID_ARG, TAG, "state required");

    if (xSemaphoreTake(s_input.mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    *state = s_input.touch_state;
    xSemaphoreGive(s_input.mutex);
    return ESP_OK;
}

esp_err_t cyd_input_get_touch_irq_level(int *level)
{
    ESP_RETURN_ON_ERROR(cyd_input_check_ready(), TAG, "input unavailable");
    ESP_RETURN_ON_FALSE(level != NULL, ESP_ERR_INVALID_ARG, TAG, "level required");
    ESP_RETURN_ON_FALSE(CONFIG_CYD_TOUCH_PIN_INT >= 0, ESP_ERR_NOT_SUPPORTED, TAG, "touch IRQ GPIO not configured");

    *level = cyd_input_touch_irq_level();
    return ESP_OK;
}

esp_err_t cyd_input_get_mode_button_touch(size_t button_count, size_t *button_index, bool *pressed)
{
    (void)button_count;
    cyd_input_touch_state_t touch_state = { 0 };
    ESP_RETURN_ON_ERROR(cyd_input_get_touch_state(&touch_state), TAG, "touch unavailable");

    if (button_index != NULL) {
        *button_index = 0;
    }
    if (pressed != NULL) {
        *pressed = false;
    }

    if (!touch_state.pressed) {
        return ESP_OK;
    }

    size_t hit_index = 0;
    if (cyd_display_hit_test_mode_button(touch_state.x, touch_state.y, &hit_index)) {
        if (button_index != NULL) {
            *button_index = hit_index;
        }
        if (pressed != NULL) {
            *pressed = true;
        }
    }

    return ESP_OK;
}

esp_err_t cyd_input_read_event(cyd_input_event_t *event, TickType_t wait_ticks)
{
    ESP_RETURN_ON_ERROR(cyd_input_check_ready(), TAG, "input unavailable");
    ESP_RETURN_ON_FALSE(event != NULL, ESP_ERR_INVALID_ARG, TAG, "event required");

    if (xQueueReceive(s_input.event_queue, event, wait_ticks) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

esp_err_t cyd_input_discard_pending_events(void)
{
    cyd_input_event_t event = { 0 };

    ESP_RETURN_ON_ERROR(cyd_input_check_ready(), TAG, "input unavailable");

    while (xQueueReceive(s_input.event_queue, &event, 0) == pdTRUE) {
    }

    return ESP_OK;
}

esp_err_t cyd_input_run_touch_calibration(void)
{
#if CONFIG_CYD_TOUCH_ENABLED
    uint16_t params[8] = { 0 };
    ESP_RETURN_ON_ERROR(cyd_display_calibrate_touch(params, 8), TAG, "touch calibration failed");
    ESP_RETURN_ON_ERROR(cyd_display_apply_touch_calibration(params, 8), TAG, "touch calibration apply failed");
    return cyd_input_touch_calibration_save_to_nvs(params);
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t cyd_input_clear_touch_calibration(void)
{
#if CONFIG_CYD_TOUCH_USE_NVS_CALIBRATION
    nvs_handle_t nvs_handle;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle), TAG, "open NVS failed");
    esp_err_t err = nvs_erase_key(nvs_handle, NVS_TOUCH_CAL_KEY);
    if (err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND) {
        err = nvs_commit(nvs_handle);
    }
    nvs_close(nvs_handle);
    ESP_RETURN_ON_ERROR(err, TAG, "clear touch calibration failed");
    s_input.touch_calibration_loaded = false;
    ESP_LOGI(TAG, "cleared touch calibration from NVS");
    return ESP_OK;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}
