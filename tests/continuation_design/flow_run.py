#!/usr/bin/env python3
"""Synthetic generated-flow coverage of existing P1 backend, not new backend support."""
import os
import pathlib
import subprocess
import sys
root = pathlib.Path(__file__).resolve().parents[2]
src = root / 'tests/continuation_design'
build = root / 'build-tools/continuation_design'
build.mkdir(parents=True, exist_ok=True)
recomp = root / '.deps/N64Recomp'
libs = root / 'build-tools/n64recomp'
cxx = os.environ.get('CXX', 'c++')
def run(args):
    subprocess.run(list(map(str, args)), check=True, cwd=root)
includes = [f'-I{recomp / p}' for p in ('include', 'lib/rabbitizer/include', 'lib/rabbitizer/cplusplus/include', 'lib/fmt/include')]
archives = [libs / p for p in ('libN64Recomp.a', 'libSymbolLists.a', 'librabbitizer.a', 'lib/fmt/libfmt.a')]
run([cxx, '-std=c++20', '-O2', *includes, src/'flow_generate.cpp', *archives, '-o', build/'flow_generate'])
run([build/'flow_generate', build/'flow_generated.cpp'])
flags = ['-O2']
if '--sanitize' in sys.argv:
    flags = ['-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']
run([cxx, '-std=c++20', *flags, '-fno-strict-aliasing', f'-I{root / "tests/continuation"}', f'-I{recomp / "include"}', src/'flow_main.cpp', build/'flow_generated.cpp', '-o', build/'flow_test'])
run([build/'flow_test'])
