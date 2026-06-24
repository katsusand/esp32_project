idf_component_register(SRCS "system_boot.c"
                    INCLUDE_DIRS "include"
                    REQUIRES nvs_flash nvs_health cyd_status_led cyd_speaker cyd_display cyd_input
                    PRIV_REQUIRES esp_driver_gpio)
