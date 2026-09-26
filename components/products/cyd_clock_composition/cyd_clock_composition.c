#include "esp_check.h"
#include "esp_log.h"
#include "nvs_health.h"
#include "sdkconfig.h"
#include "app_launcher.h"
#include "app_registry.h"
#include "app_scheduler.h"
#include "app_shell.h"
#include "cyd_clock_app.h"
#include "cyd_clock_composition.h"
#include "cyd_display.h"
#include "cyd_system_apps.h"
#include "error_log_store.h"
#include "sd_card_storage.h"
#include "system_boot.h"
#include "time_tick.h"

#ifndef APP_WIFI_STA_ENABLED
#define APP_WIFI_STA_ENABLED 1
#endif

#if APP_WIFI_STA_ENABLED
#include "radio_manager.h"
#include "status_indicator.h"
#include "time_sync.h"
#include "wifi_connection.h"
#include "wifi_profile_store.h"
#include "wifi_rssi_history.h"
#endif

#define TAG "cyd_clock_composition"

/*
 * Home app selection. Kconfig picks between the shipped options; any other app
 * can be used by replacing this macro.
 */
#if CONFIG_CYD_CLOCK_COMPOSITION_HOME_LAUNCHER
#define CYD_CLOCK_COMPOSITION_HOME_APP() app_launcher_get_app()
#else
#define CYD_CLOCK_COMPOSITION_HOME_APP() cyd_clock_app_get_app()
#endif

static bool cyd_clock_composition_home_return_allowed(void *ctx)
{
    (void)ctx;

#if APP_WIFI_STA_ENABLED && CONFIG_ESP32_WIFI_STA_AUTO_START
    wifi_connection_state_t wifi_state = WIFI_CONNECTION_STATE_STOPPED;
    if (wifi_connection_get_state(&wifi_state) == ESP_OK &&
        (wifi_state == WIFI_CONNECTION_STATE_SETUP_REQUIRED ||
         wifi_state == WIFI_CONNECTION_STATE_SETUP_RUNNING)) {
        return false;
    }
#endif

    return true;
}

/*
 * Optional services must not brick the boot.
 *
 * English contract: the clock UI is fully usable without Wi-Fi, NTP or the
 * status LED. Propagating those startup failures would reach app_main's
 * ESP_ERROR_CHECK and put a deployed device into a reboot loop, so they are
 * recorded and skipped instead. Only failures that leave the device unable to
 * render anything stay fatal.
 */
static void cyd_clock_composition_start_optional(const char *what, esp_err_t err)
{
    if (err == ESP_OK) {
        return;
    }
    (void)error_log_store_append_esp_err(TAG, what, err);
    ESP_LOGW(TAG, "%s: %s; continuing in degraded mode", what, esp_err_to_name(err));
}

static void cyd_clock_composition_preflight_nvs_health(void)
{
    (void)cyd_display_get_brightness();
    (void)app_shell_get_idle_return_timeout_seconds();

#if APP_WIFI_STA_ENABLED
    size_t profile_count = 0;
    (void)wifi_profile_store_load_entries(NULL, 0, &profile_count);
    (void)radio_manager_get_idle_timeout_seconds();
    (void)time_sync_get_interval_minutes();
    (void)time_sync_get_timezone();
#endif
}

/*
 * Which apps exist on this device, and in what order the launcher lists them.
 *
 * English contract: registration lives here, not inside an app's enter(). If an
 * app registered itself while running, swapping the home app would silently
 * drop entries that the new home never reaches.
 */
static void cyd_clock_composition_register_apps(void)
{
    /* Registering the clock also brings its settings screen and its alarms; a
       product that omits the clock gets none of them. Runs after
       app_scheduler_init(), which the alarms need. */
    cyd_clock_composition_start_optional("register clock app failed",
                                         cyd_clock_app_register());
}

esp_err_t cyd_clock_composition_start(void)
{
    system_boot_result_t boot_result = { 0 };
    /*
     * The home app is a product choice, not a framework constraint. Swap this
     * for app_launcher_get_app() to boot into the generic app list, or for any
     * other app_shell app.
     */
    const app_shell_app_t *initial_app = CYD_CLOCK_COMPOSITION_HOME_APP();
    esp_err_t err = ESP_OK;

    err = system_boot_start(&boot_result);
    if (err != ESP_OK) {
        (void)error_log_store_append_esp_err(TAG, "system boot failed", err);
        ESP_RETURN_ON_ERROR(err, TAG, "system boot failed");
    }
    cyd_clock_composition_start_optional("sd card init failed", sd_card_storage_init());
    err = time_tick_start();
    if (err != ESP_OK) {
        (void)error_log_store_append_esp_err(TAG, "time tick start failed", err);
        ESP_RETURN_ON_ERROR(err, TAG, "time tick start failed");
    }
    /*
     * The scheduler is a shared service; what runs on it belongs to the apps
     * (the clock installs its alarms in cyd_clock_app_register()). Optional in
     * the same sense as Wi-Fi: the clock face works without it. It used to be
     * fatal, so an NVS write failure became a reboot loop that never reached
     * the Initialize NVS screen meant to recover from exactly that.
     */
    cyd_clock_composition_start_optional("scheduler init failed", app_scheduler_init());

#if APP_WIFI_STA_ENABLED && CONFIG_ESP32_WIFI_STA_AUTO_START
    cyd_clock_composition_start_optional("status indicator start failed", status_indicator_start());
    cyd_clock_composition_start_optional("Wi-Fi connection start failed", wifi_connection_start());
    cyd_clock_composition_start_optional("radio manager start failed", radio_manager_start());
    cyd_clock_composition_start_optional("time sync start failed", time_sync_start());
    cyd_clock_composition_start_optional("wifi rssi history start failed", wifi_rssi_history_start());
#endif

    cyd_clock_composition_preflight_nvs_health();
    if (nvs_health_requires_initialize()) {
        ESP_LOGW(TAG, "invalid NVS data detected; forcing Initialize NVS flow");
        system_settings_open_clear_nvs_confirm();
        initial_app = system_settings_app_get_app();
    }

    /*
     * Holding the touch panel through boot opens settings.
     *
     * It deliberately does NOT arm the Wi-Fi setup wizard any more. Settings is
     * one predictable destination that also reaches Wi-Fi setup, touch
     * calibration and Initialize NVS, rather than a shortcut whose meaning
     * depends on whether a Wi-Fi profile happens to exist. It also works in a
     * build without Wi-Fi: arming the wizard sat inside the Wi-Fi build feature
     * guard, so the shortcut did nothing at all in those builds.
     *
     * When NVS is invalid the block above has already selected settings and
     * armed the Initialize NVS confirmation, so that view still wins here.
     */
    if (boot_result.setup_shortcut_requested) {
        ESP_LOGI(TAG, "boot touch shortcut held: opening settings");
        initial_app = system_settings_app_get_app();
    }

    cyd_clock_composition_register_apps();
    app_shell_set_home_return_allowed_callback(cyd_clock_composition_home_return_allowed, NULL);
    return app_shell_start(initial_app);
}
