# Case-insensitive, matching the top-level CMakeLists.txt: "OFF" must not
# select the real sources while the code is compiled as Wi-Fi disabled.
string(TOLOWER "$ENV{APP_WIFI_STA}" APP_WIFI_STA_ENV)
if(NOT "${APP_WIFI_STA_ENV}" MATCHES "^(0|false|off|no)$")
    idf_component_register(SRCS "radio_manager.c"
                        INCLUDE_DIRS "include"
                        REQUIRES wifi_connection app_diagnostics nvs_flash nvs_health nvs_schema)
else()
    idf_component_register(SRCS "radio_manager_stub.c"
                        INCLUDE_DIRS "include")
endif()
