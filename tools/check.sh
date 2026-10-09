#!/usr/bin/env bash
# Runs every automated check. Exits non-zero on the first failure.
#   tools/check.sh            host tests, fallback check, simulator, firmware build
#   tools/check.sh --device   also flash the board and run tools/smoke.py
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"
export IDF_PATH="${IDF_PATH:-$HOME/esp/esp-idf}"

step() { printf '\n== %s\n' "$*"; }

step "lint: no bsp_display_lock in main/ (it reports success as false)"
if grep -rn "bsp_display_lock\|bsp_display_unlock" main/*.c; then
    echo "use display_lock()/display_unlock() from main/display.c" >&2
    exit 1
fi

step "host unit tests (core logic, fixtures, fallback within 1 minute)"
cmake -S test -B test/build -G Ninja -DCMAKE_BUILD_TYPE=Debug >/dev/null
ninja -C test/build
./test/build/core_tests

step "simulator screenshots and clipped-text check"
cmake -S sim -B sim/build -G Ninja -DCMAKE_BUILD_TYPE=Debug >/dev/null
ninja -C sim/build
mkdir -p sim/shots
./sim/build/sim sim/shots

step "firmware build"
# shellcheck disable=SC1091
source "$IDF_PATH/export.sh" >/dev/null
idf.py build

if [[ "${1:-}" == "--device" ]]; then
    step "flash and on-device smoke test"
    port="${PORT:-/dev/cu.usbmodem101}"
    idf.py -p "$port" -b 921600 flash
    uvx --with pyserial --with pillow python tools/smoke.py --port "$port"
fi

step "all checks passed"
