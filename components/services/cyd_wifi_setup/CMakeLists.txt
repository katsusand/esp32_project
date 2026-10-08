# Case-insensitive, matching the top-level CMakeLists.txt: "OFF" must not
# select the real sources while the code is compiled as Wi-Fi disabled.
string(TOLOWER "$ENV{APP_WIFI_STA}" APP_WIFI_STA_ENV)
if(NOT "${APP_WIFI_STA_ENV}" MATCHES "^(0|false|off|no)$")
    idf_component_register(SRCS "cyd_wifi_setup.c" "cyd_wifi_setup_view.c"
                        INCLUDE_DIRS "include"
                        REQUIRES app_shell cyd_display cyd_input cyd_text_input cyd_ui esp32_wifi_sta time_sync wifi_connection)
else()
    idf_component_register(SRCS "cyd_wifi_setup_stub.c"
                        INCLUDE_DIRS "include"
                        REQUIRES app_shell cyd_display cyd_input esp32_wifi_sta)
endif()
