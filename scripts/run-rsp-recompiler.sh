#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RSPRECOMP="$ROOT_DIR/build-tools/n64recomp/RSPRecomp"
ROM="${SBK_ROM:-$ROOT_DIR/../snowboardkids.z64}"
OUTPUT="$ROOT_DIR/rsp/aspMain.cpp"
CONFIG="$ROOT_DIR/build-tools/rsp-release.toml"
if [[ ! -x "$RSPRECOMP" || ! -f "$ROM" ]]; then
    echo "Need RSPRecomp and a local ROM (SBK_ROM). Run bootstrap-n64recomp.sh first." >&2
    exit 1
fi
EXPECTED_SHA1="1583bacc9046a360df8ea4d536942155247e154c"
ACTUAL_SHA1="$(sha1sum "$ROM" | awk '{print $1}')"
if [[ "$ACTUAL_SHA1" != "$EXPECTED_SHA1" ]]; then
    echo "Unsupported ROM SHA-1: $ACTUAL_SHA1 (expected $EXPECTED_SHA1)" >&2
    exit 1
fi
if [[ -L "$OUTPUT" ]]; then
    echo "Refusing symlinked RSP output: $OUTPUT" >&2
    exit 1
fi
mkdir -p "$ROOT_DIR/build-tools"
python3 - "$ROOT_DIR/aspMain.us.toml" "$CONFIG" "$ROM" <<'PY'
from pathlib import Path
import json
import sys
source, output, rom = map(Path, sys.argv[1:])
text = source.read_text().replace('"../snowboardkids.z64"', json.dumps(str(rom.resolve())))
text = text.replace('"rsp/aspMain.cpp"', json.dumps(str((source.parent / 'rsp/aspMain.cpp').absolute())))
output.write_text(text)
PY
cd "$ROOT_DIR"
"$RSPRECOMP" "$CONFIG"
test -s "$OUTPUT"
