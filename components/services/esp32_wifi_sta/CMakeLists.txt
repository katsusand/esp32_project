if(NOT DEFINED ENV{APP_WIFI_STA} OR NOT "$ENV{APP_WIFI_STA}" MATCHES "^(0|false|off|no)$")
    idf_component_register(SRCS "esp32_wifi_sta.c"
                        INCLUDE_DIRS "include"
                        REQUIRES error_log_store esp_wifi esp_netif esp_event nvs_flash wifi_profile_store)
else()
    idf_component_register(SRCS "esp32_wifi_sta_stub.c"
                        INCLUDE_DIRS "include")
endif()
