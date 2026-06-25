set(CYD_CLOCK_COMPOSITION_REQUIRES
    app_scheduler
    app_shell
    cyd_clock_app
    cyd_system_apps
    cyd_speaker
    nvs_flash
    nvs_health
    nvs_schema
    sd_card_storage
    system_boot
    time_tick)

if(NOT DEFINED ENV{APP_WIFI_STA} OR NOT "$ENV{APP_WIFI_STA}" MATCHES "^(0|false|off|no)$")
    list(APPEND CYD_CLOCK_COMPOSITION_REQUIRES
        radio_manager
        status_indicator
        time_sync
        wifi_connection
        wifi_profile_store)
endif()

idf_component_register(SRCS "cyd_clock_composition.c"
                    INCLUDE_DIRS "include"
                    REQUIRES ${CYD_CLOCK_COMPOSITION_REQUIRES})
