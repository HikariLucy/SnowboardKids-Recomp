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

import hashlib  # noqa: E402

import dxc_redist  # noqa: E402
from audit_release_artifact import REDISTRIBUTABLE_DLLS, REQUIRED_NOTICES, audit  # noqa: E402
from pe_imports import PeFormatError, read_imports  # noqa: E402
from windows_runtime import (REDISTRIBUTABLES, RuntimeResolution, is_system_dll,  # noqa: E402
                             resolve_runtime, runtime_manifest)


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
        # msvcrt.dll is the OS C runtime the pinned SDL2.dll links (seen in CI).
        for name in ('KERNEL32.dll', 'user32.dll', 'api-ms-win-crt-runtime-l1-1-0.dll', 'd3d12.dll',
                     'msvcrt.dll', 'D3DCOMPILER_47.dll'):
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
            self.assertEqual(result.missing_license_texts(ROOT) == [],
                             (ROOT / REDISTRIBUTABLES['sdl2.dll'].licenses[0][1]).is_file())
            # dxcompiler.dll is cleared by its vendored texts; dxil.dll waits
            # for the recorded maintainer decision on Microsoft's terms.
            pending = result.pending_licenses(ROOT)
            if dxc_redist.dxil_redistribution_accepted(ROOT):
                self.assertEqual(pending, [])
            else:
                self.assertEqual(len(pending), 1)
                self.assertTrue(pending[0].startswith('dxil.dll'), pending)
            files = result.license_files(ROOT)
            self.assertEqual(set(files), {'SDL2', *dxc_redist.DLLS['dxcompiler.dll']['notices'],
                                          *dxc_redist.DLLS['dxil.dll']['notices']})
            # Synthetic stand-ins are not the pinned Microsoft binaries.
            errors = result.pinned_hash_errors()
            self.assertEqual(sorted(e.split()[0] for e in errors), ['dxcompiler.dll', 'dxil.dll'])

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
            manifest = d / 'RUNTIME-DLLS.txt'
            manifest.write_text(runtime_manifest(RuntimeResolution({'SDL2.dll': sdl}, ['kernel32.dll'], [])))
            archive = d / 'SnowboardKidsRecompiled-0.9.0-dev-Windows-x86_64.zip'
            subprocess.run([sys.executable, str(ROOT / 'scripts/package_release.py'),
                            '--binary', str(engine), '--assets', str(assets), '--game-module', str(module),
                            '--library', str(sdl), '--license', f'SDL2={ROOT / "LICENSE"}',
                            '--runtime-manifest', str(manifest),
                            '--public-beta', '--platform', 'windows', '--architecture', 'x86_64',
                            '--version', '0.9.0-dev', '--out', str(archive)], check=True)
            with zipfile.ZipFile(archive) as bundle:
                names = set(bundle.namelist())
                info = bundle.read('SnowboardKidsRecompiled/BUILD-INFO.txt').decode()
                listed = bundle.read('SnowboardKidsRecompiled/RUNTIME-DLLS.txt').decode()
            for required in ('SnowboardKidsEngine.exe', 'SDL2.dll', 'licenses/SDL2.txt', 'LICENSE',
                             'modules/snowboardkids-us/SnowboardKidsGame.dll', 'RUNNING.md',
                             'SOURCE-COMPLIANCE.md', 'THIRD_PARTY_NOTICES.md', 'BUILD-INFO.txt',
                             'BETA-DISTRIBUTION-POLICY.md', 'RUNTIME-DLLS.txt'):
                self.assertIn('SnowboardKidsRecompiled/' + required, names)
            self.assertIn('Platform: windows', info)
            self.assertIn('Architecture: x86_64', info)
            self.assertIn('Version: 0.9.0-dev', info)
            self.assertIn('Package: public-beta', info)
            self.assertIn('Runtime-DLLs: RUNTIME-DLLS.txt', info)
            self.assertIn(f'sha256: {hashlib.sha256(sdl.read_bytes()).hexdigest()}', listed)
            self.assertIn('[system] kernel32.dll', listed)
            self.assertEqual(audit(archive), [])
            sums = (d / 'SHA256SUMS.txt').read_text()
            self.assertEqual(sums, f'{hashlib.sha256(archive.read_bytes()).hexdigest()}  {archive.name}\n')

    def _audit(self, entries, manifest=True):
        with tempfile.TemporaryDirectory() as directory:
            archive = Path(directory) / 'a.zip'
            base = {'SnowboardKidsEngine.exe': b'MZ', 'BUILD-INFO.txt': b'x', 'RUNNING.md': b'x',
                    'THIRD_PARTY_NOTICES.md': b'x', 'LICENSE': b'x', 'SOURCE-COMPLIANCE.md': b'x'}
            base.update(entries)
            if manifest:  # describe exactly the top-level DLLs, as the packager does
                text = ''.join(f'[bundled] {n}\nsha256: {hashlib.sha256(data).hexdigest()}\n\n'
                               for n, data in base.items() if '/' not in n and n.lower().endswith('.dll'))
                base.setdefault('RUNTIME-DLLS.txt', text.encode())
            with zipfile.ZipFile(archive, 'w') as bundle:
                for name, data in base.items():
                    bundle.writestr('SnowboardKidsRecompiled/' + name, data)
            return audit(archive)

    def test_audit_rejects_windows_prohibited_content(self):
        self.assertEqual(self._audit({'SDL2.dll': b'MZ', 'licenses/SDL2.txt': b'zlib'}), [])
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


class RedistributionGateTests(unittest.TestCase):
    """DXC provenance and notices: the audit, the pins and the vendored texts."""

    DXC_NOTICES = {name: [f'licenses/{n}.txt' for n in entry['notices']]
                   for name, entry in dxc_redist.DLLS.items()}

    def audit_with(self, entries, manifest=True):
        return WindowsArchiveTests._audit(WindowsArchiveTests(), entries, manifest)

    def test_audit_notice_table_matches_runtime_policy(self):
        for name, entry in REDISTRIBUTABLES.items():
            self.assertEqual(set(REQUIRED_NOTICES.get(name, ())), {n for n, _ in entry.licenses}, name)

    def test_dxc_dlls_require_their_notices(self):
        for dll, notices in self.DXC_NOTICES.items():
            complete = {dll: b'MZ', **{n: b'text' for n in notices}}
            self.assertEqual(self.audit_with(complete), [], dll)
            for missing in notices:
                entries = {k: v for k, v in complete.items() if k != missing}
                errors = self.audit_with(entries)
                self.assertTrue(any(f'{dll} bundled without {missing}' in e for e in errors), errors)

    def test_runtime_manifest_must_match_archive(self):
        entries = {'SDL2.dll': b'MZ sdl', 'licenses/SDL2.txt': b'zlib'}
        self.assertIn('missing runtime DLL manifest (RUNTIME-DLLS.txt)',
                      self.audit_with(entries, manifest=False))
        wrong = b'[bundled] SDL2.dll\nsha256: ' + b'0' * 64 + b'\n'
        errors = self.audit_with({**entries, 'RUNTIME-DLLS.txt': wrong})
        self.assertTrue(any('does not match the archived file' in e for e in errors), errors)
        extra = b'[bundled] dxil.dll\nsha256: 00\n\n[bundled] SDL2.dll\nsha256: ' + \
            hashlib.sha256(b'MZ sdl').hexdigest().encode() + b'\n'
        errors = self.audit_with({**entries, 'RUNTIME-DLLS.txt': extra})
        self.assertEqual(errors, ['RUNTIME-DLLS.txt lists dxil.dll, which is not in the archive'])
        missing = b'[system] kernel32.dll\n'
        errors = self.audit_with({**entries, 'RUNTIME-DLLS.txt': missing})
        self.assertIn('SDL2.dll is not described in RUNTIME-DLLS.txt', errors)

    def test_manifest_is_deterministic_and_path_free(self):
        with tempfile.TemporaryDirectory(prefix='Users ') as directory:
            d = Path(directory)
            sdl = d / 'SDL2.dll'
            sdl.write_bytes(make_pe(['KERNEL32.dll']))
            dxc = d / 'dxcompiler.dll'
            dxc.write_bytes(make_pe(['KERNEL32.dll']))
            resolution = RuntimeResolution({'dxcompiler.dll': dxc, 'SDL2.dll': sdl},
                                           ['user32.dll', 'kernel32.dll'], [])
            text = runtime_manifest(resolution)
            self.assertEqual(text, runtime_manifest(RuntimeResolution(
                {'SDL2.dll': sdl, 'dxcompiler.dll': dxc}, ['user32.dll', 'kernel32.dll'], [])))
            self.assertNotIn(str(d), text)
            self.assertNotIn('\\', text)
            self.assertIn('version: 1.7.2308.7', text)
            self.assertIn(dxc_redist.ARCHIVE_URL, text)
            self.assertIn('licenses: licenses/DirectXShaderCompiler-LICENSE-LLVM.txt', text)
            self.assertIn('evidence: text', text)

    def test_vendored_license_texts_match_pins(self):
        self.assertEqual(dxc_redist.verify_license_texts(ROOT), [])
        # README: LICENSE-LLVM.txt applies to dxcompiler.dll, LICENSE-MS.txt to dxil.dll.
        self.assertIn('LICENSE-LLVM.txt', dxc_redist.DLLS['dxcompiler.dll']['notices'].values())
        self.assertEqual(list(dxc_redist.DLLS['dxil.dll']['notices'].values()), ['LICENSE-MS.txt'])

    def test_missing_or_edited_license_text_blocks(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            errors = dxc_redist.verify_license_texts(root)
            self.assertEqual(len(errors), len(dxc_redist.LICENSE_TEXTS))
            target = root / dxc_redist.LICENSE_DIR
            target.mkdir(parents=True)
            for name in dxc_redist.LICENSE_TEXTS:
                (target / name).write_bytes((ROOT / dxc_redist.LICENSE_DIR / name).read_bytes())
            self.assertEqual(dxc_redist.verify_license_texts(root), [])
            (target / 'LICENSE-MS.txt').write_bytes(b'summarised terms')
            self.assertEqual(len(dxc_redist.verify_license_texts(root)), 1)

    def test_dxil_decision_must_be_explicitly_accepted(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.assertFalse(dxc_redist.dxil_redistribution_accepted(root))
            record = root / dxc_redist.DXIL_DECISION
            record.parent.mkdir(parents=True)
            record.write_text('Decision: PENDING\nDecision: ACCEPTED is what it would say\n')
            self.assertFalse(dxc_redist.dxil_redistribution_accepted(root))
            for placeholder in ('Decision: ACCEPTED (<maintainer>, <YYYY-MM-DD>)\n',
                                'Decision: ACCEPTED\n', 'Decision: ACCEPTED (maintainer)\n'):
                record.write_text(placeholder)
                self.assertFalse(dxc_redist.dxil_redistribution_accepted(root), placeholder)
            record.write_text('Decision: ACCEPTED (maintainer, 2026-01-01)\n')
            self.assertTrue(dxc_redist.dxil_redistribution_accepted(root))

    def test_rt64_contrib_dxc_is_not_the_pinned_release(self):
        # rt64/dxc-bin@cc15e715 ships an unsigned dxcompiler.dll matching no release.
        contrib = ROOT / '.deps-renderer/rt64/src/contrib/dxc/bin/x64/dxcompiler.dll'
        if not contrib.is_file():
            self.skipTest('RT64 not bootstrapped')
        from windows_runtime import sha256_file
        self.assertNotEqual(sha256_file(contrib), dxc_redist.FILES['bin/x64/dxcompiler.dll'])
        self.assertTrue(dxc_redist.verify_dll(contrib))

    def test_cmake_copies_the_pinned_release(self):
        cmake = (ROOT / 'CMakeLists.txt').read_text()
        self.assertIn(f'.deps-renderer/dxc-redist/{dxc_redist.RELEASE_TAG}/bin/x64', cmake)
        self.assertNotIn('src/contrib/dxc/bin/x64/dxcompiler.dll', cmake)

    def test_installed_release_matches_pins_and_versions(self):
        if dxc_redist.verify_install(ROOT):
            self.skipTest('DXC release not bootstrapped (python scripts/bootstrap.py --only dxc)')
        from windows_runtime import pe_file_version
        for name, entry in dxc_redist.DLLS.items():
            path = dxc_redist.dll_dir(ROOT) / name
            self.assertEqual(dxc_redist.verify_dll(path), [])
            self.assertEqual(pe_file_version(path), entry['version'])


class ReadinessRuntimeTests(unittest.TestCase):
    def archive(self, directory, entries):
        path = Path(directory) / 'a.zip'
        with zipfile.ZipFile(path, 'w') as bundle:
            for name, data in {'SnowboardKidsEngine.exe': b'MZ', **entries}.items():
                bundle.writestr('SnowboardKidsRecompiled/' + name, data)
        return path

    def test_windows_archive_dxc_gates(self):
        from check_release_readiness import check_windows_runtime
        with tempfile.TemporaryDirectory() as directory:
            name, detail = check_windows_runtime(self.archive(directory, {}))
            self.assertTrue(detail.startswith('ok:'), detail)
            name, detail = check_windows_runtime(self.archive(directory, {'dxcompiler.dll': b'MZ unsigned'}))
            self.assertEqual(name, 'windows_runtime_redistribution')
            self.assertIn('dxcompiler.dll is not the pinned', detail)
            name, detail = check_windows_runtime(self.archive(directory, {'dxil.dll': b'MZ'}))
            self.assertIn('dxil.dll is not the pinned', detail)
            if not dxc_redist.dxil_redistribution_accepted(ROOT):
                self.assertIn('decision is not ACCEPTED', detail)

    def test_linux_archive_is_not_checked(self):
        from check_release_readiness import check_windows_runtime
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'l.zip'
            with zipfile.ZipFile(path, 'w') as bundle:
                bundle.writestr('SnowboardKidsRecompiled/SnowboardKidsEngine', b'ELF')
            self.assertEqual(check_windows_runtime(path), (None, ''))


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
                info = bundle.read('SnowboardKidsRecompiled/BUILD-INFO.txt').decode()
                listed = bundle.read('SnowboardKidsRecompiled/RUNTIME-DLLS.txt').decode()
            self.assertIn('SnowboardKidsRecompiled/SnowboardKidsEngine.exe', names)
            if (ROOT / REDISTRIBUTABLES['sdl2.dll'].licenses[0][1]).is_file():
                self.assertIn('SnowboardKidsRecompiled/SDL2.dll', names)
                self.assertIn('SnowboardKidsRecompiled/licenses/SDL2.txt', names)
                self.assertIn('[bundled] SDL2.dll', listed)
            else:  # RT64 not bootstrapped (ROM-free CI): never ship SDL2 without its notice
                self.assertNotIn('SnowboardKidsRecompiled/SDL2.dll', names)
                self.assertIn('left out of the draft: SDL2.dll', result.stdout)
            self.assertNotIn('SnowboardKidsRecompiled/SnowboardKidsEngine.lib', names)
            self.assertFalse(any('/modules/' in n for n in names))
            # A draft identifies itself inside the archive, not only by file name.
            self.assertIn('Package: draft (not a release candidate; do not distribute)', info)
            self.assertNotIn('public-beta', info)
            self.assertNotIn(str(d), listed + info)
            self.assertEqual(audit(archives[0]), [])
            sums = (out / 'SHA256SUMS.txt').read_text()
            self.assertEqual(sums, f'{hashlib.sha256(archives[0].read_bytes()).hexdigest()}  {archives[0].name}\n')
            self.assertIn('not release candidates', result.stdout)

    def test_unpinned_dxc_is_refused_even_in_a_draft(self):
        with tempfile.TemporaryDirectory() as directory:
            d = Path(directory)
            engine = d / 'SnowboardKidsEngine.exe'
            engine.write_bytes(make_pe(['KERNEL32.dll', 'dxcompiler.dll']))
            (d / 'dxcompiler.dll').write_bytes(make_pe(['KERNEL32.dll']))  # e.g. RT64's unsigned copy
            (d / 'dxil.dll').write_bytes(make_pe(['KERNEL32.dll']))
            result = subprocess.run([sys.executable, str(self.SCRIPT), '--engine', str(engine),
                                     '--engine-only-draft', '--no-run', '--out-dir', str(d / 'o')],
                                    capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('is not the pinned Microsoft DirectXShaderCompiler v1.7.2308', result.stderr)
            self.assertFalse(list((d / 'o').glob('*.zip')) if (d / 'o').exists() else [])

    def test_missing_license_text_withholds_in_draft_and_blocks_public(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            sdl = root / 'SDL2.dll'
            sdl.write_bytes(make_pe(['KERNEL32.dll']))
            resolution = RuntimeResolution({'SDL2.dll': sdl}, [], [])
            missing = resolution.missing_license_texts(root)  # empty root: nothing bootstrapped
            self.assertEqual(len(missing), 1)
            self.assertTrue(missing[0].startswith('SDL2.dll '), missing)

    def test_draft_and_public_beta_are_exclusive(self):
        result = subprocess.run([sys.executable, str(ROOT / 'scripts/package_release.py'), '--binary', 'x',
                                 '--assets', 'x', '--out', 'x.zip', '--draft', '--public-beta'],
                                capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('mutually exclusive', result.stderr)

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
