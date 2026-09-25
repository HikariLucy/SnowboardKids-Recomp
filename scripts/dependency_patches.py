#!/usr/bin/env python3
"""Apply pinned patch series, accepting only complete canonical prefix states.

Reconstruct all affected files from Git objects in a temporary directory, then
compare bytes and executable bits at every patch boundary. This also recognizes
older patches whose context was changed by a later patch. Unrelated files are
left alone; local edits to affected files fail closed instead of being discarded.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SERIES = {
    'runtime': ('.deps-runtime/N64ModernRuntime',
                '6ccb2e7c2e7f6708257b461097e0aaf03c445e2a', (
                    'n64modernruntime-osstopthread.patch',
                    'n64modernruntime-quiescence.patch',
                    'n64modernruntime-continuations.patch',
                    'n64modernruntime-savestate.patch',
                    'n64modernruntime-shutdown.patch')),
    'recomp': ('.deps/N64Recomp',
               'ffb39cdad1da5de07eaaa48bd1db4a89a7986771', (
                   'n64recomp-continuations.patch',)),
    'rt64': ('.deps-renderer/rt64',
             '6a4166b2cfa952d931a08481d1037da995f28b54', (
                 'rt64-quiescence.patch',
                 'rt64-aspect-coverage.patch')),
    'frontend': ('.deps-renderer/RecompFrontend',
                 'e85b912d9df677b04f9358867dd010c8af27ea05', (
                     'recompfrontend-resolution.patch',
                     'recompfrontend-quiescence.patch',
                     'recompfrontend-input.patch')),
}


def git(directory, *args):
    result = subprocess.run(['git', '-C', str(directory), *map(str, args)],
                            capture_output=True)
    if result.returncode:
        raise RuntimeError(f'{directory}: git {args[0]} failed:\n'
                           + result.stderr.decode(errors='replace'))
    return result.stdout


def snapshot(directory, paths):
    state = {}
    for relative in paths:
        path = directory / relative
        if path.is_symlink():
            raise RuntimeError(f'Refusing symlink in patched file: {path}')
        state[relative] = ((path.read_bytes(), bool(path.stat().st_mode & 0o111))
                           if path.exists() else None)
    return state


def prepare(root, name):
    relative, pin, names = SERIES[name]
    directory = root / relative
    if not directory.exists() and name in ('rt64', 'frontend'):
        print(f'{relative}: absent; skipping optional renderer patch')
        return None
    actual = git(directory, 'rev-parse', 'HEAD').decode().strip()
    if actual != pin:
        raise RuntimeError(f'Refusing {relative}: wrong pin/upstream; '
                           f'expected {pin}, actual {actual}')
    patches = [ROOT / 'patches' / p for p in names]
    paths = set()
    for patch in patches:
        for line in git(directory, 'apply', '--numstat', patch).decode().splitlines():
            paths.add(line.split('\t', 2)[2])
    current = snapshot(directory, paths)
    with tempfile.TemporaryDirectory(prefix='sbk-patch-series-') as temporary:
        baseline = Path(temporary)
        git(baseline, 'init', '--quiet')
        entries = git(directory, 'ls-tree', '-z', pin, '--', *sorted(paths))
        for entry in entries.split(b'\0'):
            if not entry:
                continue
            metadata, path_bytes = entry.split(b'\t', 1)
            mode, kind, oid = metadata.decode().split()
            if kind != 'blob' or mode not in ('100644', '100755'):
                raise RuntimeError(f'Unsupported upstream file: {entry!r}')
            path = baseline / path_bytes.decode()
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(git(directory, 'cat-file', 'blob', oid))
            path.chmod(int(mode, 8) & 0o777)
        states = [snapshot(baseline, paths)]
        for patch in patches:
            git(baseline, 'apply', '--check', patch)
            git(baseline, 'apply', patch)
            states.append(snapshot(baseline, paths))
    matches = [index for index, state in enumerate(states) if state == current]
    if not matches:
        raise RuntimeError(f'Refusing {relative}: partial/noncanonical patch state '
                           '(or local edits to patched files); no files changed. '
                           'Expected the pin or a complete canonical patch prefix.')
    return directory, patches, max(matches), states[-1]


def main(default=('runtime', 'recomp')):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=ROOT,
                        help='Dependency root (patches always come from this project)')
    parser.add_argument('--only', choices=SERIES, action='append',
                        help='Select dependencies; repeat for multiple series')
    args = parser.parse_args()
    try:
        # Validate every selected dependency before changing any of them.
        plans = [prepare(args.root.resolve(), name)
                 for name in dict.fromkeys(args.only or default)]
        for plan in plans:
            if plan is None:
                continue
            directory, patches, applied, expected = plan
            for index, patch in enumerate(patches):
                if index < applied:
                    print(f'{patch.name}: already applied (complete canonical state)')
                    continue
                git(directory, 'apply', '--check', patch)
                git(directory, 'apply', patch)
                print(f'{patch.name}: applied')
            if snapshot(directory, expected) != expected:
                raise RuntimeError(f'{directory}: final patched files changed unexpectedly')
    except (OSError, RuntimeError) as error:
        parser.exit(1, f'{error}\n')


if __name__ == '__main__':
    main()
