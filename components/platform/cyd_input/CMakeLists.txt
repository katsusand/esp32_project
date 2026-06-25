idf_component_register(SRCS "cyd_input.c"
                    INCLUDE_DIRS "include"
                    REQUIRES app_diagnostics cyd_display nvs_flash esp_driver_gpio nvs_schema xpt2046_softspi)
