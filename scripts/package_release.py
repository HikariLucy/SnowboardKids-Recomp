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
ASSET_SUFFIXES = {'.png', '.svg', '.ttf', '.rcss', '.txt'}


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
    parser.add_argument('--library', type=Path, action='append', default=[])
    parser.add_argument('--draft', action='store_true',
                        help='Local audit draft while license/source blockers remain; never upload')
    args = parser.parse_args()
    notices = (ROOT / 'THIRD_PARTY_NOTICES.md').read_bytes()
    if b'RELEASE_BLOCKER' in notices and not args.draft:
        parser.error('third-party license/source review has RELEASE_BLOCKER entries')
    if not args.binary.is_file() or not args.assets.is_dir():
        parser.error('built executable and theme asset directory are required')
    files = [('RUNNING.md', (ROOT / 'RUNNING.md').read_bytes(), False),
             ('THIRD_PARTY_NOTICES.md', notices, False),
             (args.binary.name, args.binary.read_bytes(), True)]
    for path in sorted(args.assets.rglob('*')):
        if path.is_symlink():
            parser.error(f'asset symlink refused: {path}')
        if path.is_file():
            if path.suffix.lower() not in ASSET_SUFFIXES:
                parser.error(f'unreviewed asset type: {path}')
            files.append(('assets/' + path.relative_to(args.assets).as_posix(), path.read_bytes(), False))
    for library in args.library:
        if not library.is_file() or library.is_symlink():
            parser.error(f'invalid shared library: {library}')
        files.append((library.name, library.read_bytes(), False))
    for name, source in (('N64Recomp', ROOT / '.deps/N64Recomp/LICENSE'),
                         ('RT64', ROOT / '.deps-renderer/rt64/LICENSE')):
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
