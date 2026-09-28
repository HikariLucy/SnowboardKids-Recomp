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
            subprocess.run([sys.executable, str(ROOT / 'scripts/stage_ui_assets.py'),
                            '--out', str(assets)], check=True)
            archives = [temp / 'A.zip', temp / 'B.zip']
            for archive in archives:
                subprocess.run([sys.executable, str(ROOT / 'scripts/package_release.py'),
                                '--binary', str(binary), '--assets', str(assets),
                                '--out', str(archive), '--draft'], check=True,
                               cwd=temp)
            self.assertEqual(archives[0].read_bytes(), archives[1].read_bytes())
            with zipfile.ZipFile(archives[0]) as bundle:
                names = set(bundle.namelist())
                self.assertIn('SnowboardKidsRecompiled/assets/LatoLatin-Regular.ttf', names)
                self.assertIn('SnowboardKidsRecompiled/BUILD-INFO.txt', names)
                self.assertNotIn('SnowboardKidsRecompiled/runtime-data', names)
                manifest = bundle.read('SnowboardKidsRecompiled/BUILD-INFO.txt').decode('utf-8')
                self.assertIn('Dependency-Lock-Digest:', manifest)
                for source in (ROOT / 'assets/sbk-ui/icons').glob('*.svg'):
                    self.assertEqual(bundle.read('SnowboardKidsRecompiled/assets/icons/' + source.name),
                                     source.read_bytes())

    def test_public_beta_requires_module_and_packages_it_canonically(self):
        with tempfile.TemporaryDirectory() as directory:
            temp = Path(directory)
            binary = temp / 'SnowboardKidsEngine'
            binary.write_bytes(b'ELF synthetic engine')
            module = temp / 'SnowboardKidsGame.so'
            module.write_bytes(b'synthetic game module')
            assets = temp / 'assets'
            subprocess.run([sys.executable, str(ROOT / 'scripts/stage_ui_assets.py'),
                            '--out', str(assets)], check=True)

            missing = subprocess.run([
                sys.executable, str(ROOT / 'scripts/package_release.py'),
                '--binary', str(binary), '--assets', str(assets),
                '--out', str(temp / 'missing.zip'), '--public-beta',
            ], capture_output=True, text=True)
            self.assertNotEqual(missing.returncode, 0)
            self.assertIn('--public-beta requires --game-module', missing.stderr)

            archive = temp / 'release.zip'
            subprocess.run([
                sys.executable, str(ROOT / 'scripts/package_release.py'),
                '--binary', str(binary), '--assets', str(assets),
                '--game-module', str(module), '--public-beta',
                '--version', '0.9.0-beta', '--out', str(archive),
            ], check=True)
            with zipfile.ZipFile(archive) as bundle:
                names = set(bundle.namelist())
                self.assertIn('SnowboardKidsRecompiled/modules/snowboardkids-us/SnowboardKidsGame.so', names)
                self.assertIn('SnowboardKidsRecompiled/LICENSE', names)
                self.assertIn('SnowboardKidsRecompiled/SOURCE-COMPLIANCE.md', names)
                self.assertIn('SnowboardKidsRecompiled/BETA-DISTRIBUTION-POLICY.md', names)


if __name__ == '__main__':
    unittest.main()
