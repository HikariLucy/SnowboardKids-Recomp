#!/usr/bin/env python3
"""Print canonical identity metadata for a user-supplied Snowboard Kids ROM.

The ROM is read and normalized in memory only. No ROM bytes are written,
uploaded, or retained.
"""

import argparse
import hashlib
import json
from pathlib import Path
import sys
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

from module_builder.errors import RomValidationError  # noqa: E402
from module_builder.rom import (  # noqa: E402
    EXPECTED_GAME_ID,
    EXPECTED_ROM_SIZE,
    EXPECTED_SHA1,
    EXPECTED_SHA256,
    detect_and_normalize_rom,
)


def be32(data: bytes, offset: int) -> int:
    return int.from_bytes(data[offset:offset + 4], "big")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rom", type=Path, help="Path to the user's local ROM dump")
    parser.add_argument("--json", action="store_true", dest="as_json",
                        help="Emit one JSON object for launcher/integration tooling")
    args = parser.parse_args()

    try:
        normalized, input_format = detect_and_normalize_rom(args.rom)
    except RomValidationError as exc:
        if args.as_json:
            print(json.dumps({"supported": False, "error": str(exc)}, sort_keys=True))
        else:
            print(f"ERROR: {exc}", file=sys.stderr)
        return 2

    sha256 = hashlib.sha256(normalized).hexdigest()
    sha1 = hashlib.sha1(normalized).hexdigest()
    md5 = hashlib.md5(normalized).hexdigest()
    crc32 = f"{zlib.crc32(normalized) & 0xFFFFFFFF:08x}"
    internal_name = normalized[0x20:0x34].decode("ascii", "replace").rstrip(" \x00")
    game_code = normalized[0x3B:0x3F].decode("ascii", "replace")
    supported = (
        len(normalized) == EXPECTED_ROM_SIZE
        and sha1 == EXPECTED_SHA1
        and sha256 == EXPECTED_SHA256
    )

    result = {
        "supported": supported,
        "game_id": EXPECTED_GAME_ID,
        "input_format": input_format,
        "canonical_byte_order": "big-endian-z64",
        "size": len(normalized),
        "internal_name": internal_name,
        "game_code": game_code,
        "sha256": sha256,
        "sha1": sha1,
        "md5": md5,
        "crc32": crc32,
        "n64_crc1": f"{be32(normalized, 0x10):08x}",
        "n64_crc2": f"{be32(normalized, 0x14):08x}",
        "expected_sha256": EXPECTED_SHA256,
        "expected_sha1": EXPECTED_SHA1,
    }

    if args.as_json:
        print(json.dumps(result, sort_keys=True))
    else:
        print("Snowboard Kids Recompiled — ROM identity")
        print("========================================")
        print(f"Supported      : {'yes' if supported else 'no'}")
        print(f"Input format   : {input_format}")
        print("Canonical form : big-endian Z64")
        print(f"Size           : {len(normalized)} bytes")
        print(f"Internal name  : {internal_name}")
        print(f"Game code      : {game_code}")
        print(f"SHA-256        : {sha256}")
        print(f"SHA-1          : {sha1}")
        print(f"MD5            : {md5}")
        print(f"CRC32          : {crc32}")
        print(f"N64 CRC1       : {result['n64_crc1']}")
        print(f"N64 CRC2       : {result['n64_crc2']}")

    return 0 if supported else 1


if __name__ == "__main__":
    raise SystemExit(main())
