#include <string.h>
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
#include "cyd_speaker.h"
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
#define CYD_CLOCK_ALARM_OWNER "clock"
#define CYD_CLOCK_ALARM1_TAG "alarm1"
#define CYD_CLOCK_ALARM2_TAG "alarm2"
#define CYD_CLOCK_ALARM_WEEKDAY_ALL 0x7fU

static app_scheduler_config_t cyd_clock_composition_default_alarm_config(const char *tag)
{
    app_scheduler_config_t config = { 0 };

    strlcpy(config.owner, CYD_CLOCK_ALARM_OWNER, sizeof(config.owner));
    strlcpy(config.tag, tag, sizeof(config.tag));
    config.mode = APP_SCHEDULER_MODE_INSTANT;
    config.behavior = APP_SCHEDULER_BEHAVIOR_EVENT;
    config.enabled = false;
    config.repeat = strcmp(tag, CYD_CLOCK_ALARM1_TAG) == 0;
    config.weekday_mask = config.repeat ? CYD_CLOCK_ALARM_WEEKDAY_ALL : APP_SCHEDULER_WEEKDAY_ALL;
    config.at.hour = strcmp(tag, CYD_CLOCK_ALARM1_TAG) == 0 ? 7 : 7;
    config.at.minute = strcmp(tag, CYD_CLOCK_ALARM1_TAG) == 0 ? 0 : 30;
    config.at.second = 0;
    return config;
}

static bool cyd_clock_composition_alarm_schedule_exists(const char *tag)
{
    app_scheduler_status_t status = { 0 };
    return app_scheduler_get_status(CYD_CLOCK_ALARM_OWNER, tag, &status) == ESP_OK;
}

static esp_err_t cyd_clock_composition_upsert_alarm_config(const app_scheduler_config_t *config)
{
    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG, "alarm config is null");
    return app_scheduler_upsert(config);
}

static esp_err_t cyd_clock_composition_ensure_alarm_schedules(void)
{
    bool alarm1_exists = cyd_clock_composition_alarm_schedule_exists(CYD_CLOCK_ALARM1_TAG);
    bool alarm2_exists = cyd_clock_composition_alarm_schedule_exists(CYD_CLOCK_ALARM2_TAG);

    if (alarm1_exists && alarm2_exists) {
        return ESP_OK;
    }

    if (!alarm1_exists) {
        app_scheduler_config_t alarm1 = cyd_clock_composition_default_alarm_config(CYD_CLOCK_ALARM1_TAG);
        ESP_RETURN_ON_ERROR(cyd_clock_composition_upsert_alarm_config(&alarm1),
                            TAG,
                            "upsert alarm1 failed");
    }

    if (!alarm2_exists) {
        app_scheduler_config_t alarm2 = cyd_clock_composition_default_alarm_config(CYD_CLOCK_ALARM2_TAG);
        ESP_RETURN_ON_ERROR(cyd_clock_composition_upsert_alarm_config(&alarm2),
                            TAG,
                            "upsert alarm2 failed");
    }
    return ESP_OK;
}

static void cyd_clock_composition_alarm_handler(const app_scheduler_event_t *event, void *ctx)
{
    (void)ctx;

    if (event == NULL ||
        event->type != APP_SCHEDULER_EVENT_FIRED ||
        strcmp(event->owner, CYD_CLOCK_ALARM_OWNER) != 0) {
        return;
    }
    if (strcmp(event->tag, CYD_CLOCK_ALARM1_TAG) != 0 &&
        strcmp(event->tag, CYD_CLOCK_ALARM2_TAG) != 0) {
        return;
    }

    ESP_LOGI(TAG, "alarm triggered: %s", event->tag);
    (void)cyd_speaker_play_event(CYD_SPEAKER_EVENT_ALARM);
}

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
    /* Registering the clock also brings its settings screen; a product that
       omits the clock gets neither. */
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
    err = app_scheduler_init();
    if (err != ESP_OK) {
        (void)error_log_store_append_esp_err(TAG, "scheduler init failed", err);
        ESP_RETURN_ON_ERROR(err, TAG, "scheduler init failed");
    }
    err = app_scheduler_register_handler(CYD_CLOCK_ALARM_OWNER,
                                         cyd_clock_composition_alarm_handler,
                                         NULL);
    if (err != ESP_OK) {
        (void)error_log_store_append_esp_err(TAG, "alarm handler register failed", err);
        ESP_RETURN_ON_ERROR(err, TAG, "alarm handler register failed");
    }
    err = cyd_clock_composition_ensure_alarm_schedules();
    if (err != ESP_OK) {
        (void)error_log_store_append_esp_err(TAG, "alarm schedule init failed", err);
        ESP_RETURN_ON_ERROR(err, TAG, "alarm schedule init failed");
    }

#if APP_WIFI_STA_ENABLED && CONFIG_ESP32_WIFI_STA_AUTO_START
    if (boot_result.setup_shortcut_requested) {
        wifi_connection_request_setup_on_start();
    }
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

    cyd_clock_composition_register_apps();
    app_shell_set_home_return_allowed_callback(cyd_clock_composition_home_return_allowed, NULL);
    return app_shell_start(initial_app);
}
