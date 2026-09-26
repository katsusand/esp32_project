# Case-insensitive, matching the top-level CMakeLists.txt: "OFF" must not
# select the real sources while the code is compiled as Wi-Fi disabled.
string(TOLOWER "$ENV{APP_WIFI_STA}" APP_WIFI_STA_ENV)
if(NOT "${APP_WIFI_STA_ENV}" MATCHES "^(0|false|off|no)$")
    idf_component_register(SRCS "wifi_profile_store.c"
                        INCLUDE_DIRS "include"
                        REQUIRES nvs_flash esp_wifi nvs_health nvs_schema)
else()
    idf_component_register(SRCS "wifi_profile_store_stub.c"
                        INCLUDE_DIRS "include")
endif()
