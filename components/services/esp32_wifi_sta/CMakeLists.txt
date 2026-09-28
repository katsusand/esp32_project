# Case-insensitive, matching the top-level CMakeLists.txt: "OFF" must not
# select the real sources while the code is compiled as Wi-Fi disabled.
string(TOLOWER "$ENV{APP_WIFI_STA}" APP_WIFI_STA_ENV)
if(NOT "${APP_WIFI_STA_ENV}" MATCHES "^(0|false|off|no)$")
    idf_component_register(SRCS "esp32_wifi_sta.c"
                        INCLUDE_DIRS "include"
                        REQUIRES esp_wifi esp_netif esp_event nvs_flash wifi_profile_store)
else()
    idf_component_register(SRCS "esp32_wifi_sta_stub.c"
                        INCLUDE_DIRS "include")
endif()
