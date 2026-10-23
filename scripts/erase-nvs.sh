#!/usr/bin/env bash
#
# Erases the `nvs` partition of a connected unit over USB: every setting goes
# back to unset, the settings PIN included (docs/settings_pin.md).
#
# Only `nvs` is touched. The firmware, `storage` and any other partition (a
# derived project's device keys, for example) stay as they are. The partition
# is found by name in the table on the unit, at the offset this project's
# sdkconfig sets.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
IDF_ACTIVATE_SH="${HOME}/.espressif/tools/activate_idf_v5.5.6.sh"

if [[ $# -ne 1 ]]; then
    echo "usage: $0 PORT" >&2
    echo "example: $0 /dev/cu.usbserial-11410" >&2
    exit 1
fi
if [[ ! -f "${IDF_ACTIVATE_SH}" ]]; then
    echo "error: ESP-IDF activation script not found: ${IDF_ACTIVATE_SH}" >&2
    exit 1
fi

offset=""
for config in "${PROJECT_ROOT}/sdkconfig" "${PROJECT_ROOT}/sdkconfig.defaults"; do
    if [[ -f "${config}" ]]; then
        offset="$(sed -n 's/^CONFIG_PARTITION_TABLE_OFFSET=//p' "${config}" | head -n 1)"
        [[ -n "${offset}" ]] && break
    fi
done
offset="${offset:-0x8000}"

exec bash -lc '
set -euo pipefail
set +u
# shellcheck disable=SC1090
source "$1"
set -u
exec python "${IDF_PATH}/components/partition_table/parttool.py" --port "$2" \
    --partition-table-offset "$3" erase_partition --partition-name nvs
' bash "${IDF_ACTIVATE_SH}" "$1" "${offset}"
