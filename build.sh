#!/usr/bin/env bash
# Build Pico firmware and copy amplified_mouse.uf2 to firmware/
set -e
ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"
SDK="${PICO_SDK_PATH:-$ROOT/pico-sdk}"
# Resolve to absolute path so CMake does not resolve relative to build/
[[ "$SDK" != /* ]] && SDK="$ROOT/$SDK"
mkdir -p build
cd build
cmake .. -DPICO_SDK_PATH="$SDK" -DPICOTOOL_FORCE_FETCH_FROM_GIT=ON "$@"
make -j
mkdir -p ../firmware
cp -f amplified_mouse.uf2 ../firmware/
echo "Built and copied to firmware/amplified_mouse.uf2"
