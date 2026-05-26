idf_component_register(SRCS "cyd_alarm.c"
                    INCLUDE_DIRS "include"
                    REQUIRES app_scheduler cyd_speaker nvs_flash)
