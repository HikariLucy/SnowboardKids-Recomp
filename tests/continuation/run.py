#!/usr/bin/env python3
"""Build isolated backend against already bootstrapped, pinned N64Recomp."""
import hashlib
import os
import pathlib
import subprocess
import sys

root = pathlib.Path(__file__).resolve().parents[2]
src = root / 'tests/continuation'
build = root / 'build-tools/continuation'
recomp = root / '.deps/N64Recomp'
libs = root / 'build-tools/n64recomp'
build.mkdir(parents=True, exist_ok=True)
def run(args):
    subprocess.run(list(map(str, args)), check=True, cwd=root)

run([sys.executable, src / 'extract.py', root.parent / 'snowboardkids-decomp/build/snowboardkids.elf', build / 'leaf.words'])
compiler = os.environ.get('CXX', 'c++')
includes = [f'-I{recomp / p}' for p in ('include', 'lib/rabbitizer/include', 'lib/rabbitizer/cplusplus/include', 'lib/fmt/include')]
archives = [libs / p for p in ('libN64Recomp.a', 'libSymbolLists.a', 'librabbitizer.a', 'lib/fmt/libfmt.a')]
for archive in archives:
    if not archive.is_file():
        raise SystemExit('Bootstrap the pinned N64Recomp toolchain first: missing ' + str(archive))
run([compiler, '-std=c++20', '-O2', *includes, src / 'generate.cpp', *archives, '-o', build / 'generate'])
hash_input = [*sorted(src.glob('*.cpp')), src / 'runtime.hpp', build / 'leaf.words', recomp / 'include/recomp.h', *archives]
identity = hashlib.sha256(b''.join(p.read_bytes() for p in hash_input)).hexdigest()
run([build / 'generate', build / 'leaf.words', build / 'generated.cpp', identity])
run([build / 'generate', build / 'leaf.words', build / 'reordered.cpp', identity, 'reverse'])
# Declaration order is irrelevant; generated bodies/dispatch/schema must match.
def without_declarations(path):
    return '\n'.join(line for line in path.read_text().splitlines() if not line.startswith('void ') or '(uint8_t*, recomp_context*);' not in line)
assert without_declarations(build / 'generated.cpp') == without_declarations(build / 'reordered.cpp'), 'Unstable generated IDs'
flags = ['-O2']
if '--sanitize' in sys.argv:
    flags = ['-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']
run([compiler, '-std=c++20', *flags, '-fno-strict-aliasing', f'-I{src}', f'-I{recomp / "include"}',
     src / 'runtime.cpp', src / 'main.cpp', build / 'generated.cpp', '-o', build / 'p1'])
run([sys.executable, src / 'test_roundtrip.py', build / 'p1'])
print('PASS stable IDs under reordered function discovery')
