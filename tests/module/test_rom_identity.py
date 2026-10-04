"""ROM-free tests for the launcher-facing ROM identity reporter."""

import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / "scripts" / "rom_identity.py"


class RomIdentityToolTests(unittest.TestCase):
    def run_tool(self, data: bytes):
        with tempfile.TemporaryDirectory(prefix="Snowboard Kids Identity ") as directory:
            path = Path(directory) / "candidate.rom"
            path.write_bytes(data)
            return subprocess.run(
                [sys.executable, str(TOOL), "--json", str(path)],
                capture_output=True,
                text=True,
            )

    def test_structurally_valid_wrong_rom_reports_identity_without_copying(self):
        data = b"\x80\x37\x12\x40" + b"\x00" * (8 * 1024 * 1024 - 4)
        result = self.run_tool(data)
        self.assertEqual(result.returncode, 1, result.stderr)
        payload = json.loads(result.stdout)
        self.assertFalse(payload["supported"])
        self.assertEqual(payload["size"], 8 * 1024 * 1024)
        self.assertEqual(payload["canonical_byte_order"], "big-endian-z64")
        self.assertEqual(len(payload["sha256"]), 64)
        self.assertEqual(len(payload["sha1"]), 40)

    def test_invalid_header_is_structural_error(self):
        result = self.run_tool(b"not an n64 rom")
        self.assertEqual(result.returncode, 2)
        payload = json.loads(result.stdout)
        self.assertFalse(payload["supported"])
        self.assertIn("Unrecognized ROM header", payload["error"])


if __name__ == "__main__":
    unittest.main()
