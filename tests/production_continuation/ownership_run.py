#!/usr/bin/env python3
"""ROM-free production ownership adapter test and runtime integration compile."""
from pathlib import Path
import os
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[2]
RUNTIME = ROOT / '.deps-runtime/N64ModernRuntime'
with tempfile.TemporaryDirectory(prefix='sbk-ownership-') as directory:
    flags = [os.environ.get('CXX', 'c++'), '-std=c++20', '-pthread', '-DSBK_CONTINUATIONS',
             '-Wall', '-Wextra', '-Wno-unused-parameter', '-I' + str(ROOT/'src'),
             '-I' + str(ROOT/'.deps/N64Recomp/include')]
    flags += ['-I' + str(RUNTIME/inc) for inc in [
        'ultramodern/include', 'thirdparty', 'thirdparty/concurrentqueue']]
    executable = str(Path(directory)/'ownership')
    subprocess.run(flags + [str(ROOT/'tests/production_continuation/ownership.cpp'),
        str(ROOT/'src/continuation/runtime_owner.cpp'), '-o', executable], check=True, timeout=120)
    subprocess.run([executable], check=True, timeout=30)
    subprocess.run(flags + ['-c', str(RUNTIME/'ultramodern/src/threads.cpp'),
        '-o', str(Path(directory)/'threads.o')], check=True, timeout=120)
