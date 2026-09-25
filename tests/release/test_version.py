import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from generate_version_header import generate_header, get_git_commit


class VersionTest(unittest.TestCase):
    def test_header_generation(self):
        content = generate_header("0.1.0", "abcdef123456")
        self.assertIn('#define SBK_VERSION "0.1.0"', content)
        self.assertIn('#define SBK_COMMIT "abcdef123456"', content)

    def test_dynamic_commit_override_and_cache_invalidation(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            out_file = Path(temp_dir) / "sbk_version.h"

            # Generate with SHA A
            subprocess.run([
                sys.executable, str(ROOT / "scripts/generate_version_header.py"),
                "--out", str(out_file), "--version", "0.1.0", "--commit", "aaaaaaaaaaaa"
            ], check=True)
            text_a = out_file.read_text(encoding="utf-8")
            self.assertIn("aaaaaaaaaaaa", text_a)
            mtime_a = out_file.stat().st_mtime_ns

            # Re-run with same SHA A (idempotence - no touch)
            subprocess.run([
                sys.executable, str(ROOT / "scripts/generate_version_header.py"),
                "--out", str(out_file), "--version", "0.1.0", "--commit", "aaaaaaaaaaaa"
            ], check=True)
            self.assertEqual(out_file.stat().st_mtime_ns, mtime_a)

            # Re-run with SHA B (update file)
            subprocess.run([
                sys.executable, str(ROOT / "scripts/generate_version_header.py"),
                "--out", str(out_file), "--version", "0.1.0", "--commit", "bbbbbbbbbbbb"
            ], check=True)
            text_b = out_file.read_text(encoding="utf-8")
            self.assertIn("bbbbbbbbbbbb", text_b)
            self.assertNotEqual(text_a, text_b)

    def test_fallback_without_git(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            # An empty directory without .git
            old_env = os.environ.pop("SBK_COMMIT", None)
            try:
                commit = get_git_commit(Path(temp_dir))
                self.assertIsInstance(commit, str)
                self.assertTrue(len(commit) > 0)
            finally:
                if old_env is not None:
                    os.environ["SBK_COMMIT"] = old_env


if __name__ == "__main__":
    unittest.main()
