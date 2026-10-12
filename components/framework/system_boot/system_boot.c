#include <stdbool.h>
#include <stdlib.h>
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
#include "cyd_ui.h"
#include "cyd_status_led.h"
#include "system_boot.h"
#include "system_boot_view.h"
#include "xpt2046_softspi.h"

#define TAG "system_boot"
#define SYSTEM_BOOT_TOUCH_SETUP_EARLY_WINDOW_MS 100
/*
 * Window for the check that runs after cyd_input_init(). It replaces the old
 * post-display raw-GPIO window rather than adding to it, so boot time is
 * unchanged for a device that is not being held.
 */
#define SYSTEM_BOOT_TOUCH_SETUP_POST_INPUT_WINDOW_MS 1200
#define SYSTEM_BOOT_TOUCH_SETUP_POLL_INTERVAL_MS 10
#define SYSTEM_BOOT_TOUCH_SETUP_RELEASE_WAIT_MS 10000
/*
 * Consecutive "not held" polls that count as a release (50ms). One poll is not
 * enough: PENIRQ is not driven while the controller converts, so a single high
 * read can happen with the finger still down.
 */
#define SYSTEM_BOOT_TOUCH_SETUP_RELEASE_STABLE_POLLS 5

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

/*
 * First shortcut check, before the display is initialised.
 *
 * It initialises the touch transport first. xpt2046_softspi_init() sends the
 * command that re-enables PENIRQ, which the controller may have kept disabled
 * across an ESP32-only reset (flash, reset button, monitor). Before that fix
 * this window read a raw PENIRQ that could be stuck high, and the shortcut
 * never fired on those boots.
 *
 * Each poll is xpt2046_softspi_get_raw(): a PENIRQ check that costs nothing
 * while the panel is not pressed, confirmed by an SPI read when it is, so a
 * PENIRQ line stuck low does not count as a held panel.
 *
 * Detection only. It does not wait for the release: the display is not up
 * yet, and waiting here left the screen dark for as long as the finger stayed
 * down (up to 10 seconds). system_boot_start() shows the prompt first.
 *
 * English supplement: cyd_input's task does not exist yet, so reading the
 * controller directly is safe here. After cyd_input_init() only cyd_input may
 * talk to it; two tasks on the software SPI would corrupt each other's frames.
 */
static bool system_boot_touch_early_setup_requested(uint32_t detect_window_ms)
{
#if CONFIG_CYD_TOUCH_ENABLED
    esp_err_t err = xpt2046_softspi_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "touch transport init failed: %s; skipping early shortcut check", esp_err_to_name(err));
        return false;
    }

    TickType_t start = xTaskGetTickCount();
    TickType_t detect_window_ticks = pdMS_TO_TICKS(detect_window_ms);
    unsigned int samples = 0;
    unsigned int irq_low_samples = 0;

    if (detect_window_ticks == 0) {
        detect_window_ticks = 1;
    }

    while (xTaskGetTickCount() - start <= detect_window_ticks) {
        int16_t x = 0;
        int16_t y = 0;
        bool touched = false;

        samples++;
        if (CONFIG_CYD_TOUCH_PIN_INT >= 0 && gpio_get_level((gpio_num_t)CONFIG_CYD_TOUCH_PIN_INT) == 0) {
            irq_low_samples++;
        }
        if (xpt2046_softspi_get_raw(&x, &y, &touched) == ESP_OK && touched) {
            ESP_LOGI(TAG,
                     "early shortcut detected: panel pressed at raw (%d,%d) after %u samples",
                     (int)x,
                     (int)y,
                     samples);
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(SYSTEM_BOOT_TOUCH_SETUP_POLL_INTERVAL_MS));
    }

    /*
     * Report the miss with the PENIRQ count. On this board PENIRQ is the
     * input-only pad GPIO36, which has no internal pull-up; "low with no touch"
     * points at the line, "never low" at the controller or the panel.
     */
    ESP_LOGI(TAG,
             "early shortcut not detected: panel not pressed in %u samples over %ums (touch IRQ gpio=%d low in %u)",
             samples,
             (unsigned)detect_window_ms,
             CONFIG_CYD_TOUCH_PIN_INT,
             irq_low_samples);
    return false;
#else
    (void)detect_window_ms;
    return false;
#endif
}

/*
 * Second shortcut check, after cyd_input_init().
 *
 * It catches a press that starts after the early window, typically a user who
 * puts a finger down a moment after powering on. cyd_input's task reads the
 * panel over SPI, so its touch state reflects the controller itself.
 *
 * Detection only, like the early check: the prompt goes up before the release
 * wait.
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
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(SYSTEM_BOOT_TOUCH_SETUP_POLL_INTERVAL_MS));
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

/*
 * Tells the user that settings will open and to lift the finger. Called as
 * soon as the shortcut is known and the display is up, before the release
 * wait. The screen stays until the first app draws its own.
 *
 * Not fatal: without the prompt the shortcut still works, the screen is only
 * less helpful.
 */
static void system_boot_show_shortcut_prompt(void)
{
    /* About 3KB, needed once: submit copies the screen into the display queue. */
    cyd_display_screen_t *screen = calloc(1, sizeof(*screen));
    if (screen == NULL) {
        ESP_LOGW(TAG, "no memory for the shortcut prompt");
        return;
    }

    system_boot_view_build_shortcut_prompt(screen);
    esp_err_t err = cyd_ui_submit(screen);
    free(screen);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "showing the shortcut prompt failed: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "shortcut prompt shown; waiting for release");
}

/*
 * Waits, up to SYSTEM_BOOT_TOUCH_SETUP_RELEASE_WAIT_MS, until the finger is off
 * the panel, so the hold does not arrive at the first app as a tap. Runs after
 * cyd_input_init() and goes through cyd_input only.
 *
 * Held means cyd_input reports a press or PENIRQ is low. PENIRQ is included
 * because right after cyd_input_init() the task may not have settled on
 * "pressed" yet, and an early-detected hold must not read as released then.
 *
 * Queued events are dropped on every poll. The input task keeps queueing
 * press and long-press events while the finger is down, and nobody reads them
 * yet; they used to overflow the queue (input event queue full) during a long
 * hold.
 */
static void system_boot_wait_for_touch_release(void)
{
#if CONFIG_CYD_TOUCH_ENABLED
    TickType_t start = xTaskGetTickCount();
    TickType_t release_wait_ticks = pdMS_TO_TICKS(SYSTEM_BOOT_TOUCH_SETUP_RELEASE_WAIT_MS);
    unsigned int released_polls = 0;

    while (xTaskGetTickCount() - start < release_wait_ticks) {
        cyd_input_touch_state_t state = { 0 };
        int irq_level = 1;
        bool held = (cyd_input_get_touch_state(&state) == ESP_OK && state.pressed) ||
                    (cyd_input_get_touch_irq_level(&irq_level) == ESP_OK && irq_level == 0);

        (void)cyd_input_discard_pending_events();
        if (held) {
            released_polls = 0;
        } else if (++released_polls >= SYSTEM_BOOT_TOUCH_SETUP_RELEASE_STABLE_POLLS) {
            ESP_LOGI(TAG,
                     "shortcut touch released after %ums",
                     (unsigned)((xTaskGetTickCount() - start) * portTICK_PERIOD_MS));
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(SYSTEM_BOOT_TOUCH_SETUP_POLL_INTERVAL_MS));
    }

    ESP_LOGW(TAG,
             "shortcut touch still held after %ums; continuing",
             (unsigned)SYSTEM_BOOT_TOUCH_SETUP_RELEASE_WAIT_MS);
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

    bool setup_requested_on_boot = system_boot_touch_early_setup_requested(
        SYSTEM_BOOT_TOUCH_SETUP_EARLY_WINDOW_MS
    );

    nvs_health_reset();
    ESP_RETURN_ON_ERROR(system_boot_init_nvs(), TAG, "NVS init failed");
    ESP_RETURN_ON_ERROR(cyd_status_led_init(), TAG, "status LED init failed");
    ESP_RETURN_ON_ERROR(cyd_speaker_init(), TAG, "speaker init failed");
    ESP_RETURN_ON_ERROR(cyd_display_init(), TAG, "display init failed");
    /* Before the first themed screen. Not fatal: the default theme is usable. */
    esp_err_t theme_err = cyd_ui_theme_load();
    if (theme_err != ESP_OK) {
        ESP_LOGW(TAG, "loading the colour theme failed: %s; using the default", esp_err_to_name(theme_err));
    }

    /*
     * The first screen the user sees on a held boot. It goes up before
     * cyd_input_init() and the release wait, so the finger comes off as soon as
     * the user can read why the device is waiting.
     */
    if (setup_requested_on_boot) {
        system_boot_show_shortcut_prompt();
    }

    ESP_RETURN_ON_ERROR(cyd_input_init(), TAG, "input init failed");

    if (!setup_requested_on_boot) {
        setup_requested_on_boot = system_boot_touch_state_setup_requested(
            SYSTEM_BOOT_TOUCH_SETUP_POST_INPUT_WINDOW_MS
        );
        if (setup_requested_on_boot) {
            system_boot_show_shortcut_prompt();
        }
    }
    if (setup_requested_on_boot) {
        system_boot_wait_for_touch_release();
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
