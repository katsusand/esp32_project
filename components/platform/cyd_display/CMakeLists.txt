idf_component_register(SRCS "cyd_display.cpp" "cyd_display_text.c"
                    INCLUDE_DIRS "include"
                    REQUIRES app_diagnostics cyd_ui_fonts lovyangfx_wrapper nvs_flash nvs_health nvs_schema)
