idf_component_register(SRCS "main.c"
                    PRIV_REQUIRES nvs_flash app_shell cyd_alarm cyd_status_led cyd_speaker cyd_display cyd_input wifi_manager time_sync cyd_clock_app
                    INCLUDE_DIRS ".")
