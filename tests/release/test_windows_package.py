"""ROM-free tests for Windows packaging: PE import discovery, runtime DLL policy,
archive layout/audit and the package-beta-windows.py staging flow.

Synthetic PE32+ images are generated here, so the tests run on every host.
"""
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))

from audit_release_artifact import REDISTRIBUTABLE_DLLS, audit  # noqa: E402
from pe_imports import PeFormatError, read_imports  # noqa: E402
from windows_runtime import REDISTRIBUTABLES, is_system_dll, resolve_runtime  # noqa: E402


def make_pe(imports=(), delay_imports=()):
    """Minimal PE32+ image whose import directories name `imports`/`delay_imports`."""
    section_rva, section_offset = 0x1000, 0x200
    all_names = list(imports) + list(delay_imports)
    descriptors = 20 * (len(imports) + 1)
    delay = 32 * (len(delay_imports) + 1) if delay_imports else 0
    names = bytearray()
    name_rvas = []
    for name in all_names:
        name_rvas.append(section_rva + descriptors + delay + len(names))
        names += name.encode('ascii') + b'\0'
    body = bytearray()
    for i in range(len(imports)):
        body += struct.pack('<IIIII', 0, 0, 0, name_rvas[i], 0)
    body += b'\0' * 20
    for j in range(len(delay_imports)):
        body += struct.pack('<8I', 1, name_rvas[len(imports) + j], 0, 0, 0, 0, 0, 0)
    if delay_imports:
        body += b'\0' * 32
    body += names
    raw_size = (len(body) + 0x1FF) & ~0x1FF

    image = bytearray(section_offset)
    image[0:2] = b'MZ'
    struct.pack_into('<I', image, 0x3C, 0x40)
    image[0x40:0x44] = b'PE\0\0'
    struct.pack_into('<HHIIIHH', image, 0x44, 0x8664, 1, 0, 0, 0, 240, 0x22)
    optional = 0x58
    struct.pack_into('<H', image, optional, 0x20B)
    struct.pack_into('<I', image, optional + 108, 16)
    struct.pack_into('<II', image, optional + 112 + 8 * 1, section_rva, descriptors)
    if delay_imports:
        struct.pack_into('<II', image, optional + 112 + 8 * 13, section_rva + descriptors, delay)
    section = optional + 240
    image[section:section + 8] = b'.idata\0\0'
    struct.pack_into('<IIII', image, section + 8, len(body), section_rva, raw_size, section_offset)
    return bytes(image) + bytes(body) + b'\0' * (raw_size - len(body))


class PeImportTests(unittest.TestCase):
    def test_reads_normal_and_delay_imports(self):
        info = read_imports(make_pe(['KERNEL32.dll', 'SDL2.dll'], ['dxcompiler.dll']))
        self.assertEqual(info['machine'], 0x8664)
        self.assertEqual(info['imports'], ['KERNEL32.dll', 'SDL2.dll'])
        self.assertEqual(info['delay_imports'], ['dxcompiler.dll'])

    def test_rejects_non_pe(self):
        with self.assertRaises(PeFormatError):
            read_imports(b'\x7fELF' + b'\0' * 128)


class RuntimePolicyTests(unittest.TestCase):
    def test_audit_allowlist_matches_runtime_policy(self):
        self.assertEqual(REDISTRIBUTABLE_DLLS, set(REDISTRIBUTABLES))

    def test_system_dlls_are_never_bundled(self):
        for name in ('KERNEL32.dll', 'user32.dll', 'api-ms-win-crt-runtime-l1-1-0.dll', 'd3d12.dll'):
            self.assertTrue(is_system_dll(name), name)
        for name in REDISTRIBUTABLES:
            self.assertFalse(is_system_dll(name), name)

    def test_resolves_real_import_graph(self):
        with tempfile.TemporaryDirectory() as directory:
            d = Path(directory)
            engine = d / 'SnowboardKidsEngine.exe'
            engine.write_bytes(make_pe(['KERNEL32.dll', 'SDL2.dll', 'dxcompiler.dll',
                                        'api-ms-win-crt-runtime-l1-1-0.dll', 'VCRUNTIME140.dll']))
            module = d / 'SnowboardKidsGame.dll'
            module.write_bytes(make_pe(['SnowboardKidsEngine.exe', 'KERNEL32.dll', 'MSVCP140.dll']))
            (d / 'SDL2.dll').write_bytes(make_pe(['KERNEL32.dll', 'USER32.dll']))
            (d / 'dxcompiler.dll').write_bytes(make_pe(['KERNEL32.dll']))
            (d / 'dxil.dll').write_bytes(make_pe(['KERNEL32.dll']))
            (d / 'vcruntime140.dll').write_bytes(make_pe(['KERNEL32.dll']))
            (d / 'msvcp140.dll').write_bytes(make_pe(['vcruntime140.dll']))
            result = resolve_runtime([engine, module], [d])
            self.assertEqual(result.errors, [])
            self.assertEqual(set(result.bundled),
                             {'SDL2.dll', 'dxcompiler.dll', 'dxil.dll', 'vcruntime140.dll', 'msvcp140.dll'})
            self.assertNotIn('snowboardkidsengine.exe', result.system)
            self.assertIn('kernel32.dll', result.system)
            # DXC has no reviewed license text in the pinned tree yet.
            self.assertEqual(result.pending_licenses(), ['dxcompiler.dll', 'dxil.dll'])
            self.assertIn('SDL2', result.license_files(ROOT))

    def test_unknown_and_missing_dlls_fail_closed(self):
        with tempfile.TemporaryDirectory() as directory:
            d = Path(directory)
            engine = d / 'SnowboardKidsEngine.exe'
            engine.write_bytes(make_pe(['KERNEL32.dll', 'mystery.dll', 'SDL2.dll']))
            result = resolve_runtime([engine], [d])
            self.assertEqual(len(result.errors), 2, result.errors)
            self.assertTrue(any('mystery.dll' in e and 'neither' in e for e in result.errors))
            self.assertTrue(any('SDL2.dll' in e and 'not found' in e for e in result.errors))


def stage_assets(directory):
    assets = Path(directory) / 'assets'
    subprocess.run([sys.executable, str(ROOT / 'scripts/stage_ui_assets.py'), '--out', str(assets)], check=True)
    return assets


class WindowsArchiveTests(unittest.TestCase):
    def test_public_windows_layout(self):
        with tempfile.TemporaryDirectory() as directory:
            d = Path(directory)
            assets = stage_assets(d)
            engine = d / 'SnowboardKidsEngine.exe'
            engine.write_bytes(make_pe(['KERNEL32.dll', 'SDL2.dll']))
            module = d / 'SnowboardKidsGame.dll'
            module.write_bytes(make_pe(['SnowboardKidsEngine.exe']))
            sdl = d / 'SDL2.dll'
            sdl.write_bytes(make_pe(['KERNEL32.dll']))
            archive = d / 'SnowboardKidsRecompiled-0.9.0-dev-Windows-x86_64.zip'
            subprocess.run([sys.executable, str(ROOT / 'scripts/package_release.py'),
                            '--binary', str(engine), '--assets', str(assets), '--game-module', str(module),
                            '--library', str(sdl), '--license', f'SDL2={ROOT / "LICENSE"}',
                            '--public-beta', '--platform', 'windows', '--architecture', 'x86_64',
                            '--version', '0.9.0-dev', '--out', str(archive)], check=True)
            with zipfile.ZipFile(archive) as bundle:
                names = set(bundle.namelist())
                info = bundle.read('SnowboardKidsRecompiled/BUILD-INFO.txt').decode()
            for required in ('SnowboardKidsEngine.exe', 'SDL2.dll', 'licenses/SDL2.txt', 'LICENSE',
                             'modules/snowboardkids-us/SnowboardKidsGame.dll', 'RUNNING.md',
                             'SOURCE-COMPLIANCE.md', 'THIRD_PARTY_NOTICES.md', 'BUILD-INFO.txt',
                             'BETA-DISTRIBUTION-POLICY.md'):
                self.assertIn('SnowboardKidsRecompiled/' + required, names)
            self.assertIn('Platform: windows', info)
            self.assertIn('Architecture: x86_64', info)
            self.assertIn('Version: 0.9.0-dev', info)
            self.assertEqual(audit(archive), [])

    def _audit(self, entries):
        with tempfile.TemporaryDirectory() as directory:
            archive = Path(directory) / 'a.zip'
            base = {'SnowboardKidsEngine.exe': b'MZ', 'BUILD-INFO.txt': b'x', 'RUNNING.md': b'x',
                    'THIRD_PARTY_NOTICES.md': b'x', 'LICENSE': b'x', 'SOURCE-COMPLIANCE.md': b'x'}
            base.update(entries)
            with zipfile.ZipFile(archive, 'w') as bundle:
                for name, data in base.items():
                    bundle.writestr('SnowboardKidsRecompiled/' + name, data)
            return audit(archive)

    def test_audit_rejects_windows_prohibited_content(self):
        self.assertEqual(self._audit({'SDL2.dll': b'MZ'}), [])
        cases = {
            'unreviewed.dll': 'unreviewed runtime DLL',
            'plugins/SDL2.dll': 'unreviewed runtime DLL',
            'SnowboardKidsGame.dll': 'game module outside canonical release path',
            'SnowboardKidsEngine.pdb': 'forbidden file',
            'SnowboardKidsEngine.lib': 'forbidden file',
            'SnowboardKidsEngine.exp': 'forbidden file',
            'saves/snowboardkids.sbks': 'forbidden file',
            'Controller Pak 1.mpk': 'forbidden file',
            'portable.txt': 'forbidden file',
            'snowboardkids.z64': 'forbidden file',
        }
        for name, expected in cases.items():
            errors = self._audit({name: b'MZ'})
            self.assertTrue(any(expected in e for e in errors), (name, errors))
        errors = self._audit({'notes.txt': b'C:\\Users\\someone\\rom.z64'})
        self.assertTrue(any('personal absolute path' in e for e in errors), errors)
        errors = self._audit({'dump.bin': b'\x80\x37\x12\x40' + b'\0' * 64})
        self.assertTrue(any('ROM header' in e for e in errors), errors)


class PackageScriptTests(unittest.TestCase):
    SCRIPT = ROOT / 'scripts' / 'package-beta-windows.py'

    def test_engine_only_draft_staging(self):
        with tempfile.TemporaryDirectory() as directory:
            d = Path(directory)
            build = d / 'build'
            build.mkdir()
            engine = build / 'SnowboardKidsEngine.exe'
            engine.write_bytes(make_pe(['KERNEL32.dll', 'SDL2.dll']))
            (build / 'SDL2.dll').write_bytes(make_pe(['KERNEL32.dll']))
            (build / 'SnowboardKidsEngine.lib').write_bytes(b'import library, never shipped')
            out = d / 'dist'
            result = subprocess.run([sys.executable, str(self.SCRIPT), '--engine', str(engine),
                                     '--engine-only-draft', '--no-run', '--out-dir', str(out)],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            archives = list(out.glob('SnowboardKidsRecompiled-*-Windows-x86_64-engine-draft.zip'))
            self.assertEqual(len(archives), 1)
            with zipfile.ZipFile(archives[0]) as bundle:
                names = set(bundle.namelist())
            self.assertIn('SnowboardKidsRecompiled/SnowboardKidsEngine.exe', names)
            self.assertIn('SnowboardKidsRecompiled/SDL2.dll', names)
            self.assertNotIn('SnowboardKidsRecompiled/SnowboardKidsEngine.lib', names)
            self.assertFalse(any('/modules/' in n for n in names))
            self.assertEqual(audit(archives[0]), [])

    def test_release_mode_requires_running_engine_and_module(self):
        with tempfile.TemporaryDirectory() as directory:
            engine = Path(directory) / 'SnowboardKidsEngine.exe'
            engine.write_bytes(make_pe(['KERNEL32.dll']))
            result = subprocess.run([sys.executable, str(self.SCRIPT), '--engine', str(engine), '--no-run'],
                                    capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('--no-run is only allowed', result.stderr)
            env = {**os.environ, 'APPDATA': directory}
            result = subprocess.run([sys.executable, str(self.SCRIPT), '--engine', str(engine)],
                                    capture_output=True, text=True, env=env)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('reviewed game module not found', result.stderr)

    def test_unknown_runtime_dll_blocks_packaging(self):
        with tempfile.TemporaryDirectory() as directory:
            engine = Path(directory) / 'SnowboardKidsEngine.exe'
            engine.write_bytes(make_pe(['KERNEL32.dll', 'libgcc_s_seh-1.dll']))
            result = subprocess.run([sys.executable, str(self.SCRIPT), '--engine', str(engine),
                                     '--engine-only-draft', '--no-run', '--out-dir', str(Path(directory) / 'o')],
                                    capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('libgcc_s_seh-1.dll', result.stderr)


class WindowsUserDataTests(unittest.TestCase):
    def test_builder_installs_into_appdata_module_dir(self):
        sys.path.insert(0, str(ROOT / 'scripts'))
        from module_builder import service
        saved = (sys.platform, os.environ.get('APPDATA'), os.environ.pop('SBK_USER_DATA_DIR', None))
        try:
            service.sys.platform = 'win32'
            os.environ['APPDATA'] = str(Path('C:/Users/Player/AppData/Roaming'))
            self.assertEqual(service.default_module_filename(), 'SnowboardKidsGame.dll')
            self.assertEqual(service.default_user_module_dir(ROOT),
                             Path(os.environ['APPDATA']) / 'SnowboardKids' / 'modules' / 'snowboardkids-us')
        finally:
            service.sys.platform = saved[0]
            if saved[1] is None:
                os.environ.pop('APPDATA', None)
            else:
                os.environ['APPDATA'] = saved[1]
            if saved[2] is not None:
                os.environ['SBK_USER_DATA_DIR'] = saved[2]

    def test_engine_uses_appdata_and_dll_module_name(self):
        boot = (ROOT / 'src/main/native_boot.cpp').read_text()
        self.assertIn('std::getenv("APPDATA")', boot)
        self.assertIn('/ "SnowboardKids"', boot)
        loader = (ROOT / 'src/module/module_loader.cpp').read_text()
        self.assertIn('"SnowboardKidsGame.dll"', loader)
        self.assertIn('"modules" / "snowboardkids-us" / mod_name', loader)


if __name__ == '__main__':
    unittest.main()
