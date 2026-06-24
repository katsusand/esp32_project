idf_component_register(SRCS "app_scheduler.c"
                    INCLUDE_DIRS "include"
                    REQUIRES nvs_flash nvs_health time_tick nvs_schema)
