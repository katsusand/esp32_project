set(CYD_CLOCK_COMPOSITION_REQUIRES
    app_launcher
    app_registry
    app_scheduler
    app_shell
    cyd_clock_app
    cyd_system_apps
    error_log_store
    nvs_flash
    nvs_health
    nvs_schema
    sd_card_status
    system_boot
    time_tick)

# Case-insensitive, matching the top-level CMakeLists.txt: "OFF" must not
# select the real sources while the code is compiled as Wi-Fi disabled.
string(TOLOWER "$ENV{APP_WIFI_STA}" APP_WIFI_STA_ENV)
if(NOT "${APP_WIFI_STA_ENV}" MATCHES "^(0|false|off|no)$")
    list(APPEND CYD_CLOCK_COMPOSITION_REQUIRES
        radio_manager
        status_indicator
        time_sync
        wifi_connection
        wifi_profile_store
        wifi_rssi_history)
endif()

idf_component_register(SRCS "cyd_clock_composition.c"
                    INCLUDE_DIRS "include"
                    REQUIRES ${CYD_CLOCK_COMPOSITION_REQUIRES})
