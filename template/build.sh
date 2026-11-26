#!/bin/bash
# {{NAME}} — build for one board: ./build.sh [BOARD] [-clean] [-uart]
#   BOARD: {{BOARDS}} (default {{BOARD0}}), or HOST (SDL2 on this machine); -uart: console on the UART
# Output: build-<board>/<board>_{{NAME}}.uf2
# SPDX-License-Identifier: {{LICENSE}}
set -e
cd "$(dirname "$0")"
BOARD={{BOARD0}}; CLEAN=0; CONSOLE=""
for a in "$@"; do
    case $a in
        -clean) CLEAN=1 ;;
        -uart) CONSOLE="-DFRANK_CONSOLE=uart" ;;
        -usb) CONSOLE="-DFRANK_CONSOLE=usb" ;;
        -h|--help) sed -n 2,4p "$0"; exit 0 ;;
        *) BOARD=$(echo "$a" | tr a-z A-Z) ;;
    esac
done
if [ "$BOARD" = HOST ]; then   # the firmware on SDL2, on this machine (frank-sdk/host)
    B=build-host; [ $CLEAN = 1 ] && rm -rf "$B"
    cmake -S "${FRANK_SDK:-frank-sdk}/host" -B "$B" -DAPP_DIR="$PWD" && cmake --build "$B"
    echo "$B/frank_app [-sd DIR]"; exit 0
fi
B=build-$(echo "$BOARD" | tr A-Z a-z)
[ $CLEAN = 1 ] && rm -rf "$B"
: "${PICO_SDK_PATH:=$HOME/pico/pico-sdk}"
cmake -S . -B "$B" -G Ninja -DFRANK_BOARD="$BOARD" -DPICO_SDK_PATH="$PICO_SDK_PATH" $CONSOLE ${FRANK_SDK:+-DFRANK_SDK=$FRANK_SDK}
cmake --build "$B"
ls -1 "$B"/*.uf2
