#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build-renderer-stack"
ROM="$ROOT_DIR/../snowboardkids.z64"

if [[ ! -f "$ROM" ]]; then
    echo "ROM not found: $ROM" >&2
    exit 1
fi

if [[ ! -d "$ROOT_DIR/.deps-renderer/recomp-theme/.git" ]]; then
    echo "Shared Snowboard Kids frontend theme is missing; bootstrapping renderer dependencies..."
    bash "$ROOT_DIR/scripts/bootstrap-renderer-stack.sh"
fi

python3 "$ROOT_DIR/scripts/apply-continuation-patches.py"
python3 "$ROOT_DIR/scripts/apply-quiescence-patches.py"

echo "Regenerating N64Recomp output..."
bash "$ROOT_DIR/scripts/run-recompiler.sh"

cmake \
    -S "$ROOT_DIR" \
    -B "$BUILD_DIR" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_C_COMPILER=clang \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DSBK_BUILD_RENDERER_STACK=ON \
    -DSBK_BUILD_NATIVE_BOOT=ON

cmake --build "$BUILD_DIR" --target SnowboardKidsRecompiled -j "$(nproc)"

echo
echo "Native boot executable built:"
echo "  $BUILD_DIR/SnowboardKidsRecompiled"
echo
echo "Launching first native boot attempt..."
echo "Close the SDL window to stop the runtime if it remains open."
echo

cd "$BUILD_DIR"
./SnowboardKidsRecompiled "$ROM"
