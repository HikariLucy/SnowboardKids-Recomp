import tempfile
import unittest
import zipfile
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts'))
from audit_release_artifact import audit


class ArtifactAuditTest(unittest.TestCase):
    def check(self, extra, expected=None):
        with tempfile.TemporaryDirectory() as directory:
            archive = Path(directory) / 'test.zip'
            entries = {
                'SnowboardKidsRecompiled/SnowboardKidsRecompiled': b'ELF safe',
                'SnowboardKidsRecompiled/BUILD-INFO.txt': b'Project: Snowboard Kids Recompiled\nVersion: 0.1.0\nCommit: e430e84c8bad\nPlatform: linux\nArchitecture: x86_64\n',
                'SnowboardKidsRecompiled/RUNNING.md': b'Run it',
                'SnowboardKidsRecompiled/THIRD_PARTY_NOTICES.md': b'Notices',
            }
            entries.update(extra)
            with zipfile.ZipFile(archive, 'w') as bundle:
                for name, data in entries.items():
                    bundle.writestr(name, data)
            result = audit(archive)
            if expected:
                self.assertTrue(any(expected in error for error in result), result)
            else:
                self.assertEqual(result, [])

    def test_clean(self):
        self.check({})

    def test_missing_manifest(self):
        with tempfile.TemporaryDirectory() as directory:
            archive = Path(directory) / 'test.zip'
            with zipfile.ZipFile(archive, 'w') as bundle:
                bundle.writestr('SnowboardKidsRecompiled/SnowboardKidsRecompiled', b'ELF safe')
                bundle.writestr('SnowboardKidsRecompiled/RUNNING.md', b'Run it')
                bundle.writestr('SnowboardKidsRecompiled/THIRD_PARTY_NOTICES.md', b'Notices')
            result = audit(archive)
            self.assertTrue(any('missing build manifest' in error for error in result))

    def test_rom_extension_and_header(self):
        self.check({'SnowboardKidsRecompiled/rom.z64': b'X'}, 'forbidden file')
        self.check({'SnowboardKidsRecompiled/assets/picture.png': b'\x80\x37\x12\x40'}, 'ROM header')

    def test_save_and_config(self):
        self.check({'SnowboardKidsRecompiled/save.mpk': b'X'}, 'forbidden file')
        self.check({'SnowboardKidsRecompiled/controls.json': b'{}'}, 'forbidden file')

    def test_developer_path_and_build_junk(self):
        self.check({'SnowboardKidsRecompiled/SnowboardKidsRecompiled': b'/home/alice/build'}, 'personal absolute path')
        self.check({'SnowboardKidsRecompiled/build.ninja': b'X'}, 'forbidden file')

    def test_traversal(self):
        self.check({'SnowboardKidsRecompiled/../bad': b'X'}, 'unsafe layout')


if __name__ == '__main__':
    unittest.main()
