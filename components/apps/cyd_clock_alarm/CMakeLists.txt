idf_component_register(SRCS "cyd_clock_alarm.c"
                    INCLUDE_DIRS "include"
                    REQUIRES app_scheduler
                    PRIV_REQUIRES cyd_speaker)
