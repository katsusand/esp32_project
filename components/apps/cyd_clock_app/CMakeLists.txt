idf_component_register(SRCS "cyd_clock_app.c" "cyd_clock_view.c"
                    INCLUDE_DIRS "include"
                    REQUIRES app_diagnostics app_registry app_shell cyd_clock_alarm cyd_clock_settings_app cyd_display cyd_input cyd_system_apps cyd_ui cyd_wifi_setup sd_card_status wifi_connection time_sync time_tick)
