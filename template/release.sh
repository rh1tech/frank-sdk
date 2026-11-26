#!/bin/bash
# {{NAME}} — release images for every board: ./release.sh [A.BB]
# Output: release/<board>_{{NAME}}_A_BB.uf2 (console on the UART: the native USB is the host)
# SPDX-License-Identifier: {{LICENSE}}
set -e
cd "$(dirname "$0")"
[ -n "$1" ] && echo "$1" > version.txt
V=$(tr -d '[:space:]' < version.txt); V=${V//./_}
mkdir -p release
for b in {{BOARDS}}; do
    ./build.sh "$b" -clean -uart >/dev/null
    l=$(echo "$b" | tr A-Z a-z)
    cp "build-$l/${l}_{{NAME}}.uf2" "release/${l}_{{NAME}}_$V.uf2"
    echo "release/${l}_{{NAME}}_$V.uf2"
done
