set(LGFX_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../../third_party/lovyangfx_upstream)

# Same source list as upstream boards.cmake/esp-idf.cmake. Since 1.2.x the
# platform code is .inl files compiled through these few translation units
# (lgfx_v1_platforms.cpp includes every platforms/*/*.inl and each one compiles
# only for its target), so it can no longer be trimmed per file.
#
# Left out on purpose: panel/Panel_M5HDMI.cpp and touch/Touch_GSLx680.cpp. They
# are separate translation units for hardware a CYD does not have.
set(SRCS
     ${LGFX_ROOT}/src/lgfx/v1/lgfx_v1.cpp
     ${LGFX_ROOT}/src/lgfx/v1/lgfx_v1_panel.cpp
     ${LGFX_ROOT}/src/lgfx/v1/lgfx_v1_touch.cpp
     ${LGFX_ROOT}/src/lgfx/v1/lgfx_v1_platforms.cpp
     ${LGFX_ROOT}/src/lgfx/v1/LGFXBase.cpp
     ${LGFX_ROOT}/src/lgfx/v1/lgfx_fonts.cpp
     ${LGFX_ROOT}/src/lgfx/Fonts/efont/lgfx_efont_cn.c
     ${LGFX_ROOT}/src/lgfx/Fonts/efont/lgfx_efont_ja.c
     ${LGFX_ROOT}/src/lgfx/Fonts/efont/lgfx_efont_kr.c
     ${LGFX_ROOT}/src/lgfx/Fonts/efont/lgfx_efont_tw.c
     ${LGFX_ROOT}/src/lgfx/Fonts/IPA/lgfx_font_japan.c
     ${LGFX_ROOT}/src/lgfx/utility/lgfx_miniz.c
     ${LGFX_ROOT}/src/lgfx/utility/lgfx_pngle.c
     ${LGFX_ROOT}/src/lgfx/utility/lgfx_qoi.c
     ${LGFX_ROOT}/src/lgfx/utility/lgfx_qrcode.c
     ${LGFX_ROOT}/src/lgfx/utility/lgfx_tjpgd.c)

# Same REQUIRES as upstream. From ESP-IDF 6 the legacy `driver` component no
# longer pulls in the LEDC and I2S drivers or `hal`, which LovyanGFX's ESP32
# backend (Light_PWM, the I2S bus types, register-level GPIO) still uses.
set(LGFX_REQUIRES nvs_flash efuse esp_lcd driver esp_timer esp_mm)
if(IDF_VERSION_MAJOR GREATER_EQUAL 6)
    list(APPEND LGFX_REQUIRES esp_driver_ledc esp_driver_i2s hal)
endif()

idf_component_register(SRCS ${SRCS}
                    INCLUDE_DIRS "${LGFX_ROOT}/src"
                    REQUIRES ${LGFX_REQUIRES})
