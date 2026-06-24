# The following lines of boilerplate have to be in your project's CMakeLists
# in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.22)

set(APP_WIFI_STA_REQUESTED ON)
if(DEFINED ENV{APP_WIFI_STA})
    string(TOLOWER "$ENV{APP_WIFI_STA}" APP_WIFI_STA_ENV)
    if(APP_WIFI_STA_ENV STREQUAL "0" OR
       APP_WIFI_STA_ENV STREQUAL "false" OR
       APP_WIFI_STA_ENV STREQUAL "off" OR
       APP_WIFI_STA_ENV STREQUAL "no")
        set(APP_WIFI_STA_REQUESTED OFF)
    endif()
endif()
set(APP_WIFI_STA_ENABLED ${APP_WIFI_STA_REQUESTED} CACHE BOOL "Build Wi-Fi STA feature" FORCE)

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/components/apps"
    "${CMAKE_CURRENT_LIST_DIR}/components/framework"
    "${CMAKE_CURRENT_LIST_DIR}/components/platform"
    "${CMAKE_CURRENT_LIST_DIR}/components/products"
    "${CMAKE_CURRENT_LIST_DIR}/components/services"
    "${CMAKE_CURRENT_LIST_DIR}/components/support")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
idf_build_set_property(MINIMAL_BUILD ON)

if(DEFINED ENV{DEV} AND NOT "$ENV{DEV}" STREQUAL "" AND NOT "$ENV{DEV}" STREQUAL "0")
    add_compile_definitions(APP_DEV=1)
endif()

if(APP_WIFI_STA_ENABLED)
    add_compile_definitions(APP_WIFI_STA_ENABLED=1)
else()
    add_compile_definitions(APP_WIFI_STA_ENABLED=0)
endif()

project(esp32_project)
