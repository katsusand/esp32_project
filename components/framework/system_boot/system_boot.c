#include <stdbool.h>
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_health.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "cyd_display.h"
#include "cyd_input.h"
#include "cyd_speaker.h"
#include "cyd_status_led.h"
#include "system_boot.h"

#define TAG "system_boot"
#define SYSTEM_BOOT_TOUCH_IRQ_SETUP_EARLY_WINDOW_MS 100
/*
 * Window for the SPI-based check that runs after cyd_input_init(). It replaces
 * the old post-display raw-GPIO window rather than adding to it, so boot time
 * is unchanged for a device that is not being held.
 */
#define SYSTEM_BOOT_TOUCH_SETUP_POST_INPUT_WINDOW_MS 1200
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
    unsigned int samples = 0;
    unsigned int low_samples = 0;

    if (detect_window_ticks == 0) {
        detect_window_ticks = 1;
    }

    while (xTaskGetTickCount() - start <= detect_window_ticks) {
        samples++;
        if (gpio_get_level(irq_gpio) != 0) {
            vTaskDelay(pdMS_TO_TICKS(SYSTEM_BOOT_TOUCH_IRQ_SETUP_POLL_INTERVAL_MS));
            continue;
        }
        low_samples++;

        TickType_t hold_start = xTaskGetTickCount();
        TickType_t release_wait_ticks = pdMS_TO_TICKS(SYSTEM_BOOT_TOUCH_IRQ_SETUP_RELEASE_WAIT_MS);
        ESP_LOGI(TAG,
                 "%s shortcut detected: touch IRQ gpio=%d low after %u samples",
                 phase != NULL ? phase : "boot",
                 CONFIG_CYD_TOUCH_PIN_INT,
                 samples);

        while (gpio_get_level(irq_gpio) == 0 &&
               xTaskGetTickCount() - hold_start < release_wait_ticks) {
            vTaskDelay(pdMS_TO_TICKS(SYSTEM_BOOT_TOUCH_IRQ_SETUP_POLL_INTERVAL_MS));
        }
        return true;
    }

    /*
     * Report the miss. A silent false is indistinguishable from "the window
     * never ran", and on this board the touch IRQ is the input-only pad GPIO36,
     * which has no internal pull-up: if the panel's PENIRQ pull-up is absent or
     * the controller is not asserting, the line simply never reads low and the
     * shortcut can never fire. The sample count makes that visible.
     */
    ESP_LOGI(TAG,
             "%s shortcut not detected: gpio=%d stayed high for %u/%u samples over %ums",
             phase != NULL ? phase : "boot",
             CONFIG_CYD_TOUCH_PIN_INT,
             samples - low_samples,
             samples,
             (unsigned)detect_window_ms);
    return false;
#else
    (void)phase;
    (void)detect_window_ms;
    return false;
#endif
}

/*
 * Authoritative shortcut check, run after cyd_input_init().
 *
 * The raw PENIRQ line is not reliable at boot: the XPT2046 only drives it low
 * while it is in power-down between conversions, and after a warm reset it can
 * come up in a state where it never asserts until the host has actually talked
 * to it over SPI. Both raw-GPIO windows above run before the controller has
 * been touched by any driver, so on those boots the shortcut can never fire.
 *
 * cyd_input's task reads the panel over SPI, so its touch state reflects the
 * controller itself rather than a side-band line that may be stuck high.
 *
 * English supplement: this is why the detection lives after input init. Moving
 * it back before cyd_input_init() reintroduces the boot where holding the panel
 * does nothing.
 */
static bool system_boot_touch_state_setup_requested(uint32_t detect_window_ms)
{
#if CONFIG_CYD_TOUCH_ENABLED
    TickType_t start = xTaskGetTickCount();
    TickType_t detect_window_ticks = pdMS_TO_TICKS(detect_window_ms);
    unsigned int samples = 0;

    if (detect_window_ticks == 0) {
        detect_window_ticks = 1;
    }

    while (xTaskGetTickCount() - start <= detect_window_ticks) {
        cyd_input_touch_state_t state = { 0 };

        samples++;
        if (cyd_input_get_touch_state(&state) == ESP_OK && state.pressed) {
            ESP_LOGI(TAG,
                     "post-input shortcut detected: panel pressed at (%d,%d) after %u samples",
                     (int)state.x,
                     (int)state.y,
                     samples);

            /* Wait for release so the touch does not leak into the first app. */
            TickType_t hold_start = xTaskGetTickCount();
            TickType_t release_wait_ticks = pdMS_TO_TICKS(SYSTEM_BOOT_TOUCH_IRQ_SETUP_RELEASE_WAIT_MS);
            while (xTaskGetTickCount() - hold_start < release_wait_ticks) {
                if (cyd_input_get_touch_state(&state) != ESP_OK || !state.pressed) {
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(SYSTEM_BOOT_TOUCH_IRQ_SETUP_POLL_INTERVAL_MS));
            }
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(SYSTEM_BOOT_TOUCH_IRQ_SETUP_POLL_INTERVAL_MS));
    }

    ESP_LOGI(TAG,
             "post-input shortcut not detected: panel not pressed in %u samples over %ums",
             samples,
             (unsigned)detect_window_ms);
    return false;
#else
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

esp_err_t system_boot_start(system_boot_result_t *result)
{
    system_boot_log_dev_banner();

    bool setup_requested_on_boot = system_boot_touch_irq_setup_requested(
        "early",
        SYSTEM_BOOT_TOUCH_IRQ_SETUP_EARLY_WINDOW_MS
    );

    nvs_health_reset();
    ESP_RETURN_ON_ERROR(system_boot_init_nvs(), TAG, "NVS init failed");
    ESP_RETURN_ON_ERROR(cyd_status_led_init(), TAG, "status LED init failed");
    ESP_RETURN_ON_ERROR(cyd_speaker_init(), TAG, "speaker init failed");
    ESP_RETURN_ON_ERROR(cyd_display_init(), TAG, "display init failed");

    ESP_RETURN_ON_ERROR(cyd_input_init(), TAG, "input init failed");

    /*
     * Detection happens here, not before input init: the raw PENIRQ windows
     * above run while the XPT2046 has never been addressed, and on a warm reset
     * it can stay high throughout. Reading the panel through cyd_input asks the
     * controller over SPI instead, which is what makes holding the screen at
     * boot actually work.
     */
    if (!setup_requested_on_boot) {
        setup_requested_on_boot = system_boot_touch_state_setup_requested(
            SYSTEM_BOOT_TOUCH_SETUP_POST_INPUT_WINDOW_MS
        );
    }

    /*
     * Not fatal. A bad tap set is already asked for again inside the
     * calibration; what can still fail here is saving it (NVS), and failing the
     * boot turned that into a reboot loop. The session keeps the calibration it
     * just applied, and without a saved one the clock app routes to the
     * calibration app again at its first enter.
     */
    esp_err_t calibration_err = system_boot_run_touch_calibration_if_needed();
    if (calibration_err != ESP_OK) {
        ESP_LOGW(TAG, "initial touch calibration failed: %s; continuing", esp_err_to_name(calibration_err));
    }
    if (setup_requested_on_boot) {
        /* The hold must not arrive at the first app as a stray tap. */
        (void)cyd_input_discard_pending_events();
    }

    if (result != NULL) {
        result->setup_shortcut_requested = setup_requested_on_boot;
    }
    return ESP_OK;
}
