idf_component_register(SRCS "status_indicator.c"
                    INCLUDE_DIRS "include"
                    REQUIRES cyd_status_led wifi_connection)
