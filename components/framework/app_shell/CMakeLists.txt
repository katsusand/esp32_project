idf_component_register(SRCS "app_shell.c"
                    INCLUDE_DIRS "include"
                    REQUIRES cyd_display cyd_input nvs_flash nvs_health nvs_schema)
