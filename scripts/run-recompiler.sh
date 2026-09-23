#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
N64RECOMP="$ROOT_DIR/build-tools/n64recomp/N64Recomp"
ELF="$ROOT_DIR/../snowboardkids-decomp/build/snowboardkids.elf"
CONFIG="$ROOT_DIR/us.toml"

if [[ ! -x "$N64RECOMP" ]]; then
    echo "N64Recomp binary not found: $N64RECOMP" >&2
    echo "Run: bash scripts/bootstrap-n64recomp.sh" >&2
    exit 1
fi

if [[ ! -f "$ELF" ]]; then
    echo "Matching Snowboard Kids ELF not found: $ELF" >&2
    echo "Build the sibling snowboardkids-decomp repository first." >&2
    exit 1
fi

cd "$ROOT_DIR"

if [[ "${1:-}" == "--dump-context" ]]; then
    exec "$N64RECOMP" "$CONFIG" --dump-context
elif [[ $# -gt 0 ]]; then
    echo "Usage: bash scripts/run-recompiler.sh [--dump-context]" >&2
    exit 2
else
    exec "$N64RECOMP" "$CONFIG"
fi
