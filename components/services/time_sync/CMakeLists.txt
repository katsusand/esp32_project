if(NOT DEFINED ENV{APP_WIFI_STA} OR NOT "$ENV{APP_WIFI_STA}" MATCHES "^(0|false|off|no)$")
    idf_component_register(SRCS "time_sync.c"
                        INCLUDE_DIRS "include"
                        REQUIRES app_diagnostics esp_netif esp_hw_support nvs_flash nvs_health radio_manager nvs_schema)
else()
    idf_component_register(SRCS "time_sync_stub.c"
                        INCLUDE_DIRS "include")
endif()
