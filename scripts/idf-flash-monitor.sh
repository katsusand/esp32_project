#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [[ $# -lt 1 ]]; then
    echo "usage: $0 PORT [extra idf.py args...]" >&2
    echo "example: $0 /dev/cu.usbserial-11410" >&2
    exit 1
fi

PORT="$1"
shift

exec "${SCRIPT_DIR}/idf-env.sh" -p "${PORT}" flash monitor "$@"
