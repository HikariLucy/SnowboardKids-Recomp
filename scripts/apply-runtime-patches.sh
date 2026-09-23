#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RUNTIME_DIR="$ROOT_DIR/.deps-runtime/N64ModernRuntime"
PATCH_FILE="$ROOT_DIR/patches/n64modernruntime-osstopthread.patch"
PINNED_COMMIT="6ccb2e7c2e7f6708257b461097e0aaf03c445e2a"

if [[ ! -d "$RUNTIME_DIR/.git" ]]; then
    echo "N64ModernRuntime is missing." >&2
    echo "Run: bash scripts/bootstrap-native-runtime.sh" >&2
    exit 1
fi

actual_commit="$(git -C "$RUNTIME_DIR" rev-parse HEAD)"
if [[ "$actual_commit" != "$PINNED_COMMIT" ]]; then
    echo "Unexpected N64ModernRuntime revision:" >&2
    echo "  expected: $PINNED_COMMIT" >&2
    echo "  actual:   $actual_commit" >&2
    exit 1
fi

if git -C "$RUNTIME_DIR" apply --reverse --check "$PATCH_FILE" >/dev/null 2>&1; then
    echo "N64ModernRuntime Snowboard Kids thread patch already applied."
    exit 0
fi

git -C "$RUNTIME_DIR" apply --check "$PATCH_FILE"
git -C "$RUNTIME_DIR" apply "$PATCH_FILE"

echo "Applied Snowboard Kids N64ModernRuntime thread compatibility patch."
