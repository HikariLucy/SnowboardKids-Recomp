import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class PackageTest(unittest.TestCase):
    def test_draft_package_is_deterministic_and_clean(self):
        with tempfile.TemporaryDirectory() as directory:
            temp = Path(directory)
            binary = temp / 'SnowboardKidsRecompiled'
            binary.write_bytes(b'ELF synthetic test binary')
            assets = temp / 'assets'
            assets.mkdir()
            (assets / 'font.ttf').write_bytes(b'synthetic test font')
            archives = [temp / 'A.zip', temp / 'B.zip']
            for archive in archives:
                subprocess.run([sys.executable, str(ROOT / 'scripts/package_release.py'),
                                '--binary', str(binary), '--assets', str(assets),
                                '--out', str(archive), '--draft'], check=True,
                               cwd=temp)
            self.assertEqual(archives[0].read_bytes(), archives[1].read_bytes())
            with zipfile.ZipFile(archives[0]) as bundle:
                names = set(bundle.namelist())
                self.assertIn('SnowboardKidsRecompiled/assets/font.ttf', names)
                self.assertIn('SnowboardKidsRecompiled/BUILD-INFO.txt', names)
                self.assertNotIn('SnowboardKidsRecompiled/runtime-data', names)

    def test_public_package_rejects_license_blocker(self):
        with tempfile.TemporaryDirectory() as directory:
            temp = Path(directory)
            binary = temp / 'SnowboardKidsRecompiled'
            binary.write_bytes(b'synthetic')
            assets = temp / 'assets'
            assets.mkdir()
            result = subprocess.run([sys.executable, str(ROOT / 'scripts/package_release.py'),
                                     '--binary', str(binary), '--assets', str(assets),
                                     '--out', str(temp / 'release.zip')], capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse((temp / 'release.zip').exists())


if __name__ == '__main__':
    unittest.main()
