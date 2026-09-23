#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEPS_DIR="$ROOT_DIR/.deps-runtime"
RUNTIME_DIR="$DEPS_DIR/N64ModernRuntime"

RUNTIME_REPO="https://github.com/cdlewis/N64ModernRuntime.git"
RUNTIME_COMMIT="6ccb2e7c2e7f6708257b461097e0aaf03c445e2a"

for cmd in git cmake ninja clang clang++; do
    if ! command -v "$cmd" >/dev/null 2>&1; then
        echo "Missing dependency: $cmd" >&2
        echo "On Ubuntu/Linux Mint install:" >&2
        echo "  sudo apt install -y git cmake ninja-build clang" >&2
        exit 1
    fi
done

mkdir -p "$DEPS_DIR"

if [[ ! -d "$RUNTIME_DIR/.git" ]]; then
    git clone --recurse-submodules "$RUNTIME_REPO" "$RUNTIME_DIR"
fi

git -C "$RUNTIME_DIR" fetch origin "$RUNTIME_COMMIT" || git -C "$RUNTIME_DIR" fetch origin
git -C "$RUNTIME_DIR" checkout --detach "$RUNTIME_COMMIT"
git -C "$RUNTIME_DIR" submodule update --init --recursive

echo
echo "N64ModernRuntime ready."
echo "Pinned commit: $RUNTIME_COMMIT"
echo "Path: $RUNTIME_DIR"
