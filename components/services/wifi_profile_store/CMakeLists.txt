if(NOT DEFINED ENV{APP_WIFI_STA} OR NOT "$ENV{APP_WIFI_STA}" MATCHES "^(0|false|off|no)$")
    idf_component_register(SRCS "wifi_profile_store.c"
                        INCLUDE_DIRS "include"
                        REQUIRES nvs_flash esp_wifi nvs_health nvs_schema)
else()
    idf_component_register(SRCS "wifi_profile_store_stub.c"
                        INCLUDE_DIRS "include")
endif()
