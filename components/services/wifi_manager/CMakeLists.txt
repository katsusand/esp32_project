idf_component_register(SRCS "wifi_manager.c"
                    INCLUDE_DIRS "include"
                    REQUIRES app_diagnostics cyd_status_led cyd_wifi_setup esp32_wifi_sta)
