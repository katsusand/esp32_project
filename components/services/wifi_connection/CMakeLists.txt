if(NOT DEFINED ENV{APP_WIFI_STA} OR NOT "$ENV{APP_WIFI_STA}" MATCHES "^(0|false|off|no)$")
    idf_component_register(SRCS "wifi_connection.c" "wifi_connection_manager.c"
                        INCLUDE_DIRS "include"
                        REQUIRES app_diagnostics esp32_wifi_sta esp_timer wifi_profile_store)
else()
    idf_component_register(SRCS "wifi_connection_stub.c"
                        INCLUDE_DIRS "include"
                        REQUIRES esp32_wifi_sta)
endif()
