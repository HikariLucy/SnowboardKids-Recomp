#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build-renderer-stack"

if [[ ! -d "$ROOT_DIR/.deps-renderer/rt64" || ! -d "$ROOT_DIR/.deps-renderer/RecompFrontend" ]]; then
    echo "Renderer dependencies are missing." >&2
    echo "Run: bash scripts/bootstrap-renderer-stack.sh" >&2
    exit 1
fi

if [[ ! -d "$ROOT_DIR/.deps-runtime/N64ModernRuntime" ]]; then
    echo "N64ModernRuntime is missing." >&2
    echo "Run: bash scripts/bootstrap-native-runtime.sh" >&2
    exit 1
fi

cmake \
    -S "$ROOT_DIR" \
    -B "$BUILD_DIR" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_C_COMPILER=clang \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DSBK_BUILD_RENDERER_STACK=ON

cmake --build "$BUILD_DIR" --target SnowboardKidsRendererStack -j "$(nproc)"

echo
echo "Renderer/frontend compile validation passed."
