#include <stdbool.h>
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "app_shell.h"
#include "cyd_clock_app.h"
#include "cyd_display.h"
#include "cyd_input.h"
#include "cyd_speaker.h"
#include "cyd_status_led.h"
#if CONFIG_ESP32_WIFI_STA_ENABLED
#include "time_sync.h"
#include "wifi_manager.h"
#endif

#define TAG "main"
#define MAIN_TOUCH_IRQ_SETUP_EARLY_WINDOW_MS 100
#define MAIN_TOUCH_IRQ_SETUP_POST_DISPLAY_WINDOW_MS 1000
#define MAIN_TOUCH_IRQ_SETUP_POLL_INTERVAL_MS 10
#define MAIN_TOUCH_IRQ_SETUP_RELEASE_WAIT_MS 10000

static bool main_touch_irq_setup_requested(const char *phase, uint32_t detect_window_ms)
{
#if CONFIG_CYD_TOUCH_ENABLED && CONFIG_CYD_TOUCH_PIN_INT >= 0
    gpio_num_t irq_gpio = (gpio_num_t)CONFIG_CYD_TOUCH_PIN_INT;
    gpio_config_t int_cfg = {
        .pin_bit_mask = 1ULL << CONFIG_CYD_TOUCH_PIN_INT,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t err = gpio_config(&int_cfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "early touch IRQ GPIO config failed: %s", esp_err_to_name(err));
        return false;
    }

    TickType_t start = xTaskGetTickCount();
    TickType_t detect_window_ticks = pdMS_TO_TICKS(detect_window_ms);
    if (detect_window_ticks == 0) {
        detect_window_ticks = 1;
    }

    while (xTaskGetTickCount() - start <= detect_window_ticks) {
        if (gpio_get_level(irq_gpio) != 0) {
            vTaskDelay(pdMS_TO_TICKS(MAIN_TOUCH_IRQ_SETUP_POLL_INTERVAL_MS));
            continue;
        }

        TickType_t hold_start = xTaskGetTickCount();
        TickType_t release_wait_ticks = pdMS_TO_TICKS(MAIN_TOUCH_IRQ_SETUP_RELEASE_WAIT_MS);
        ESP_LOGI(TAG,
                 "%s Wi-Fi setup requested by touch IRQ low: gpio=%d window=%ums",
                 phase != NULL ? phase : "boot",
                 CONFIG_CYD_TOUCH_PIN_INT,
                 (unsigned)detect_window_ms);

        while (gpio_get_level(irq_gpio) == 0 &&
               xTaskGetTickCount() - hold_start < release_wait_ticks) {
            vTaskDelay(pdMS_TO_TICKS(MAIN_TOUCH_IRQ_SETUP_POLL_INTERVAL_MS));
        }
        return true;
    }

    return false;
#else
    (void)phase;
    (void)detect_window_ms;
    return false;
#endif
}

void app_main(void)
{
#if defined(APP_DEV) && APP_DEV
    ESP_LOGW(TAG, " -------------------------------------------------");
    ESP_LOGW(TAG, " | DDDD   EEEEE V   V    M   M  OOO  DDDD  EEEEE |");
    ESP_LOGW(TAG, " | D   D  E     V   V    MM MM O   O D   D E     |");
    ESP_LOGW(TAG, " | D   D  EEEEE  V V     M M M O   O D   D EEEEE |");
    ESP_LOGW(TAG, " | D   D  E      V V     M   M O   O D   D E     |");
    ESP_LOGW(TAG, " | DDDD   EEEEE   V      M   M  OOO  DDDD  EEEEE |");
    ESP_LOGW(TAG, " -------------------------------------------------");
#endif

    bool setup_requested_on_boot = main_touch_irq_setup_requested("early", MAIN_TOUCH_IRQ_SETUP_EARLY_WINDOW_MS);
    esp_err_t ret = nvs_flash_init();

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(cyd_status_led_init());
    ESP_ERROR_CHECK(cyd_speaker_init());
    ESP_ERROR_CHECK(cyd_display_init());
    if (!setup_requested_on_boot) {
        setup_requested_on_boot = main_touch_irq_setup_requested("post-display",
                                                                 MAIN_TOUCH_IRQ_SETUP_POST_DISPLAY_WINDOW_MS);
    }
    ESP_ERROR_CHECK(cyd_input_init());

#if CONFIG_ESP32_WIFI_STA_AUTO_START
    if (setup_requested_on_boot) {
        wifi_manager_request_setup_on_start();
    }
    ESP_ERROR_CHECK(wifi_manager_start());
    ESP_ERROR_CHECK(time_sync_start());
#endif
    ESP_ERROR_CHECK(app_shell_start(cyd_clock_app_get_app()));
}
