file(GLOB FONT_SRCS "${CMAKE_CURRENT_LIST_DIR}/generated/*.c")
list(APPEND FONT_SRCS "cyd_ui_fonts.c")

idf_component_register(SRCS ${FONT_SRCS}
                    INCLUDE_DIRS "include")

# Fail the build when a string literal uses a character the fonts lack.
#
# The check re-runs whenever any C/C++ source or the font manifest changes. A
# missing glyph would otherwise only show up as an empty box on the panel, and
# only on whichever screen happens to use it.
set(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")
file(GLOB_RECURSE UI_FONT_CHECK_INPUTS CONFIGURE_DEPENDS
     "${REPO_ROOT}/components/*.c"
     "${REPO_ROOT}/components/*.h"
     "${REPO_ROOT}/components/*.cpp"
     "${REPO_ROOT}/components/*.hpp"
     "${REPO_ROOT}/main/*.c"
     "${REPO_ROOT}/main/*.h")
list(FILTER UI_FONT_CHECK_INPUTS EXCLUDE REGEX "/cyd_ui_fonts/generated/")

idf_build_get_property(python PYTHON)
set(UI_FONT_CHECK_STAMP "${CMAKE_CURRENT_BINARY_DIR}/ui_font_glyphs.stamp")
add_custom_command(
    OUTPUT "${UI_FONT_CHECK_STAMP}"
    COMMAND "${python}" "${REPO_ROOT}/scripts/ui_fonts/check_ui_font_glyphs.py"
            --stamp "${UI_FONT_CHECK_STAMP}"
    DEPENDS ${UI_FONT_CHECK_INPUTS}
            "${CMAKE_CURRENT_LIST_DIR}/generated/text_font_chars.txt"
            "${REPO_ROOT}/scripts/ui_fonts/check_ui_font_glyphs.py"
            "${REPO_ROOT}/scripts/ui_fonts/ui_font_chars.py"
            "${REPO_ROOT}/scripts/ui_fonts/extra_chars.txt"
    COMMENT "Checking UI font glyph coverage"
    VERBATIM)
add_custom_target(cyd_ui_font_glyph_check DEPENDS "${UI_FONT_CHECK_STAMP}")
add_dependencies(${COMPONENT_LIB} cyd_ui_font_glyph_check)
