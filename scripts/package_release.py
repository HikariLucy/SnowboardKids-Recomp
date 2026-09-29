#!/usr/bin/env python3
"""Stage only explicit distributable inputs in a deterministic zip."""
import argparse
import hashlib
from pathlib import Path
import stat
import sys
import zipfile

from audit_release_artifact import audit

ROOT = Path(__file__).resolve().parents[1]
from stage_ui_assets import validate_assets


def add(bundle, name, data, executable=False):
    info = zipfile.ZipInfo('SnowboardKidsRecompiled/' + name, (1980, 1, 1, 0, 0, 0))
    info.compress_type = zipfile.ZIP_DEFLATED
    info.external_attr = ((stat.S_IFREG | (0o755 if executable else 0o644)) << 16)
    bundle.writestr(info, data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--assets', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--library', type=Path, action='append', default=[],
                        help='Runtime shared library placed beside the executable')
    parser.add_argument('--license', action='append', default=[], metavar='NAME=PATH',
                        help='Extra license text shipped as licenses/NAME.txt (bundled runtime DLLs)')
    parser.add_argument('--game-module', type=Path,
                        help='Reviewed precompiled SnowboardKidsGame module to bundle under modules/snowboardkids-us')
    parser.add_argument('--public-beta', action='store_true',
                        help='Build the disclosed public-beta package while RecompFrontend license clarification is pending')
    parser.add_argument('--version', default='0.1.0', help='Project version')
    parser.add_argument('--commit', default=None, help='Git commit hash')
    parser.add_argument('--platform', default=sys.platform if not sys.platform.startswith('linux') else 'linux')
    parser.add_argument('--architecture', default='x86_64')
    parser.add_argument('--draft', action='store_true',
                        help='Local audit draft while license/source blockers remain; never upload')
    args = parser.parse_args()
    notices = (ROOT / 'THIRD_PARTY_NOTICES.md').read_bytes()
    if (b'BLOCKER' in notices or b'RELEASE_BLOCKER' in notices) and not args.draft:
        parser.error('third-party license/source review has blocker entries')
    if args.public_beta:
        required_policy = [
            ROOT / 'LICENSE',
            ROOT / 'SOURCE-COMPLIANCE.md',
            ROOT / 'docs' / 'BETA-DISTRIBUTION-POLICY.md',
        ]
        missing_policy = [str(path.relative_to(ROOT)) for path in required_policy if not path.is_file()]
        if missing_policy:
            parser.error('public beta policy files missing: ' + ', '.join(missing_policy))
        if b'RecompFrontend/issues/44' not in notices:
            parser.error('public beta notices must disclose pending RecompFrontend issue #44')
        if args.game_module is None:
            parser.error('--public-beta requires --game-module for a ready-to-use package')
    if not args.binary.is_file() or not args.assets.is_dir():
        parser.error('built executable and theme asset directory are required')
    from generate_version_header import get_git_commit
    from dependency_lock import compute_lock_digest
    commit = args.commit if args.commit is not None else get_git_commit()
    lock_digest = compute_lock_digest()
    manifest = (
        f"Project: Snowboard Kids Recompiled\n"
        f"Version: {args.version}\n"
        f"Commit: {commit}\n"
        f"Platform: {args.platform}\n"
        f"Architecture: {args.architecture}\n"
        f"Dependency-Lock-Digest: {lock_digest}\n"
    ).encode('utf-8')
    binary_bytes = args.binary.read_bytes()
    import shutil
    import subprocess
    import tempfile
    if sys.platform.startswith('linux') and shutil.which('strip'):
        with tempfile.NamedTemporaryFile() as tmp_bin:
            res = subprocess.run(['strip', '--strip-all', '-o', tmp_bin.name, str(args.binary)], capture_output=True)
            if res.returncode == 0 and Path(tmp_bin.name).stat().st_size > 0:
                binary_bytes = Path(tmp_bin.name).read_bytes()

    files = [('BUILD-INFO.txt', manifest, False),
             ('RUNNING.md', (ROOT / 'RUNNING.md').read_bytes(), False),
             ('THIRD_PARTY_NOTICES.md', notices, False),
             ('LICENSE', (ROOT / 'LICENSE').read_bytes(), False),
             ('SOURCE-COMPLIANCE.md', (ROOT / 'SOURCE-COMPLIANCE.md').read_bytes(), False),
             ('BETA-DISTRIBUTION-POLICY.md',
              (ROOT / 'docs' / 'BETA-DISTRIBUTION-POLICY.md').read_bytes(), False),
             (args.binary.name, binary_bytes, True)]

    # Bundle offline module builder tooling for user-ROM local compilation
    builder_script = ROOT / 'scripts' / 'build-game-module.py'
    if builder_script.is_file():
        files.append(('scripts/build-game-module.py', builder_script.read_bytes(), True))
    module_builder_dir = ROOT / 'scripts' / 'module_builder'
    if module_builder_dir.is_dir():
        for py_file in sorted(module_builder_dir.glob('*.py')):
            files.append((f'scripts/module_builder/{py_file.name}', py_file.read_bytes(), False))
    try:
        reviewed = validate_assets(args.assets, ROOT)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    for name, data in sorted(reviewed.items()):
        files.append(('assets/' + name, data, False))
    if args.game_module is not None:
        module = args.game_module
        if not module.is_file() or module.is_symlink():
            parser.error(f'invalid game module: {module}')
        allowed_names = {'SnowboardKidsGame.so', 'SnowboardKidsGame.dll', 'SnowboardKidsGame.dylib'}
        if module.name not in allowed_names:
            parser.error(f'game module must use a canonical filename, got: {module.name}')
        files.append((f'modules/snowboardkids-us/{module.name}', module.read_bytes(), False))

    for library in args.library:
        if not library.is_file() or library.is_symlink():
            parser.error(f'invalid shared library: {library}')
        files.append((library.name, library.read_bytes(), False))
    license_candidates = [
        ('N64Recomp', ROOT / '.deps/N64Recomp/LICENSE'),
        ('RT64', ROOT / '.deps-renderer/rt64/LICENSE'),
        ('N64ModernRuntime', ROOT / '.deps-runtime/N64ModernRuntime/COPYING'),
        ('nativefiledialog-extended', ROOT / '.deps-renderer/rt64/src/contrib/nativefiledialog-extended/LICENSE'),
        ('promptfont', ROOT / '.deps-renderer/recomp-theme/assets/promptfont/LICENSE.txt'),
        ('LatoLatin', ROOT / 'licenses/LatoLatin-OFL.txt'),
        ('Fredoka', ROOT / 'licenses/Fredoka-OFL.txt'),
        ('NotoEmoji', ROOT / 'licenses/NotoEmoji-OFL.txt'),
    ]
    for spec in args.license:
        name, sep, source = spec.partition('=')
        if not sep or not name or not Path(source).is_file():
            parser.error(f'invalid --license {spec!r}: expected NAME=existing-file')
        license_candidates.append((name, Path(source)))
    for name, source in license_candidates:
        if source.is_file():
            files.append((f'licenses/{name}.txt', source.read_bytes(), False))
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.out, 'w') as bundle:
        for name, data, executable in sorted(files):
            add(bundle, name, data, executable)
    errors = audit(args.out)
    if errors:
        args.out.unlink()
        parser.exit(1, '\n'.join(errors) + '\n')
    digest = hashlib.sha256(args.out.read_bytes()).hexdigest()
    (args.out.parent / 'SHA256SUMS.txt').write_text(f'{digest}  {args.out.name}\n')
    print(f'{args.out}: {digest}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
