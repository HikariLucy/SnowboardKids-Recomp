#!/usr/bin/env python3
"""P1 cross-optimization / PIE proof, independent of the proposed schema model.
Requires python3 tests/continuation/run.py to have produced generated.cpp.
Uses two host binaries of the same semantic generated build, not save migration.
"""
import os
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[2]
src = root / 'tests/continuation'
build = root / 'build-tools/continuation'
def run(args):
    subprocess.run([str(a) for a in args], cwd=root, check=True)
if not (build / 'generated.cpp').exists():
    raise SystemExit('Run python3 tests/continuation/run.py first')
with tempfile.TemporaryDirectory(prefix='p15-cross-build-') as directory:
    work = Path(directory)
    binaries = []
    for name, flags in [('o0-pie', ['-O0', '-fPIE', '-pie']),
                        ('o2-fixed', ['-O2', '-fno-pie', '-no-pie'])]:
        binary = work / name
        run([os.environ.get('CXX', 'c++'), '-std=c++20', *flags, '-fno-strict-aliasing',
             '-I' + str(src), '-I' + str(root / '.deps/N64Recomp/include'),
             src / 'runtime.cpp', src / 'main.cpp', build / 'generated.cpp', '-o', binary])
        binaries.append(binary)
    for fr in (0, 1):
        reference = work / f'reference-{fr}'
        run([binaries[0], 'reference', reference, fr])
        parks = []
        for index, exporter in enumerate(binaries):
            parked = work / f'park-{fr}-{index}'
            resumed = work / f'resumed-{fr}-{index}'
            run([exporter, 'export', parked, fr])
            run([binaries[1-index], 'import', parked, resumed])
            if resumed.read_bytes() != reference.read_bytes():
                raise AssertionError('cross-build resume differs from baseline')
            parks.append(parked.read_bytes())
        if parks[0] != parks[1]:
            raise AssertionError('host compiler/layout leaked into continuation encoding')
    print('PASS cross-binary P1: O0 PIE <-> O2 non-PIE, FR=0/1, identical parked and final bytes')
