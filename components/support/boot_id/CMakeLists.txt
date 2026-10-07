idf_component_register(SRCS "boot_id.c" "boot_id_format.c"
                    INCLUDE_DIRS "include"
                    PRIV_REQUIRES esp_timer esp_hw_support)
