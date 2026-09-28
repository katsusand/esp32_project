idf_component_register(SRCS "error_log_store.c"
                    INCLUDE_DIRS "include"
                    REQUIRES sd_card_storage sd_card_writer esp_timer)
