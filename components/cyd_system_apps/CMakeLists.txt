idf_component_register(SRCS "cyd_system_apps.c"
                    INCLUDE_DIRS "include"
                    REQUIRES app_shell cyd_display cyd_input cyd_ui cyd_wifi_setup esp_app_format esp_hw_support esp_system heap wifi_manager)
