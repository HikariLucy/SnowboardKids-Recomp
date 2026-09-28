#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="${SBK_RELEASE_VERSION:-0.9.0-beta}"
ENGINE="${SBK_ENGINE:-$ROOT/build-renderer-stack/SnowboardKidsEngine}"
DATA_ROOT="${XDG_DATA_HOME:-$HOME/.local/share}/SnowboardKids"
MODULE="${SBK_GAME_MODULE:-$DATA_ROOT/modules/snowboardkids-us/SnowboardKidsGame.so}"
OUT_DIR="${SBK_RELEASE_OUT:-$ROOT/dist}"
STAGING="$OUT_DIR/staging"
ASSETS="$STAGING/assets"
ARCHIVE="$OUT_DIR/SnowboardKidsRecompiled-$VERSION-Linux-x86_64.zip"

cd "$ROOT"

if [[ ! -x "$ENGINE" ]]; then
    echo "ERROR: release engine not found or not executable: $ENGINE" >&2
    echo "Build it first with the release branch configuration." >&2
    exit 1
fi
if [[ ! -f "$MODULE" ]]; then
    echo "ERROR: reviewed game module not found: $MODULE" >&2
    exit 1
fi
if [[ -n "$(git status --porcelain)" ]]; then
    echo "ERROR: working tree is not clean; refuse to package an uncommitted release." >&2
    git status --short >&2
    exit 1
fi

HEAD_SHORT="$(git rev-parse --short=12 HEAD)"
VERSION_OUT="$("$ENGINE" --version)"
printf '%s
' "$VERSION_OUT"
if ! grep -Fq "$HEAD_SHORT" <<<"$VERSION_OUT"; then
    echo "ERROR: engine was not built from current HEAD $HEAD_SHORT" >&2
    exit 1
fi
if ! grep -Fq "0.9.0" <<<"$VERSION_OUT"; then
    echo "ERROR: engine does not report project version 0.9.0" >&2
    exit 1
fi

echo "=== VALIDATING GAME MODULE ==="
"$ENGINE" --validate-module "$MODULE"

rm -rf "$STAGING"
mkdir -p "$STAGING" "$OUT_DIR"
python3 scripts/stage_ui_assets.py --out "$ASSETS"

rm -f "$ARCHIVE" "$OUT_DIR/SHA256SUMS.txt"
python3 scripts/package_release.py \
    --binary "$ENGINE" \
    --game-module "$MODULE" \
    --assets "$ASSETS" \
    --version "$VERSION" \
    --commit "$(git rev-parse HEAD)" \
    --platform linux \
    --architecture x86_64 \
    --public-beta \
    --out "$ARCHIVE"

python3 scripts/check_release_readiness.py \
    --assets "$ASSETS" \
    --archive "$ARCHIVE" \
    --public-beta

echo
echo "=== RELEASE ARCHIVE ==="
ls -lh "$ARCHIVE" "$OUT_DIR/SHA256SUMS.txt"
cat "$OUT_DIR/SHA256SUMS.txt"

echo
echo "=== CONTENTS ==="
python3 - "$ARCHIVE" <<'PY'
from pathlib import Path
import sys, zipfile
archive = Path(sys.argv[1])
with zipfile.ZipFile(archive) as z:
    for info in z.infolist():
        print(f"{info.file_size:10d}  {info.filename}")
PY

echo
echo "Release candidate created successfully."
echo "Next: extract this ZIP into a fresh temporary directory and test it before tagging."
