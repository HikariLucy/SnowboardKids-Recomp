from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from check_release_readiness import run_checks


class UIAssetsTest(unittest.TestCase):
    def stage(self, directory):
        result = subprocess.run([sys.executable, str(ROOT / 'scripts/stage_ui_assets.py'),
                                 '--out', str(directory)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_stage_replaces_stale_tree_and_keeps_owned_bytes(self):
        with tempfile.TemporaryDirectory() as tmp:
            assets = Path(tmp) / 'assets'
            (assets / 'icons').mkdir(parents=True)
            (assets / 'icons/unknown.svg').write_text('stale')
            (assets / 'icons/X.svg').write_text('upstream stale')
            self.stage(assets)
            self.assertFalse((assets / 'icons/unknown.svg').exists())
            icons = list((ROOT / 'assets/sbk-ui/icons').glob('*.svg'))
            self.assertEqual(len(icons), 11)
            for icon in icons:
                self.assertEqual((assets / 'icons' / icon.name).read_bytes(), icon.read_bytes())
            self.assertEqual((assets / 'recomp.rcss').read_bytes(),
                             (ROOT / 'assets/sbk-ui/recomp.rcss').read_bytes())

    def test_package_rejects_stale_and_unknown_icons(self):
        with tempfile.TemporaryDirectory() as tmp:
            temp = Path(tmp)
            assets = temp / 'assets'
            binary = temp / 'SnowboardKidsEngine'
            binary.write_bytes(b'synthetic')
            for relative in ('icons/X.svg', 'icons/unreviewed.svg', 'unexpected.ttf'):
                self.stage(assets)
                (assets / relative).write_bytes(b'unreviewed')
                result = subprocess.run([sys.executable, str(ROOT / 'scripts/package_release.py'),
                                         '--binary', str(binary), '--assets', str(assets),
                                         '--out', str(temp / 'bad.zip'), '--draft'], capture_output=True)
                self.assertNotEqual(result.returncode, 0, relative)
                self.assertFalse((temp / 'bad.zip').exists())

    def test_readiness_requires_staging_and_archive_byte_evidence(self):
        with tempfile.TemporaryDirectory() as tmp:
            temp = Path(tmp)
            assets = temp / 'assets'
            self.stage(assets)
            binary = temp / 'SnowboardKidsEngine'
            binary.write_bytes(b'synthetic')
            archive = temp / 'draft.zip'
            subprocess.run([sys.executable, str(ROOT / 'scripts/package_release.py'),
                            '--binary', str(binary), '--assets', str(assets),
                            '--out', str(archive), '--draft'], check=True, capture_output=True)
            passes, blockers = run_checks(ROOT, assets=assets, archive=archive)
            self.assertIn('theme_asset_icons', dict(passes))
            self.assertIn('project_license', dict(blockers))
            self.assertIn('recompfrontend_license', dict(blockers))
            with zipfile.ZipFile(archive, 'a') as bundle:
                bundle.writestr('SnowboardKidsRecompiled/assets/icons/unknown.svg', 'stale')
            passes, blockers = run_checks(ROOT, assets=assets, archive=archive)
            self.assertIn('theme_asset_icons', dict(blockers))
            self.assertNotIn('theme_asset_icons', dict(passes))

    def test_stage_refuses_source_destination(self):
        result = subprocess.run([sys.executable, str(ROOT / 'scripts/stage_ui_assets.py'),
                                 '--out', str(ROOT / 'assets/sbk-ui')], capture_output=True)
        self.assertNotEqual(result.returncode, 0)


if __name__ == '__main__':
    unittest.main()
