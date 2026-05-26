#include <stdbool.h>
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "app_shell.h"
#include "cyd_alarm.h"
#include "cyd_display.h"
#include "cyd_input.h"
#include "app_scheduler.h"
#include "cyd_speaker.h"
#include "cyd_status_led.h"
#include "time_tick.h"
#include "system_boot.h"
#if CONFIG_ESP32_WIFI_STA_ENABLED
#include "radio_manager.h"
#include "time_sync.h"
#include "wifi_manager.h"
#endif

#define TAG "system_boot"
#define SYSTEM_BOOT_TOUCH_IRQ_SETUP_EARLY_WINDOW_MS 100
#define SYSTEM_BOOT_TOUCH_IRQ_SETUP_POST_DISPLAY_WINDOW_MS 1000
#define SYSTEM_BOOT_TOUCH_IRQ_SETUP_POLL_INTERVAL_MS 10
#define SYSTEM_BOOT_TOUCH_IRQ_SETUP_RELEASE_WAIT_MS 10000

static void system_boot_log_dev_banner(void)
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
}

static bool system_boot_touch_irq_setup_requested(const char *phase, uint32_t detect_window_ms)
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
        ESP_LOGW(TAG, "touch IRQ GPIO config failed: %s", esp_err_to_name(err));
        return false;
    }

    TickType_t start = xTaskGetTickCount();
    TickType_t detect_window_ticks = pdMS_TO_TICKS(detect_window_ms);
    if (detect_window_ticks == 0) {
        detect_window_ticks = 1;
    }

    while (xTaskGetTickCount() - start <= detect_window_ticks) {
        if (gpio_get_level(irq_gpio) != 0) {
            vTaskDelay(pdMS_TO_TICKS(SYSTEM_BOOT_TOUCH_IRQ_SETUP_POLL_INTERVAL_MS));
            continue;
        }

        TickType_t hold_start = xTaskGetTickCount();
        TickType_t release_wait_ticks = pdMS_TO_TICKS(SYSTEM_BOOT_TOUCH_IRQ_SETUP_RELEASE_WAIT_MS);
        ESP_LOGI(TAG,
                 "%s Wi-Fi setup requested by touch IRQ low: gpio=%d window=%ums",
                 phase != NULL ? phase : "boot",
                 CONFIG_CYD_TOUCH_PIN_INT,
                 (unsigned)detect_window_ms);

        while (gpio_get_level(irq_gpio) == 0 &&
               xTaskGetTickCount() - hold_start < release_wait_ticks) {
            vTaskDelay(pdMS_TO_TICKS(SYSTEM_BOOT_TOUCH_IRQ_SETUP_POLL_INTERVAL_MS));
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

static esp_err_t system_boot_init_nvs(void)
{
    esp_err_t ret = nvs_flash_init();

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    return ret;
}

static esp_err_t system_boot_run_touch_calibration_if_needed(void)
{
#if CONFIG_CYD_TOUCH_ENABLED
    if (cyd_input_has_saved_touch_calibration()) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "no saved touch calibration, running calibration before app shell start");
    ESP_RETURN_ON_ERROR(cyd_input_run_touch_calibration(), TAG, "touch calibration failed");
    return cyd_input_discard_pending_events();
#else
    return ESP_OK;
#endif
}

esp_err_t system_boot_start(const app_shell_app_t *home_app)
{
    ESP_RETURN_ON_FALSE(home_app != NULL, ESP_ERR_INVALID_ARG, TAG, "home app required");

    system_boot_log_dev_banner();

    bool setup_requested_on_boot = system_boot_touch_irq_setup_requested(
        "early",
        SYSTEM_BOOT_TOUCH_IRQ_SETUP_EARLY_WINDOW_MS
    );

    ESP_RETURN_ON_ERROR(system_boot_init_nvs(), TAG, "NVS init failed");
    ESP_RETURN_ON_ERROR(cyd_status_led_init(), TAG, "status LED init failed");
    ESP_RETURN_ON_ERROR(cyd_speaker_init(), TAG, "speaker init failed");
    ESP_RETURN_ON_ERROR(time_tick_start(), TAG, "time tick start failed");
    ESP_RETURN_ON_ERROR(app_scheduler_init(), TAG, "scheduler init failed");
    ESP_RETURN_ON_ERROR(cyd_alarm_init(), TAG, "alarm init failed");
    ESP_RETURN_ON_ERROR(cyd_display_init(), TAG, "display init failed");

    if (!setup_requested_on_boot) {
        setup_requested_on_boot = system_boot_touch_irq_setup_requested(
            "post-display",
            SYSTEM_BOOT_TOUCH_IRQ_SETUP_POST_DISPLAY_WINDOW_MS
        );
    }

    ESP_RETURN_ON_ERROR(cyd_input_init(), TAG, "input init failed");
    ESP_RETURN_ON_ERROR(system_boot_run_touch_calibration_if_needed(),
                        TAG,
                        "initial touch calibration failed");

#if CONFIG_ESP32_WIFI_STA_AUTO_START
    if (setup_requested_on_boot) {
        wifi_manager_request_setup_on_start();
    }
    ESP_RETURN_ON_ERROR(wifi_manager_start(), TAG, "Wi-Fi manager start failed");
    ESP_RETURN_ON_ERROR(radio_manager_start(), TAG, "radio manager start failed");
    ESP_RETURN_ON_ERROR(time_sync_start(), TAG, "time sync start failed");
#endif

    return app_shell_start(home_app);
}
