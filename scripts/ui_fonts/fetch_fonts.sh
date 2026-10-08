#!/usr/bin/env bash
#
# Downloads the BIZ UDPGothic TTFs that gen_ui_fonts.py subsets.
#
# Pinned to one upstream commit and checked by SHA-256, so a regenerated font
# can only differ because the glyph set or the generator changed - never
# because upstream moved.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEST="${SCRIPT_DIR}/../../third_party/fonts/biz_udpgothic"
COMMIT="18934af56b9c003ca58c54bffbf226848cb11032"
BASE="https://raw.githubusercontent.com/googlefonts/morisawa-biz-ud-gothic/${COMMIT}/fonts/ttf"

fetch() {
    local name="$1" sha="$2"
    local path="${DEST}/${name}"
    if [[ -f "${path}" ]] && echo "${sha}  ${path}" | shasum -a 256 -c --status; then
        echo "ok        ${name}"
        return
    fi
    curl -sSfL -o "${path}.part" "${BASE}/${name}"
    echo "${sha}  ${path}.part" | shasum -a 256 -c --status || {
        echo "checksum mismatch: ${name}" >&2
        rm -f "${path}.part"
        exit 1
    }
    mv "${path}.part" "${path}"
    echo "fetched   ${name}"
}

mkdir -p "${DEST}"
fetch BIZUDPGothic-Regular.ttf 258d7156c165f2ff774b6efee637c22c3b950de0d8a10e501137061bc8085d01
fetch BIZUDPGothic-Bold.ttf 30eba52fc837e8b62c97d4b82e6706583149fb7294e3712dd71a655eaea80a90
