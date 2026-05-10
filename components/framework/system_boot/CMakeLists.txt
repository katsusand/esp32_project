idf_component_register(SRCS "system_boot.c"
                    INCLUDE_DIRS "include"
                    REQUIRES app_shell nvs_flash cyd_alarm cyd_status_led cyd_speaker cyd_display cyd_input app_scheduler time_tick
                    PRIV_REQUIRES esp_driver_gpio wifi_manager time_sync)
