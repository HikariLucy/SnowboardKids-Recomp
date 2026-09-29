#!/usr/bin/env python3
"""Fail closed on user data, ROMs, build junk and personal paths in a zip."""
import argparse
from pathlib import PurePosixPath
import re
import sys
import zipfile

BAD_SUFFIXES = {'.z64', '.n64', '.v64', '.rom', '.mpk', '.pak', '.sbks',
                '.log', '.o', '.obj', '.a', '.lib', '.pdb', '.cmake', '.json',
                '.exp', '.ilk', '.def', '.manifest', '.zip', '.inl'}
BAD_NAMES = {'cmakecache.txt', 'build.ninja', 'makefile', 'portable.txt',
             '.git', 'runtime-data', '__pycache__',
             # ROM-derived module inputs (corpus/RSP), see scripts/module_builder/inputs.py
             'corpus', 'aspmain.cpp', 'lookup.cpp', 'funcs.h'}
ROM_DERIVED_SOURCE = re.compile(r'funcs_\d+\.c', re.IGNORECASE)
GAME_MODULE_PATHS = {
    'SnowboardKidsRecompiled/modules/snowboardkids-us/SnowboardKidsGame.so',
    'SnowboardKidsRecompiled/modules/snowboardkids-us/SnowboardKidsGame.dll',
    'SnowboardKidsRecompiled/modules/snowboardkids-us/SnowboardKidsGame.dylib',
}
GAME_MODULE_NAMES = {'snowboardkidsgame.so', 'snowboardkidsgame.dll', 'snowboardkidsgame.dylib'}
# Keep in sync with scripts/windows_runtime.py REDISTRIBUTABLES (tested).
REDISTRIBUTABLE_DLLS = {
    'sdl2.dll', 'dxcompiler.dll', 'dxil.dll', 'vcruntime140.dll', 'vcruntime140_1.dll',
    'msvcp140.dll', 'msvcp140_1.dll', 'msvcp140_2.dll', 'msvcp140_atomic_wait.dll', 'concrt140.dll',
}
ROM_MAGIC = (b'\x80\x37\x12\x40', b'\x37\x80\x40\x12', b'\x40\x12\x37\x80')
PATH_PATTERN = re.compile(rb'/home/[^/\x00\s]+|/Users/[^/\x00\s]+|/tmp/[^\x00\s]+|[A-Za-z]:\\Users\\[^\\\x00\s]+')
MAX_FILE = 500 * 1024 * 1024
MAX_TOTAL = 1024 * 1024 * 1024


def audit(archive):
    errors = []
    total = 0
    with zipfile.ZipFile(archive) as bundle:
        names = set()
        for info in bundle.infolist():
            name = info.filename
            path = PurePosixPath(name)
            parts = path.parts
            if name in names:
                errors.append(f'duplicate entry: {name}')
            names.add(name)
            if path.is_absolute() or '..' in parts or len(parts) < 2 or parts[0] != 'SnowboardKidsRecompiled':
                errors.append(f'unsafe layout: {name}')
            if info.is_dir():
                continue
            if (info.external_attr >> 16) & 0o170000 == 0o120000:
                errors.append(f'symlink: {name}')
            if (any(part.lower() in BAD_NAMES for part in parts) or path.suffix.lower() in BAD_SUFFIXES
                    or ROM_DERIVED_SOURCE.fullmatch(path.name)):
                errors.append(f'forbidden file: {name}')
            if path.name.lower() in GAME_MODULE_NAMES and name not in GAME_MODULE_PATHS:
                errors.append(f'game module outside canonical release path: {name}')
            elif path.suffix.lower() == '.dll' and name not in GAME_MODULE_PATHS:
                # Only reviewed redistributables, and only beside the executable.
                if len(parts) != 2 or path.name.lower() not in REDISTRIBUTABLE_DLLS:
                    errors.append(f'unreviewed runtime DLL: {name}')
            if info.file_size > MAX_FILE:
                errors.append(f'oversize file: {name}')
                continue
            total += info.file_size
            if total > MAX_TOTAL:
                errors.append('archive exceeds size limit')
                break
            data = bundle.read(info)
            if data.startswith(ROM_MAGIC):
                errors.append(f'ROM header: {name}')
            if PATH_PATTERN.search(data):
                errors.append(f'personal absolute path: {name}')
        executable = any(name in ('SnowboardKidsRecompiled/SnowboardKidsRecompiled',
                                  'SnowboardKidsRecompiled/SnowboardKidsRecompiled.exe',
                                  'SnowboardKidsRecompiled/SnowboardKidsEngine',
                                  'SnowboardKidsRecompiled/SnowboardKidsEngine.exe') for name in names)
        if not executable:
            errors.append('missing executable')
        if 'SnowboardKidsRecompiled/BUILD-INFO.txt' not in names:
            errors.append('missing build manifest (BUILD-INFO.txt)')
        if 'SnowboardKidsRecompiled/RUNNING.md' not in names:
            errors.append('missing running guide')
        if 'SnowboardKidsRecompiled/THIRD_PARTY_NOTICES.md' not in names:
            errors.append('missing third-party notices')
        if 'SnowboardKidsRecompiled/LICENSE' not in names:
            errors.append('missing project license')
        if 'SnowboardKidsRecompiled/SOURCE-COMPLIANCE.md' not in names:
            errors.append('missing corresponding-source directions')
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive')
    args = parser.parse_args()
    try:
        errors = audit(args.archive)
    except (OSError, zipfile.BadZipFile, RuntimeError) as error:
        errors = [str(error)]
    if errors:
        for error in errors:
            print(f'FAIL: {error}', file=sys.stderr)
        return 1
    print('PASS: release artifact audit')
    return 0


if __name__ == '__main__':
    sys.exit(main())
