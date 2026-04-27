# The following lines of boilerplate have to be in your project's CMakeLists
# in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.22)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
idf_build_set_property(MINIMAL_BUILD ON)

if(DEFINED ENV{DEV} AND NOT "$ENV{DEV}" STREQUAL "" AND NOT "$ENV{DEV}" STREQUAL "0")
    add_compile_definitions(APP_DEV=1)
endif()

project(esp32_project)
