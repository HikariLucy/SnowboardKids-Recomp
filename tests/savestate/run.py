#!/usr/bin/env python3
"""Build and run the ROM-free P4-B/P4-C savestate tests.

Links the actual patched ultramodern scheduler/queues/timer/thread sources, the
continuation dispatcher/HLE/owner registry, the P2 coordinator and the savestate
service. Also compile-checks the runtime adapters that need the full app
(VI/events, librecomp overlays). Passing does not establish live restore.
"""
from pathlib import Path
import argparse
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
RUNTIME = ROOT / '.deps-runtime/N64ModernRuntime'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--sanitize', action='store_true', help='ASan+UBSan build')
args = parser.parse_args()
cxx = os.environ.get('CXX', 'c++')
# Same recomp.h the application build uses (runtime's N64Recomp submodule).
includes = ['-I' + str(ROOT / 'src'), '-I' + str(RUNTIME / 'N64Recomp/include')]
includes += ['-I' + str(RUNTIME / inc) for inc in [
    'ultramodern/include', 'librecomp/include', 'thirdparty', 'thirdparty/concurrentqueue']]
flags = [cxx, '-std=c++20', '-pthread', '-DSBK_CONTINUATIONS', '-fno-strict-aliasing',
         '-Wall', '-Wextra', '-Wno-unused-parameter', '-Wno-missing-field-initializers', *includes]
flags += (['-g', '-O1', '-fsanitize=address,undefined', '-fno-omit-frame-pointer']
          if args.sanitize else ['-O2'])
sources = [ROOT / 'tests/savestate/runtime.cpp', ROOT / 'tests/savestate/hle_stubs.cpp',
           ROOT / 'src/quiescence/quiescence.cpp']
sources += [ROOT / f'src/continuation/{name}.cpp' for name in ['dispatch', 'execution', 'hle', 'runtime_owner']]
sources += [ROOT / f'src/savestate/{name}.cpp' for name in ['snapshot', 'service', 'runtime_domains']]
sources += [RUNTIME / f'ultramodern/src/{name}.cpp' for name in [
    'threads', 'threadqueue', 'scheduling', 'mesgqueue', 'timer', 'audio']]
with tempfile.TemporaryDirectory(prefix='sbk-p4bc-') as directory:
    executable = Path(directory) / 'savestate_runtime'
    subprocess.run(flags + [str(s) for s in sources] + ['-o', str(executable)], check=True, timeout=300)
    app_only = flags + ['-I' + str(RUNTIME / 'librecomp/include/librecomp'), '-fsyntax-only']
    for source in [RUNTIME / 'ultramodern/src/events.cpp', RUNTIME / 'librecomp/src/overlays.cpp',
                   ROOT / 'src/savestate/app_domains.cpp']:
        subprocess.run(app_only + [str(source)], check=True, timeout=120)
    env = dict(os.environ)
    if args.sanitize:
        # Detached runtime workers are owned by process exit (_Exit) by design.
        env['ASAN_OPTIONS'] = 'detect_leaks=0'
    subprocess.run([str(executable)], check=True, timeout=240, env=env)
