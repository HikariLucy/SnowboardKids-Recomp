#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build-runtime-smoke"
ROM="$ROOT_DIR/../snowboardkids.z64"

if [[ ! -f "$ROM" ]]; then
    echo "ROM not found: $ROM" >&2
    exit 1
fi

if [[ ! -d "$ROOT_DIR/.deps-runtime/N64ModernRuntime" ]]; then
    echo "N64ModernRuntime is missing." >&2
    echo "Run: bash scripts/bootstrap-native-runtime.sh" >&2
    exit 1
fi

if [[ ! -d "$ROOT_DIR/RecompiledFuncs" ]]; then
    echo "RecompiledFuncs is missing." >&2
    echo "Run: bash scripts/run-recompiler.sh" >&2
    exit 1
fi

if [[ ! -f "$ROOT_DIR/rsp/aspMain.cpp" ]]; then
    echo "rsp/aspMain.cpp is missing." >&2
    echo "Run: bash scripts/run-rsp-recompiler.sh" >&2
    exit 1
fi

cmake \
    -S "$ROOT_DIR" \
    -B "$BUILD_DIR" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_C_COMPILER=clang \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DSBK_BUILD_RUNTIME_SMOKE=ON

cmake --build "$BUILD_DIR" --target SnowboardKidsRuntimeSmoke -j "$(nproc)"

echo
echo "Running N64ModernRuntime ROM/GameEntry smoke test..."
"$BUILD_DIR/SnowboardKidsRuntimeSmoke" "$ROM"
