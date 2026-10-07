idf_component_register(SRCS "sd_card_status.c" "sd_card_status_logic.c"
                            "sd_card_status_icon.c" "sd_card_status_icon_art.c"
                    INCLUDE_DIRS "include"
                    REQUIRES cyd_display
                    PRIV_REQUIRES sd_card_storage sd_card_writer error_log_store app_diagnostics)
