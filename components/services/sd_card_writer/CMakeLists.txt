idf_component_register(SRCS "sd_card_writer.c" "sd_card_writer_benchmark.c"
                    INCLUDE_DIRS "include"
                    REQUIRES sd_card_storage
                    PRIV_REQUIRES app_diagnostics)
