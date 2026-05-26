#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
IDF_ACTIVATE_SH="${HOME}/.espressif/tools/activate_idf_v5.4.3.sh"

if [[ ! -f "${IDF_ACTIVATE_SH}" ]]; then
    echo "error: ESP-IDF activation script not found: ${IDF_ACTIVATE_SH}" >&2
    echo "hint: install ESP-IDF v5.4.3 with EIM first, then retry." >&2
    exit 1
fi

cd "${PROJECT_ROOT}"

exec bash -lc '
set -euo pipefail
cd "$1"
set +u
# shellcheck disable=SC1090
source "$2"
set -u
export DEV="${DEV:-1}"

if [[ -z "${IDF_PATH:-}" ]]; then
    echo "error: IDF_PATH is not set after activating ESP-IDF." >&2
    exit 1
fi

if ! command -v python >/dev/null 2>&1; then
    echo "error: python is not available after activating ESP-IDF." >&2
    exit 1
fi

exec python "${IDF_PATH}/tools/idf.py" "${@:3}"
' bash "${PROJECT_ROOT}" "${IDF_ACTIVATE_SH}" "$@"
