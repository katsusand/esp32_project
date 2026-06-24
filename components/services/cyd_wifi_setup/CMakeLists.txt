if(NOT DEFINED ENV{APP_WIFI_STA} OR NOT "$ENV{APP_WIFI_STA}" MATCHES "^(0|false|off|no)$")
    idf_component_register(SRCS "cyd_wifi_setup.c"
                        INCLUDE_DIRS "include"
                        REQUIRES app_shell cyd_display cyd_input cyd_text_input cyd_ui esp32_wifi_sta time_sync wifi_connection)
else()
    idf_component_register(SRCS "cyd_wifi_setup_stub.c"
                        INCLUDE_DIRS "include"
                        REQUIRES app_shell cyd_display cyd_input esp32_wifi_sta)
endif()
