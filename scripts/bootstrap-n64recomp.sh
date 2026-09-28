#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
python3 "$ROOT_DIR/scripts/bootstrap.py" --only recomp
cmake -S "$ROOT_DIR/.deps/N64Recomp" -B "$ROOT_DIR/build-tools/n64recomp" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$ROOT_DIR/build-tools/n64recomp" --target N64RecompCLI RSPRecomp
