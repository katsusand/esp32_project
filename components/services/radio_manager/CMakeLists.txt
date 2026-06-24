if(NOT DEFINED ENV{APP_WIFI_STA} OR NOT "$ENV{APP_WIFI_STA}" MATCHES "^(0|false|off|no)$")
    idf_component_register(SRCS "radio_manager.c"
                        INCLUDE_DIRS "include"
                        REQUIRES wifi_connection app_diagnostics nvs_flash nvs_health nvs_schema)
else()
    idf_component_register(SRCS "radio_manager_stub.c"
                        INCLUDE_DIRS "include")
endif()
