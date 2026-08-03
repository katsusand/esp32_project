if(NOT DEFINED ENV{APP_WIFI_STA} OR NOT "$ENV{APP_WIFI_STA}" MATCHES "^(0|false|off|no)$")
    idf_component_register(SRCS "wifi_rssi_history.c"
                        INCLUDE_DIRS "include"
                        REQUIRES esp_wifi radio_manager app_diagnostics)
else()
    idf_component_register(SRCS "wifi_rssi_history_stub.c"
                        INCLUDE_DIRS "include")
endif()
