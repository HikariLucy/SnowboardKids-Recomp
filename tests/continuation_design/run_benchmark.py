#!/usr/bin/env python3
"""Run existing P1 verification first, then a no-park, preallocated P1 microbenchmark."""
import os
from pathlib import Path
import subprocess
import sys
root = Path(__file__).resolve().parents[2]
def run(args):
    subprocess.run([str(a) for a in args], cwd=root, check=True)
run([sys.executable, root / 'tests/continuation/run.py'])
build = root / 'build-tools/continuation'
run([os.environ.get('CXX', 'c++'), '-std=c++20', '-O2', '-fno-strict-aliasing',
     '-I' + str(root / 'tests/continuation'), '-I' + str(root / '.deps/N64Recomp/include'),
     root / 'tests/continuation/runtime.cpp', root / 'tests/continuation_design/benchmark.cpp',
     build / 'generated.cpp', '-o', build / 'p15-benchmark'])
run([build / 'p15-benchmark'])
