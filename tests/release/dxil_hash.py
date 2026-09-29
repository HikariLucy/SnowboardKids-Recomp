#!/usr/bin/env python3
"""DXIL container hash check (the "signature" D3D12 verifies).

Port of ComputeHashRetail from DirectXShaderCompiler's open-sourced
lib/DxilHash/DxilHash.cpp (microsoft/DirectXShaderCompiler#6846, tag
v1.8.2505.1 = b106a961d09221b3c5bdb37be45b679257da08b8). A DXIL container
starts with 'DXBC' and a 16-byte digest; the digest covers every byte after
it. dxil.dll (or, since DXC 1.8.2502, dxcompiler's internal validator) writes
the retail hash there; an unvalidated container carries zeros.

    python tests/release/dxil_hash.py shader.dxil [...]   # exit 1 unless all retail-hashed
"""
import struct
import sys

HEADER = b"DXBC"
HASH_START = 20  # magic (4) + digest (16)

_SHIFTS = [7, 12, 17, 22] * 4 + [5, 9, 14, 20] * 4 + [4, 11, 16, 23] * 4 + [6, 10, 15, 21] * 4
_CONSTANTS = [int(abs(__import__("math").sin(i + 1)) * 2 ** 32) & 0xFFFFFFFF for i in range(64)]
_PADDING = b"\x80" + b"\0" * 63


def _rotl(value, shift):
    value &= 0xFFFFFFFF
    return ((value << shift) | (value >> (32 - shift))) & 0xFFFFFFFF


def _compress(state, block):
    x = struct.unpack("<16I", block)
    a, b, c, d = state
    for i in range(64):
        if i < 16:
            f, g = (b & c) | (~b & d), i
        elif i < 32:
            f, g = (b & d) | (c & ~d), (5 * i + 1) % 16
        elif i < 48:
            f, g = b ^ c ^ d, (3 * i + 5) % 16
        else:
            f, g = c ^ (b | ~d & 0xFFFFFFFF), (7 * i) % 16
        f = (f + a + _CONSTANTS[i] + x[g]) & 0xFFFFFFFF
        a, d, c = d, c, b
        b = (b + _rotl(f, _SHIFTS[i])) & 0xFFFFFFFF
    return [(s + v) & 0xFFFFFFFF for s, v in zip(state, (a, b, c, d))]


def retail_hash(data: bytes) -> bytes:
    """ComputeHashRetail: MD5 rounds with DXBC-specific length padding."""
    count = len(data)
    left_over = count & 0x3F
    two_rows = left_over >= 56
    pad_amount = (120 - left_over) if two_rows else (56 - left_over)
    blocks = (count + pad_amount + 8) >> 6
    state = [0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476]
    next_end = blocks - 2 if two_rows else blocks - 1
    for i in range(blocks):
        offset = 64 * i
        if i == next_end:
            if not two_rows and i == blocks - 1:
                block = (struct.pack("<I", (count << 3) & 0xFFFFFFFF) + data[offset:]
                         + _PADDING[:pad_amount])
                block = block[:60] + struct.pack("<I", (1 | (count << 1)) & 0xFFFFFFFF)
            elif i == blocks - 2:
                block = data[offset:] + _PADDING[:pad_amount - 56]
                next_end = blocks - 1
            else:
                block = (struct.pack("<I", (count << 3) & 0xFFFFFFFF)
                         + _PADDING[pad_amount - 56:pad_amount] + struct.pack("<I", (1 | (count << 1)) & 0xFFFFFFFF))
        else:
            block = data[offset:offset + 64]
        state = _compress(state, block)
    return struct.pack("<4I", *state)


def check_container(container: bytes) -> str:
    """'retail' (correctly hashed), 'zero' (unsigned) or 'mismatch'."""
    if container[:4] != HEADER or len(container) < 32:
        return "not-a-container"
    size = struct.unpack_from("<I", container, 24)[0]
    stored = container[4:20]
    if stored == b"\0" * 16:
        return "zero"
    return "retail" if retail_hash(container[HASH_START:size]) == stored else "mismatch"


def main(paths) -> int:
    bad = 0
    for path in paths:
        with open(path, "rb") as handle:
            status = check_container(handle.read())
        print(f"{status}: {path}")
        bad += status != "retail"
    return 1 if bad or not paths else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
