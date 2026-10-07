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

if [[ "${status}" -eq 0 ]]; then
    echo "all host tests passed"
else
    echo "host tests FAILED"
fi
exit "${status}"
