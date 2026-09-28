# Case-insensitive, matching the top-level CMakeLists.txt: "OFF" must not
# select the real sources while the code is compiled as Wi-Fi disabled.
string(TOLOWER "$ENV{APP_WIFI_STA}" APP_WIFI_STA_ENV)
if(NOT "${APP_WIFI_STA_ENV}" MATCHES "^(0|false|off|no)$")
    idf_component_register(SRCS "time_sync.c"
                        INCLUDE_DIRS "include"
                        REQUIRES app_diagnostics error_log_store esp_netif esp_hw_support nvs_flash nvs_health radio_manager nvs_schema)
else()
    idf_component_register(SRCS "time_sync_stub.c"
                        INCLUDE_DIRS "include")
endif()
