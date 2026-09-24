#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEPS_DIR="$ROOT_DIR/.deps"
SRC_DIR="$DEPS_DIR/N64Recomp"
BUILD_DIR="$ROOT_DIR/build-tools/n64recomp"

N64RECOMP_REPO="https://github.com/N64Recomp/N64Recomp.git"
N64RECOMP_COMMIT="ffb39cdad1da5de07eaaa48bd1db4a89a7986771"

for cmd in git cmake ninja; do
    if ! command -v "$cmd" >/dev/null 2>&1; then
        echo "Missing dependency: $cmd" >&2
        echo "On Ubuntu/Linux Mint install: sudo apt install -y cmake ninja-build git" >&2
        exit 1
    fi
done

mkdir -p "$DEPS_DIR" "$BUILD_DIR"

if [[ ! -d "$SRC_DIR/.git" ]]; then
    git clone --recurse-submodules "$N64RECOMP_REPO" "$SRC_DIR"
    git -C "$SRC_DIR" checkout --detach "$N64RECOMP_COMMIT"
fi

python3 "$ROOT_DIR/scripts/dependency_patches.py" --only recomp
git -C "$SRC_DIR" submodule update --init --recursive

cmake -S "$SRC_DIR" -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR" --target N64RecompCLI RSPRecomp -j "$(nproc)"

echo
echo "N64Recomp toolchain ready."
echo "Pinned commit: $N64RECOMP_COMMIT"
echo "N64Recomp: $BUILD_DIR/N64Recomp"
echo "RSPRecomp: $BUILD_DIR/RSPRecomp"
