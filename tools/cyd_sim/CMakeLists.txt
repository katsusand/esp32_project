# Desktop simulator for the CYD display stack. See docs/cyd_sim.md.
#
# Builds the device's own rendering code (cyd_display_render.hpp, cyd_ui,
# cyd_ui_fonts) against LovyanGFX's SDL backend, so what the window shows is
# what the panel shows.
cmake_minimum_required(VERSION 3.16)
project(cyd_sim C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

set(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
set(LGFX_SRC "${REPO_ROOT}/third_party/lovyangfx_upstream/src")

find_package(SDL2 REQUIRED)

# LovyanGFX with the SDL panel. Built without the ESP-IDF stubs on its include
# path so its platform detection only ever sees the real host headers.
# lgfx_fonts.cpp defines every built-in face, so the efont and IPA tables must
# be linked even though the simulator never draws with them. They are large
# and compile slowly once; incremental builds do not touch them again.
file(GLOB LGFX_SRCS CONFIGURE_DEPENDS
     "${LGFX_SRC}/lgfx/Fonts/efont/*.c"
     "${LGFX_SRC}/lgfx/Fonts/IPA/*.c"
     "${LGFX_SRC}/lgfx/utility/*.c"
     "${LGFX_SRC}/lgfx/v1/*.cpp"
     "${LGFX_SRC}/lgfx/v1/misc/*.cpp"
     "${LGFX_SRC}/lgfx/v1/platforms/sdl/*.cpp")
list(APPEND LGFX_SRCS
     "${LGFX_SRC}/lgfx/v1/panel/Panel_Device.cpp"
     "${LGFX_SRC}/lgfx/v1/panel/Panel_FrameBufferBase.cpp")
add_library(lgfx_sdl STATIC ${LGFX_SRCS})
target_include_directories(lgfx_sdl SYSTEM PUBLIC "${LGFX_SRC}")
target_compile_definitions(lgfx_sdl PUBLIC LGFX_SDL)
target_compile_options(lgfx_sdl PRIVATE -w)
target_link_libraries(lgfx_sdl PUBLIC SDL2::SDL2)

file(GLOB UI_FONT_SRCS CONFIGURE_DEPENDS
     "${REPO_ROOT}/components/support/cyd_ui_fonts/generated/*.c")
file(GLOB SCENE_SRCS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/scenes/*.c")

# Screen builders that apps keep apart from their service calls. By convention
# they are named *_view.c and their header sits in the component's include/
# directory, so a new view (in this project or a derived one) needs no edit
# here. See "Writing Simulator-Friendly Apps" in docs/cyd_sim.md.
file(GLOB VIEW_SRCS CONFIGURE_DEPENDS "${REPO_ROOT}/components/*/*/*_view.c")
set(VIEW_INCLUDE_DIRS "")
foreach(view_src IN LISTS VIEW_SRCS)
    get_filename_component(view_dir "${view_src}" DIRECTORY)
    list(APPEND VIEW_INCLUDE_DIRS "${view_dir}" "${view_dir}/include")
endforeach()
list(REMOVE_DUPLICATES VIEW_INCLUDE_DIRS)

add_executable(cyd_sim
    sim_main.cpp
    sim_port.cpp
    sim_stubs.c
    ${SCENE_SRCS}
    ${UI_FONT_SRCS}
    "${REPO_ROOT}/components/support/cyd_ui_fonts/cyd_ui_fonts.c"
    "${REPO_ROOT}/components/platform/cyd_display/cyd_display_text.c"
    "${REPO_ROOT}/components/framework/cyd_ui/cyd_ui.c"
    ${VIEW_SRCS})
target_include_directories(cyd_sim PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}"
    "${REPO_ROOT}/test/host/stub"
    "${REPO_ROOT}/components/platform/cyd_display"
    "${REPO_ROOT}/components/platform/cyd_display/include"
    "${REPO_ROOT}/components/support/cyd_ui_fonts/include"
    "${REPO_ROOT}/components/framework/cyd_ui/include"
    ${VIEW_INCLUDE_DIRS})
target_compile_options(cyd_sim PRIVATE
    $<$<COMPILE_LANGUAGE:C>:-Wall -Wextra -Wno-missing-field-initializers>
    $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra -Wno-missing-field-initializers -Wno-c99-designator>)
target_link_libraries(cyd_sim PRIVATE lgfx_sdl)
