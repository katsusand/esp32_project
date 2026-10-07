idf_component_register(SRCS "error_log_store.c"
                            "error_log_format.c" "error_log_naming.c" "error_log_hold.c" "error_log_dedupe.c"
                    INCLUDE_DIRS "include"
                    REQUIRES sd_card_storage sd_card_writer esp_timer
                    PRIV_REQUIRES boot_id esp_app_format esp_system freertos)
