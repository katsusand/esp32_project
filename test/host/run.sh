#!/usr/bin/env bash
#
# Host-side tests.
#
# These compile the real component sources - not copies - against the minimal
# ESP-IDF stubs in stub/, so logic that does not need the SDK can be tested
# without flashing hardware.
#
# English supplement: only sources free of FreeRTOS tasks / NVS / HTTP / the
# display belong here, or sources whose few SDK calls the test can replace
# (test_sd_card_status_machine.c replaces the card, the error log and the
# clock). Anything needing the real SDK must be verified on a unit.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
CC="${CC:-cc}"
CFLAGS=(-std=c11 -Wall -Wextra -Werror -I"${SCRIPT_DIR}/stub")

mkdir -p "${BUILD_DIR}"

status=0

run_case() {
    local name="$1"
    shift
    echo "--- ${name}"
    if "$@"; then
        echo "--- ${name}: PASS"
    else
        echo "--- ${name}: FAIL"
        status=1
    fi
    echo
}

# sd_card_status: which state a failed mount means, and how long to wait before
# trying again - a wrong answer is the wrong icon, or a mount loop on a unit
# that has no card
SD_CARD_STATUS="${PROJECT_ROOT}/components/services/sd_card_status"
"${CC}" "${CFLAGS[@]}" -I"${SD_CARD_STATUS}/include" \
    "${SD_CARD_STATUS}/sd_card_status_logic.c" "${SCRIPT_DIR}/test_sd_card_status_logic.c" \
    -o "${BUILD_DIR}/test_sd_card_status_logic"

run_case "SD card status decisions" "${BUILD_DIR}/test_sd_card_status_logic"

# sd_card_status: the real source file against a fake card, a fake error log and
# a clock moved by hand - a card that is missing, unformatted, full, pulled and
# put back. With no card detect pin the state machine is all there is.
# gnu11: the source uses POSIX open()/write(), which the test redirects.
"${CC}" "${CFLAGS[@]}" -std=gnu11 \
    -I"${SD_CARD_STATUS}/include" -I"${SD_CARD_STATUS}" \
    -I"${PROJECT_ROOT}/components/platform/sd_card_storage/include" \
    -I"${PROJECT_ROOT}/components/services/sd_card_writer/include" \
    -I"${PROJECT_ROOT}/components/services/error_log_store/include" \
    -I"${PROJECT_ROOT}/components/support/app_diagnostics/include" \
    "${SD_CARD_STATUS}/sd_card_status_logic.c" "${SCRIPT_DIR}/test_sd_card_status_machine.c" \
    -o "${BUILD_DIR}/test_sd_card_status_machine"

run_case "SD card status state machine" "${BUILD_DIR}/test_sd_card_status_machine"

# sd_card_status_icon_art: the icons are typed in as strings; a slipped
# character must fail here, not show as a smudged icon on a unit
"${CC}" "${CFLAGS[@]}" -I"${SD_CARD_STATUS}" \
    "${SD_CARD_STATUS}/sd_card_status_icon_art.c" "${SCRIPT_DIR}/test_sd_card_status_icon_art.c" \
    -o "${BUILD_DIR}/test_sd_card_status_icon_art"

run_case "SD card status icons" "${BUILD_DIR}/test_sd_card_status_icon_art"

# boot_id: a well formed UUID, and one that does not collapse when the random
# source is weak
BOOT_ID="${PROJECT_ROOT}/components/support/boot_id"
"${CC}" "${CFLAGS[@]}" -I"${BOOT_ID}/include" \
    "${BOOT_ID}/boot_id_format.c" "${SCRIPT_DIR}/test_boot_id_format.c" \
    -o "${BUILD_DIR}/test_boot_id_format"

run_case "boot id" "${BUILD_DIR}/test_boot_id_format"

# error_log_store: the pure parts - the line format, file names and today's
# file, the hold buffer, folding of repeats
ERROR_LOG_STORE="${PROJECT_ROOT}/components/services/error_log_store"
for part in format naming hold dedupe; do
    "${CC}" "${CFLAGS[@]}" -I"${ERROR_LOG_STORE}" \
        "${ERROR_LOG_STORE}/error_log_${part}.c" "${SCRIPT_DIR}/test_error_log_${part}.c" \
        -o "${BUILD_DIR}/test_error_log_${part}"
done

run_case "error log line format" "${BUILD_DIR}/test_error_log_format"
run_case "error log file names" "${BUILD_DIR}/test_error_log_naming"
run_case "error log hold buffer" "${BUILD_DIR}/test_error_log_hold"
run_case "error log repeat folding" "${BUILD_DIR}/test_error_log_dedupe"

# error_log_store: the real source against a card writer that really writes
# files into a temporary directory, a clock that can be set or left unset, and
# timers the test fires by moving the clock - held lines, the clock question,
# file choice and appending, the boot line, date changes, a card that goes away.
"${CC}" "${CFLAGS[@]}" -std=gnu11 \
    -I"${ERROR_LOG_STORE}" -I"${ERROR_LOG_STORE}/include" \
    -I"${PROJECT_ROOT}/components/services/sd_card_writer/include" \
    -I"${PROJECT_ROOT}/components/platform/sd_card_storage/include" \
    -I"${BOOT_ID}/include" \
    "${ERROR_LOG_STORE}/error_log_format.c" "${ERROR_LOG_STORE}/error_log_naming.c" \
    "${ERROR_LOG_STORE}/error_log_hold.c" "${ERROR_LOG_STORE}/error_log_dedupe.c" \
    "${SCRIPT_DIR}/test_error_log_store_machine.c" \
    -o "${BUILD_DIR}/test_error_log_store_machine"

run_case "error log end to end" "${BUILD_DIR}/test_error_log_store_machine"

# radio_manager: which Wi-Fi state means which failure, and the words a caller
# puts in its error log line - every state, every failure, every Wi-Fi reason
RADIO_MANAGER="${PROJECT_ROOT}/components/services/radio_manager"
WIFI_CONNECTION="${PROJECT_ROOT}/components/services/wifi_connection"
"${CC}" "${CFLAGS[@]}" -I"${RADIO_MANAGER}" -I"${RADIO_MANAGER}/include" \
    -I"${WIFI_CONNECTION}/include" -I"${PROJECT_ROOT}/components/services/esp32_wifi_sta/include" \
    "${RADIO_MANAGER}/radio_manager_failure.c" "${WIFI_CONNECTION}/wifi_connection_failure_text.c" \
    "${SCRIPT_DIR}/test_radio_manager_failure.c" \
    -o "${BUILD_DIR}/test_radio_manager_failure"

run_case "radio manager failure reasons" "${BUILD_DIR}/test_radio_manager_failure"

# Japanese UI: the display text rules and font tables every UI test links.
# A screen's layout test adds its view sources and include path to these.
CYD_DISPLAY="${PROJECT_ROOT}/components/platform/cyd_display"
CYD_UI_FONTS="${PROJECT_ROOT}/components/support/cyd_ui_fonts"
CYD_UI="${PROJECT_ROOT}/components/framework/cyd_ui"
UI_TEST_INCLUDES=(-I"${CYD_DISPLAY}" -I"${CYD_DISPLAY}/include" -I"${CYD_UI_FONTS}/include"
                  -I"${CYD_UI}/include")
UI_TEST_SRCS=("${CYD_DISPLAY}/cyd_display_text.c" "${CYD_UI_FONTS}/cyd_ui_fonts.c"
              "${CYD_UI_FONTS}"/generated/*.c "${CYD_UI}/cyd_ui.c" "${SCRIPT_DIR}/ui_test_support.c")

# UTF-8 cuts, reference-or-copy, measuring and wrapping with the real fonts
"${CC}" "${CFLAGS[@]}" -std=gnu11 "${UI_TEST_INCLUDES[@]}" \
    "${UI_TEST_SRCS[@]}" "${SCRIPT_DIR}/test_ui_text.c" \
    -o "${BUILD_DIR}/test_ui_text"

run_case "Japanese UI text" "${BUILD_DIR}/test_ui_text"

# Settings chrome, stepper rows and every keyboard state, measured with the real fonts
CYD_TEXT_INPUT="${PROJECT_ROOT}/components/framework/cyd_text_input"
"${CC}" "${CFLAGS[@]}" -std=gnu11 "${UI_TEST_INCLUDES[@]}" -I"${CYD_TEXT_INPUT}/include" \
    "${UI_TEST_SRCS[@]}" "${CYD_TEXT_INPUT}/cyd_text_input_view.c" "${SCRIPT_DIR}/test_ui_common.c" \
    -o "${BUILD_DIR}/test_ui_common"

run_case "Japanese UI shared parts" "${BUILD_DIR}/test_ui_common"

# System settings: every page, dialog and stepper value, measured with the real fonts
SYSTEM_APPS="${PROJECT_ROOT}/components/apps/cyd_system_apps"
"${CC}" "${CFLAGS[@]}" -std=gnu11 "${UI_TEST_INCLUDES[@]}" -I"${SYSTEM_APPS}" \
    "${UI_TEST_SRCS[@]}" "${SYSTEM_APPS}/system_settings_view.c" "${SCRIPT_DIR}/test_system_settings_view.c" \
    -o "${BUILD_DIR}/test_system_settings_view"

run_case "System settings screens" "${BUILD_DIR}/test_system_settings_view"

# System information: every page with the longest values, measured with the real fonts
"${CC}" "${CFLAGS[@]}" -std=gnu11 "${UI_TEST_INCLUDES[@]}" -I"${SYSTEM_APPS}" \
    "${UI_TEST_SRCS[@]}" "${SYSTEM_APPS}/system_settings_view.c" "${SYSTEM_APPS}/system_info_view.c" \
    "${SCRIPT_DIR}/test_system_info_view.c" -o "${BUILD_DIR}/test_system_info_view"

run_case "System information screens" "${BUILD_DIR}/test_system_info_view"

# Wi-Fi setup: the network list, notices and failure dialog
WIFI_SETUP="${PROJECT_ROOT}/components/services/cyd_wifi_setup"
"${CC}" "${CFLAGS[@]}" -std=gnu11 "${UI_TEST_INCLUDES[@]}" -I"${WIFI_SETUP}" \
    "${UI_TEST_SRCS[@]}" "${WIFI_SETUP}/cyd_wifi_setup_view.c" "${SCRIPT_DIR}/test_wifi_setup_view.c" \
    -o "${BUILD_DIR}/test_wifi_setup_view"

run_case "Wi-Fi setup screens" "${BUILD_DIR}/test_wifi_setup_view"

if [[ "${status}" -eq 0 ]]; then
    echo "all host tests passed"
else
    echo "host tests FAILED"
fi
exit "${status}"
