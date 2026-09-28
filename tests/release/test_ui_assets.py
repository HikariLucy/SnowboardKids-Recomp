from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[2]


def font_families(data):
    """Family names (name IDs 1 and 16) from a TrueType name table."""
    count = struct.unpack('>H', data[4:6])[0]
    for i in range(count):
        tag, _, offset, _ = struct.unpack('>4sIII', data[12 + 16 * i:28 + 16 * i])
        if tag != b'name':
            continue
        _, records, strings = struct.unpack('>HHH', data[offset:offset + 6])
        names = set()
        for j in range(records):
            platform, _, _, name_id, length, start = struct.unpack(
                '>HHHHHH', data[offset + 6 + 12 * j:offset + 18 + 12 * j])
            if name_id in (1, 16):
                raw = data[offset + strings + start:offset + strings + start + length]
                names.add(raw.decode('utf-16-be') if platform in (0, 3) else raw.decode('latin-1'))
        return names
    return set()
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
            self.assertEqual((assets / 'app-icon.png').read_bytes(),
                             (ROOT / 'assets/sbk-ui/app-icon.png').read_bytes())

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
            self.assertIn('project_license', dict(passes))
            self.assertIn('recompfrontend_license', dict(blockers))
            with zipfile.ZipFile(archive, 'a') as bundle:
                bundle.writestr('SnowboardKidsRecompiled/assets/icons/unknown.svg', 'stale')
            passes, blockers = run_checks(ROOT, assets=assets, archive=archive)
            self.assertIn('theme_asset_icons', dict(blockers))
            self.assertNotIn('theme_asset_icons', dict(passes))

    def test_theme_font_families_exist_in_staged_fonts(self):
        # RmlUi silently draws nothing for an unknown family; names must match the TTF name table.
        with tempfile.TemporaryDirectory() as tmp:
            assets = Path(tmp) / 'assets'
            self.stage(assets)
            families = {'PromptFont'}
            for font in assets.rglob('*.ttf'):
                families |= font_families(font.read_bytes())
            sources = (ROOT / 'src/ui/recomp_theme.cpp').read_text() + (ROOT / 'src/ui/menu.cpp').read_text()
            used = set(re.findall(r'set_font_family\("([^"]+)"\)', sources))
            used |= set(re.findall(r'register_primary_font\("[^"]+", "([^"]+)"\)', sources))
            rcss = (ROOT / 'assets/sbk-ui/recomp.rcss').read_text()
            used |= {name.strip() for name in re.findall(r'font-family:\s*([^;]+);', rcss)}
            self.assertTrue(used)
            self.assertLessEqual(used, families, f'unknown font families: {sorted(used - families)}')

    def test_stage_refuses_source_destination(self):
        result = subprocess.run([sys.executable, str(ROOT / 'scripts/stage_ui_assets.py'),
                                 '--out', str(ROOT / 'assets/sbk-ui')], capture_output=True)
        self.assertNotEqual(result.returncode, 0)


if __name__ == '__main__':
    unittest.main()
