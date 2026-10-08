idf_component_register(SRCS "cyd_clock_settings_app.c" "cyd_clock_settings_view.c"
                    INCLUDE_DIRS "include"
                    REQUIRES app_registry app_scheduler app_shell cyd_clock_alarm cyd_display cyd_input cyd_ui)
