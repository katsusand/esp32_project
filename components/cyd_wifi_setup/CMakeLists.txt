idf_component_register(SRCS "cyd_wifi_setup.c"
                    INCLUDE_DIRS "include"
                    REQUIRES app_shell cyd_display cyd_input cyd_ui esp32_wifi_sta time_sync wifi_manager)
