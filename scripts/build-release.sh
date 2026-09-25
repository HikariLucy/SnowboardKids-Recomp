#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
: "${SBK_ROM:?Set SBK_ROM to your legally obtained Snowboard Kids USA ROM path}"
: "${SBK_ELF:?Set SBK_ELF to the matching locally built USA ELF path}"
if [[ ! -f "$SBK_ROM" || ! -f "$SBK_ELF" ]]; then
    echo "SBK_ROM and SBK_ELF must be existing local files." >&2
    exit 1
fi
for path in .deps .deps-runtime .deps-renderer build-tools; do
    if [[ -L "$ROOT_DIR/$path" ]]; then
        echo "Refusing linked dependency/output path: $ROOT_DIR/$path" >&2
        exit 1
    fi
done
for tool in git cmake ninja python3 clang clang++ pkg-config sha1sum; do
    command -v "$tool" >/dev/null || { echo "Missing build tool: $tool" >&2; exit 1; }
done
bash "$ROOT_DIR/scripts/bootstrap-n64recomp.sh"
bash "$ROOT_DIR/scripts/bootstrap.sh" --only runtime --only rt64 --only frontend --only theme
bash "$ROOT_DIR/scripts/build-continuation-generator.sh"
bash "$ROOT_DIR/scripts/run-rsp-recompiler.sh"
python3 "$ROOT_DIR/tests/production_continuation/generate_corpus.py"
cmake -S "$ROOT_DIR" -B "$ROOT_DIR/build-release" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
    -DSBK_BUILD_RENDERER_STACK=ON -DSBK_BUILD_NATIVE_BOOT=ON \
    -DSBK_CONTINUATIONS=ON -DSBK_BUILD_QUIESCENCE_TESTS=ON
cmake --build "$ROOT_DIR/build-release" --target SnowboardKidsRecompiled
ctest --test-dir "$ROOT_DIR/build-release" --output-on-failure
