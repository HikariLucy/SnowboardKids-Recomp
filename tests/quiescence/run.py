#!/usr/bin/env python3
"""Build and run ROM-free P2 tests against project and patched runtime sources."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
RUNTIME = ROOT / '.deps-runtime/N64ModernRuntime'
compiler = os.environ.get('CXX', 'c++')
with tempfile.TemporaryDirectory(prefix='sbk-p2-tests-') as directory:
    common = [compiler, '-std=c++20', '-pthread', '-Wall', '-Wextra', '-I' + str(ROOT / 'src')]
    coordinator = ROOT / 'src/quiescence/quiescence.cpp'
    for name in ['main', 'kernel', 'lifecycle']:
        command = common + [str(ROOT / f'tests/quiescence/{name}.cpp'), str(coordinator)]
        if name == 'kernel':
            command += ['-Wno-unused-parameter', '-Wno-missing-field-initializers']
            command += ['-I' + str(RUNTIME / inc) for inc in [
                'ultramodern/include', 'thirdparty', 'thirdparty/concurrentqueue']]
            command += [str(RUNTIME / f'ultramodern/src/{source}.cpp') for source in [
                'threads', 'threadqueue', 'scheduling', 'mesgqueue', 'timer']]
        executable = Path(directory) / name
        subprocess.run(command + ['-o', str(executable)], check=True, timeout=120)
        subprocess.run([str(executable)], check=True, timeout=60)
        if name == 'kernel':
            subprocess.run([str(executable), '--baseline'], check=True, timeout=60)
        if name == 'lifecycle':
            subprocess.run([str(executable), '--late-enable'], check=True, timeout=60)
