#!/usr/bin/env python3
"""Fetch exact upstream pins and apply the canonical patch series safely."""
import argparse
from pathlib import Path
import subprocess
import sys

from dependency_lock import DEPENDENCIES
import dxc_redist

ROOT = Path(__file__).resolve().parents[1]


def run(*args):
    subprocess.run(args, check=True)


def output(*args):
    return subprocess.check_output(args, text=True).strip()


def ensure(name, root=ROOT):
    dep = DEPENDENCIES[name]
    path = root / dep.path
    if not path.exists():
        path.parent.mkdir(parents=True, exist_ok=True)
        run('git', 'clone', '--recurse-submodules', dep.url, str(path))
        run('git', '-C', str(path), 'fetch', 'origin', dep.commit)
        run('git', '-C', str(path), 'checkout', '--detach', dep.commit)
    if not (path / '.git').exists():
        raise RuntimeError(f'{path}: existing path is not a Git checkout')
    actual = output('git', '-C', str(path), 'rev-parse', 'HEAD')
    if actual != dep.commit:
        raise RuntimeError(f'{path}: wrong commit {actual}; expected {dep.commit}')
    remote = output('git', '-C', str(path), 'remote', 'get-url', 'origin')
    if remote != dep.url:
        raise RuntimeError(f'{path}: wrong origin {remote}; expected {dep.url}')
    run('git', '-C', str(path), 'submodule', 'update', '--init', '--recursive')
    print(f'{name}: {actual}', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    # 'dxc' is the pinned Microsoft DXC release archive (Windows runtime DLLs),
    # not a Git checkout; it is fetched by default only on Windows.
    parser.add_argument('--only', choices=[*DEPENDENCIES, 'dxc'], action='append')
    args = parser.parse_args()
    default = [*DEPENDENCIES] + (['dxc'] if sys.platform == 'win32' else [])
    names = list(dict.fromkeys(args.only or default))
    try:
        for name in names:
            if name == 'dxc':
                dxc_redist.fetch()
            else:
                ensure(name)
        patched = [name for name in names if name in DEPENDENCIES and DEPENDENCIES[name].patches]
        if patched:
            run(sys.executable, str(ROOT / 'scripts/dependency_patches.py'),
                *[value for name in patched for value in ('--only', name)])
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        parser.exit(1, f'Bootstrap failed: {error}\n')


if __name__ == '__main__':
    main()
