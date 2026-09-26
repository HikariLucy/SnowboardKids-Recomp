#!/usr/bin/env python3
"""Stage reviewed project drawings and licensed fonts; validate every byte."""
import argparse
from pathlib import Path
import shutil
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
ICONS = ('Caret', 'Cont', 'Keyboard', 'PlusKeyboard', 'Question', 'Quit',
         'RecordBorder', 'RecordSpinner', 'Reset', 'Trash', 'X')
FONTS = ('Fredoka.ttf', 'LatoLatin-Regular.ttf', 'LatoLatin-Italic.ttf',
         'LatoLatin-Bold.ttf', 'LatoLatin-BoldItalic.ttf', 'NotoEmoji-Regular.ttf',
         'promptfont/promptfont.ttf', 'promptfont/LICENSE.txt')


def reviewed_assets(root=ROOT):
    owned = root / 'assets/sbk-ui'
    theme = root / '.deps-renderer/recomp-theme/assets'
    paths = {f'icons/{name}.svg': owned / 'icons' / f'{name}.svg' for name in ICONS}
    paths.update({name: owned / name for name in ('recomp.rcss', 'slope.svg', 'README.md')})
    paths.update({name: theme / name for name in FONTS})
    paths.update({f'licenses/{name}-OFL.txt': root / 'licenses' / f'{name}-OFL.txt'
                  for name in ('LatoLatin', 'Fredoka', 'NotoEmoji')})
    for name, path in paths.items():
        if path.is_symlink() or not path.is_file():
            raise ValueError(f'missing or symlinked reviewed asset: {name}')
    actual = {p.relative_to(owned).as_posix() for p in owned.rglob('*') if p.is_file()}
    expected = {name for name in paths if paths[name].is_relative_to(owned)}
    if actual != expected or any(p.is_symlink() for p in owned.rglob('*')):
        raise ValueError('project asset directory contains unreviewed files or symlinks')
    return {name: path.read_bytes() for name, path in paths.items()}


def validate_assets(directory, root=ROOT):
    expected = reviewed_assets(root)
    if directory.is_symlink() or not directory.is_dir():
        raise ValueError('staged asset directory missing or symlinked')
    entries = list(directory.rglob('*'))
    if any(p.is_symlink() for p in entries):
        raise ValueError('asset symlink refused')
    actual = {p.relative_to(directory).as_posix() for p in entries if p.is_file()}
    if actual != set(expected):
        raise ValueError(f'unreviewed or missing staged assets: {sorted(actual ^ set(expected))}')
    for name, data in expected.items():
        if (directory / name).read_bytes() != data:
            raise ValueError(f'staged asset differs from reviewed source: {name}')
    return expected


def validate_archive(archive, root=ROOT):
    expected = reviewed_assets(root)
    prefix = 'SnowboardKidsRecompiled/assets/'
    with zipfile.ZipFile(archive) as bundle:
        names = [name[len(prefix):] for name in bundle.namelist() if name.startswith(prefix)]
        if len(names) != len(expected) or set(names) != set(expected):
            raise ValueError('archive has unreviewed or missing assets')
        for name, data in expected.items():
            if bundle.read(prefix + name) != data:
                raise ValueError(f'archive asset differs from reviewed source: {name}')


def stage(directory, root=ROOT):
    expected = reviewed_assets(root)
    # The output must be a dedicated assets directory, never the source tree.
    destination = directory.absolute()
    protected = (root.resolve(), (root / 'assets').resolve(),
                 (root / '.deps-renderer').resolve())
    if (destination.name != 'assets' or destination.is_symlink() or
        any(destination.resolve() == p or destination.resolve() in p.parents for p in protected) or
        destination.resolve().is_relative_to(root / 'assets') or
        destination.resolve().is_relative_to(root / '.deps-renderer')):
        raise ValueError('output must be a dedicated build assets directory outside asset sources')
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.sbk-assets-', dir=destination.parent) as tmp:
        staged = Path(tmp) / 'assets'
        staged.mkdir()
        for name, data in expected.items():
            target = staged / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
        validate_assets(staged, root)
        if destination.exists():
            shutil.rmtree(destination)
        staged.rename(destination)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=ROOT)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    try:
        stage(args.out, args.root)
    except (OSError, ValueError) as error:
        parser.error(str(error))


if __name__ == '__main__':
    main()
