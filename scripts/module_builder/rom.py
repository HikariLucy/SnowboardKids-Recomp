"""ROM normalization, format recognition, and cryptographic integrity verification."""

import hashlib
from pathlib import Path
from typing import Tuple

from .errors import RomValidationError

EXPECTED_SHA1 = "1583bacc9046a360df8ea4d536942155247e154c"
EXPECTED_ROM_HASH_HEX = "0xF384619787B78D4B"
EXPECTED_ROM_HASH_U64 = 0xF384619787B78D4B
EXPECTED_ROM_SIZE = 8 * 1024 * 1024  # 8 MiB
EXPECTED_GAME_ID = "snowboardkids.n64.us"


def detect_and_normalize_rom(rom_path: Path) -> Tuple[bytes, str]:
    """
    Read and normalize a Nintendo 64 ROM to Big-Endian (.z64) byte order.
    Returns:
        (normalized_bytes, format_name)
    Raises:
        RomValidationError on invalid format, size, or unreadable file.
    """
    if not rom_path.is_file():
        raise RomValidationError(f"ROM file does not exist or is not a regular file: {rom_path}")

    try:
        data = rom_path.read_bytes()
    except OSError as e:
        raise RomValidationError(f"Cannot read ROM file '{rom_path}': {e}")

    if len(data) < 4:
        raise RomValidationError(f"ROM file is too small ({len(data)} bytes). Expected {EXPECTED_ROM_SIZE} bytes.")

    magic = data[:4]

    # Native Big-Endian (.z64): 0x80371240
    if magic == b'\x80\x37\x12\x40':
        format_name = "Big-Endian (.z64)"
        normalized = data
    # Byte-swapped (.v64): 0x37804012
    elif magic == b'\x37\x80\x40\x12':
        format_name = "Byte-Swapped (.v64)"
        if len(data) % 2 != 0:
            raise RomValidationError(f"Byte-swapped ROM has odd byte length: {len(data)}")
        swapped = bytearray(data)
        for i in range(0, len(data), 2):
            swapped[i], swapped[i + 1] = swapped[i + 1], swapped[i]
        normalized = bytes(swapped)
    # Little-Endian (.n64): 0x40123780
    elif magic == b'\x40\x12\x37\x80':
        format_name = "Little-Endian (.n64)"
        if len(data) % 4 != 0:
            raise RomValidationError(f"Little-endian ROM has non-word-aligned byte length: {len(data)}")
        swapped = bytearray(data)
        for i in range(0, len(data), 4):
            swapped[i], swapped[i + 1], swapped[i + 2], swapped[i + 3] = (
                swapped[i + 3], swapped[i + 2], swapped[i + 1], swapped[i]
            )
        normalized = bytes(swapped)
    else:
        raise RomValidationError(
            f"Unrecognized ROM header magic '0x{magic.hex().upper()}'. "
            "Supported formats: .z64 (0x80371240), .v64 (0x37804012), .n64 (0x40123780)."
        )

    if len(normalized) != EXPECTED_ROM_SIZE:
        raise RomValidationError(
            f"ROM size is {len(normalized)} bytes ({len(normalized) / (1024 * 1024):.2f} MiB), "
            f"expected exactly {EXPECTED_ROM_SIZE} bytes (8 MiB)."
        )

    return normalized, format_name


def validate_rom(rom_path: Path) -> Tuple[bytes, str, str]:
    """
    Validates that the file is the exact supported Snowboard Kids (USA) ROM.
    Returns:
        (normalized_bytes, format_name, sha1_hex)
    Raises:
        RomValidationError on any check failure.
    """
    normalized, format_name = detect_and_normalize_rom(rom_path)
    sha1 = hashlib.sha1(normalized).hexdigest()

    if sha1.lower() != EXPECTED_SHA1.lower():
        raise RomValidationError(
            f"ROM SHA-1 mismatch:\n"
            f"  Calculated: {sha1}\n"
            f"  Expected:   {EXPECTED_SHA1}\n"
            f"The provided ROM is not the supported Snowboard Kids (USA) v1.0 image."
        )

    return normalized, format_name, sha1
