idf_component_register(SRCS "cyd_status_led.c"
                    INCLUDE_DIRS "include"
                    PRIV_REQUIRES app_diagnostics esp_driver_gpio esp_timer)
