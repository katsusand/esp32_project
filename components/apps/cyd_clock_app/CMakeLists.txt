idf_component_register(SRCS "cyd_clock_app.c"
                    INCLUDE_DIRS "include"
                    REQUIRES app_diagnostics app_scheduler app_shell cyd_clock_settings_app cyd_display cyd_input cyd_system_apps cyd_ui cyd_wifi_setup wifi_connection time_sync time_tick)
