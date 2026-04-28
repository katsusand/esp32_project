#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
IDF_EXPORT_SH="${HOME}/.espressif/v5.4.3/esp-idf/export.sh"
BUILD_CACHE="${PROJECT_ROOT}/build/CMakeCache.txt"

if [[ ! -f "${IDF_EXPORT_SH}" ]]; then
    echo "error: ESP-IDF export script not found: ${IDF_EXPORT_SH}" >&2
    echo "hint: install ESP-IDF v5.4.3 first, then retry." >&2
    exit 1
fi

cd "${PROJECT_ROOT}"

# shellcheck disable=SC1090
source "${IDF_EXPORT_SH}"
export DEV="${DEV:-1}"

cached_python=""
if [[ -f "${BUILD_CACHE}" ]]; then
    cached_python="$(sed -n 's/^PYTHON:UNINITIALIZED=//p' "${BUILD_CACHE}" | head -n 1)"
fi

if [[ -n "${cached_python}" && -x "${cached_python}" ]]; then
    exec "${cached_python}" "${IDF_PATH}/tools/idf.py" "$@"
fi

exec "$@"
