#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RECOMP_DIR="$ROOT_DIR/.deps/N64Recomp"
BUILD_DIR="$ROOT_DIR/build-tools/n64recomp"
OUT_DIR="$ROOT_DIR/build-tools/production-continuation"
python3 "$ROOT_DIR/scripts/apply-continuation-patches.py"
cmake --build "$BUILD_DIR" --target N64Recomp N64RecompElf -j 4
mkdir -p "$OUT_DIR"
"${CXX:-c++}" -std=c++20 -O2 \
    -I"$RECOMP_DIR/include" -I"$RECOMP_DIR/src" \
    -I"$RECOMP_DIR/lib/rabbitizer/include" -I"$RECOMP_DIR/lib/rabbitizer/cplusplus/include" \
    -I"$RECOMP_DIR/lib/fmt/include" -I"$RECOMP_DIR/lib/tomlplusplus/include" \
    "$ROOT_DIR/tools/continuation/generate.cpp" "$RECOMP_DIR/src/config.cpp" \
    "$BUILD_DIR/libN64Recomp.a" "$BUILD_DIR/libN64RecompElf.a" \
    "$BUILD_DIR/libSymbolLists.a" "$BUILD_DIR/librabbitizer.a" "$BUILD_DIR/lib/fmt/libfmt.a" \
    -o "$OUT_DIR/generate"
