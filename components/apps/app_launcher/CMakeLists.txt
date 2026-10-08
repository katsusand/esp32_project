idf_component_register(SRCS "app_launcher.c" "app_launcher_view.c"
                    INCLUDE_DIRS "include"
                    REQUIRES app_registry app_shell cyd_display cyd_input cyd_ui)
