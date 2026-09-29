"""The DXIL retail-hash port must match DirectXShaderCompiler's own code.

Expected digests were produced by compiling lib/DxilHash/DxilHash.cpp from
microsoft/DirectXShaderCompiler@b106a961 (v1.8.2505.1) and hashing the same
inputs; the lengths cover both padding branches and block boundaries.
"""
import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from dxil_hash import HASH_START, check_container, retail_hash  # noqa: E402

REFERENCE = {
    0: "140d60f6b775e2ba4e4abed401b2e9a1",
    1: "2c0faf2f1e272c8285c0dd537f95097a",
    55: "6f1fd0a7d12099693fc16d6c45c28f64",
    56: "20ec0f395e3a535afa0ec7ff65c2a16c",
    63: "c0edec77c67afbd4fc95cb4c30f69e2c",
    64: "1d75e7085da262ff1baaf8a651673d6b",
    119: "6842a3826a07dd998048e7b0d509a503",
    120: "8246f418c90917b464222ddd2fe797eb",
    200: "ff9f83c1f0aa74184b3aa68a531a43e1",
}


def sample(n):
    return bytes((i * 31 + 7) & 0xFF for i in range(n))


class DxilHashTests(unittest.TestCase):
    def test_matches_dxc_reference_vectors(self):
        for n, digest in REFERENCE.items():
            self.assertEqual(retail_hash(sample(n)).hex(), digest, n)

    def container(self, payload, digest=None):
        body = struct.pack("<HHII", 1, 0, 0, 0) + payload  # version, size (patched), part count
        size = HASH_START + len(body)
        body = body[:4] + struct.pack("<I", size) + body[8:]
        digest = retail_hash(body) if digest is None else digest
        return b"DXBC" + digest + body

    def test_container_states(self):
        good = self.container(sample(100))
        self.assertEqual(check_container(good), "retail")
        self.assertEqual(check_container(self.container(sample(100), b"\0" * 16)), "zero")
        tampered = bytearray(good)
        tampered[-1] ^= 1
        self.assertEqual(check_container(bytes(tampered)), "mismatch")
        self.assertEqual(check_container(b"\x03\x02\x23\x07" + b"\0" * 40), "not-a-container")


if __name__ == "__main__":
    unittest.main()
