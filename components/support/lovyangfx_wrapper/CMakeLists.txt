set(LGFX_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../../third_party/lovyangfx_upstream)

file(GLOB SRCS
     ${LGFX_ROOT}/src/lgfx/Fonts/efont/*.c
     ${LGFX_ROOT}/src/lgfx/Fonts/IPA/*.c
     ${LGFX_ROOT}/src/lgfx/utility/*.c
     ${LGFX_ROOT}/src/lgfx/v1/*.cpp
     ${LGFX_ROOT}/src/lgfx/v1/misc/*.cpp
     ${LGFX_ROOT}/src/lgfx/v1/panel/*.cpp
     ${LGFX_ROOT}/src/lgfx/v1/platforms/arduino_default/*.cpp
     ${LGFX_ROOT}/src/lgfx/v1/platforms/esp32/*.cpp
     ${LGFX_ROOT}/src/lgfx/v1/platforms/esp32c3/*.cpp
     ${LGFX_ROOT}/src/lgfx/v1/platforms/esp32s2/*.cpp
     ${LGFX_ROOT}/src/lgfx/v1/platforms/esp32s3/*.cpp
     ${LGFX_ROOT}/src/lgfx/v1/touch/*.cpp)

list(REMOVE_ITEM SRCS
     ${LGFX_ROOT}/src/lgfx/v1/panel/Panel_M5HDMI.cpp
     ${LGFX_ROOT}/src/lgfx/v1/platforms/esp32/Bus_HUB75.cpp
     ${LGFX_ROOT}/src/lgfx/v1/platforms/esp32/Bus_Parallel8.cpp
     ${LGFX_ROOT}/src/lgfx/v1/platforms/esp32/Panel_CVBS.cpp
     ${LGFX_ROOT}/src/lgfx/v1/platforms/esp32s2/Bus_Parallel8.cpp
     ${LGFX_ROOT}/src/lgfx/v1/platforms/esp32s2/Bus_Parallel16.cpp)

idf_component_register(SRCS ${SRCS}
                    INCLUDE_DIRS "include" "${LGFX_ROOT}/src"
                    REQUIRES nvs_flash efuse esp_lcd driver esp_timer)
