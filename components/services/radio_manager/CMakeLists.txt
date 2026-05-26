idf_component_register(SRCS "radio_manager.c"
                    INCLUDE_DIRS "include"
                    REQUIRES wifi_manager app_diagnostics nvs_flash)
