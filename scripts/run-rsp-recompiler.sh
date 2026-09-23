#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RSPRECOMP="$ROOT_DIR/build-tools/n64recomp/RSPRecomp"
ROM="$ROOT_DIR/../snowboardkids.z64"
CONFIG="$ROOT_DIR/aspMain.us.toml"
OUTPUT="$ROOT_DIR/rsp/aspMain.cpp"

if [[ ! -x "$RSPRECOMP" ]]; then
    echo "RSPRecomp binary not found: $RSPRECOMP" >&2
    echo "Run: bash scripts/bootstrap-n64recomp.sh" >&2
    exit 1
fi

if [[ ! -f "$ROM" ]]; then
    echo "Snowboard Kids ROM not found: $ROM" >&2
    echo "Expected the verified local USA .z64 dump in the sibling workspace." >&2
    exit 1
fi

EXPECTED_SHA1="1583bacc9046a360df8ea4d536942155247e154c"
ACTUAL_SHA1="$(sha1sum "$ROM" | awk '{print $1}')"

if [[ "$ACTUAL_SHA1" != "$EXPECTED_SHA1" ]]; then
    echo "ROM SHA-1 mismatch." >&2
    echo "Expected: $EXPECTED_SHA1" >&2
    echo "Actual:   $ACTUAL_SHA1" >&2
    exit 1
fi

cd "$ROOT_DIR"
mkdir -p rsp
rm -f "$OUTPUT"

"$RSPRECOMP" "$CONFIG"

if [[ ! -s "$OUTPUT" ]]; then
    echo "RSPRecomp did not produce $OUTPUT" >&2
    exit 1
fi

echo
echo "Generated: $OUTPUT"
wc -l "$OUTPUT"
du -h "$OUTPUT"
