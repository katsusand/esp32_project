# cyd_ui_theme_store.c is firmware only: the simulator and the host tests build
# cyd_ui.c on its own, without NVS.
idf_component_register(SRCS "cyd_ui.c" "cyd_ui_theme_store.c"
                    INCLUDE_DIRS "include"
                    REQUIRES cyd_display
                    PRIV_REQUIRES nvs_flash nvs_schema)
