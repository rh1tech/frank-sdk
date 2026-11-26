#!/bin/bash
# {{NAME}} — flash the board with nobody at it: ./flash.sh [BOARD] [image.uf2]
# ~BOOT~ over the board's USB CDC console, picotool load -v -x by serial, verify;
# SWD only when the console never answers. Boards and serials: ~/.config/frank/bench.yaml
# SPDX-License-Identifier: {{LICENSE}}
set -e
cd "$(dirname "$0")"
BOARD=$(echo "${1:-{{BOARD0}}}" | tr a-z A-Z)
IMG=${2:-$(ls -t build-$(echo "$BOARD" | tr A-Z a-z)/*.uf2 2>/dev/null | head -1)}
[ -f "$IMG" ] || { echo "no image: ./build.sh $BOARD" >&2; exit 1; }
SDK=${FRANK_SDK:-frank-sdk}
exec "$SDK/mcp/.venv/bin/frank" flash "$BOARD" "$IMG"
