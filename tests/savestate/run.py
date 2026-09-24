#!/usr/bin/env python3
"""Build and run the ROM-free savestate tests (P4-B/P4-C, P5 audio, P6 .sbks).

1. persistence: .sbks format/parser hardening, atomic storage and the host
   audio boundary (no runtime).
2. runtime: links the actual patched ultramodern scheduler/queues/timer/thread
   sources, the continuation dispatcher/HLE/owner registry, the P2 coordinator
   and the savestate service; capture/restore, rollback, repeated restores and
   capture -> .sbks -> restore cycles in memory and on disk.
3. cross-process: process A saves a .sbks and exits; a fresh process B boots,
   loads and restores it, and continues.
Also compile-checks the app-only adapters. Passing does not establish live
restore in the game.
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
parser.add_argument('--tsan', action='store_true', help='ThreadSanitizer build (run under setarch -R)')
args = parser.parse_args()
cxx = os.environ.get('CXX', 'c++')
# Same recomp.h the application build uses (runtime's N64Recomp submodule).
includes = ['-I' + str(ROOT / 'src'), '-I' + str(RUNTIME / 'N64Recomp/include')]
includes += ['-I' + str(RUNTIME / inc) for inc in [
    'ultramodern/include', 'librecomp/include', 'thirdparty', 'thirdparty/concurrentqueue']]
flags = [cxx, '-std=c++20', '-pthread', '-DSBK_CONTINUATIONS', '-fno-strict-aliasing',
         '-Wall', '-Wextra', '-Wno-unused-parameter', '-Wno-missing-field-initializers', *includes]
if args.sanitize:
    flags += ['-g', '-O1', '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-sanitize-recover=undefined']
elif args.tsan:
    flags += ['-g', '-O1', '-fsanitize=thread']
else:
    flags += ['-O2']
savestate = [ROOT / f'src/savestate/{name}.cpp' for name in ['snapshot', 'sbks', 'storage', 'host_audio']]
runtime_sources = [ROOT / 'tests/savestate/runtime.cpp', ROOT / 'tests/savestate/hle_stubs.cpp',
                   ROOT / 'src/quiescence/quiescence.cpp']
runtime_sources += [ROOT / f'src/continuation/{name}.cpp' for name in ['dispatch', 'execution', 'hle', 'runtime_owner']]
runtime_sources += savestate + [ROOT / f'src/savestate/{name}.cpp' for name in ['service', 'runtime_domains']]
runtime_sources += [RUNTIME / f'ultramodern/src/{name}.cpp' for name in [
    'threads', 'threadqueue', 'scheduling', 'mesgqueue', 'timer', 'audio']]
env = dict(os.environ)
if args.sanitize:
    # Detached runtime workers are owned by process exit (_Exit) by design.
    env['ASAN_OPTIONS'] = 'detect_leaks=0'
wrap = ['setarch', os.uname().machine, '-R'] if args.tsan else []
with tempfile.TemporaryDirectory(prefix='sbk-savestate-') as directory:
    directory = Path(directory)
    persistence = directory / 'savestate_persistence'
    runtime = directory / 'savestate_runtime'
    subprocess.run(flags + [str(ROOT / 'tests/savestate/persistence.cpp')] + [str(s) for s in savestate] +
                   ['-o', str(persistence)], check=True, timeout=300)
    subprocess.run(flags + [str(s) for s in runtime_sources] + ['-o', str(runtime)], check=True, timeout=300)
    app_only = flags + ['-I' + str(RUNTIME / 'librecomp/include/librecomp'), '-fsyntax-only']
    for source in [RUNTIME / 'ultramodern/src/events.cpp', RUNTIME / 'librecomp/src/overlays.cpp',
                   ROOT / 'src/savestate/app_domains.cpp', ROOT / 'src/savestate/driver.cpp',
                   ROOT / 'src/savestate/dev_trigger.cpp']:
        subprocess.run(app_only + [str(source)], check=True, timeout=120)
    subprocess.run(wrap + [str(persistence), str(directory / 'persistence-files')], check=True, timeout=600, env=env)
    subprocess.run(wrap + [str(runtime)], check=True, timeout=600, env=env)
    state = directory / 'cross-process' / 'quick.sbks'
    subprocess.run(wrap + [str(runtime), '--save', str(state)], check=True, timeout=240, env=env)
    subprocess.run(wrap + [str(runtime), '--load', str(state)], check=True, timeout=240, env=env)
    print('PASS: cross-process .sbks save (process A) -> load/restore (process B)')
