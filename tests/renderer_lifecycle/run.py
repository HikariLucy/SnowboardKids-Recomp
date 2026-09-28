#!/usr/bin/env python3
"""Build and run the renderer lifecycle contract test (P6-XPROC-01), no GPU.

Links the real RT64::SharedQueueResources (build-renderer-stack/rt64/rt64.a)
and the production reset from RecompFrontend. Before running, checks that the
pinned RT64 queues still contain the handshake rules the test models.
Requires a configured and built build-renderer-stack.
"""
from pathlib import Path
import argparse
import json
import os
import shlex
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / 'build-renderer-stack'
RT64 = ROOT / '.deps-renderer/rt64'
FRONTEND_RENDERER = ROOT / '.deps-renderer/RecompFrontend/recompui/src/renderer'

# (file, text) pairs the contract models; a pin bump that changes them must
# revisit contract.cpp.
MODELED_RULES = [
    ('src/hle/rt64_present_queue.cpp', 'framesToPresent = frameCounters.count;'),
    ('src/hle/rt64_present_queue.cpp', 'if (i == 0) {\n                notifyPresentId(present);'),
    ('src/hle/rt64_workload_queue.cpp', 'ext.presentQueue->waitForPresentId(workload.presentId);'),
    ('src/hle/rt64_workload_queue.cpp',
     'return (prevFrameCounters.presented == 0) || (prevFrameCounters.presented >= prevFrameCounters.available);'),
    ('src/hle/rt64_workload_queue.cpp', 'assert((displayFrames > 0) && "At least one display frame must be generated.");'),
]


def renderer_flags():
    for entry in json.loads((BUILD / 'compile_commands.json').read_text()):
        if entry['file'].endswith('recompui/src/renderer/rt64_render_context.cpp'):
            args = shlex.split(entry['command']) if 'command' in entry else entry['arguments']
            # Third-party headers: warnings stay scoped to the test and the reset.
            return ['-isystem' + a[2:] if a.startswith('-I') else a
                    for a in args if a.startswith(('-D', '-I', '-std'))]
    raise SystemExit('rt64_render_context.cpp not in compile_commands.json; build build-renderer-stack first')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sanitize', action='store_true', help='ASan+UBSan build')
    args = parser.parse_args()

    for relative, text in MODELED_RULES:
        if text not in (RT64 / relative).read_text():
            raise SystemExit(f'RT64 rule changed in {relative}: {text!r}; revisit contract.cpp')
    print(f'PASS pinned RT64 still has the {len(MODELED_RULES)} modeled handshake rules', flush=True)

    libraries = [BUILD / 'rt64/rt64.a', BUILD / 'rt64/src/contrib/zstd/build/cmake/lib/libzstd.a']
    for library in libraries:
        if not library.exists():
            raise SystemExit(f'missing {library}; build build-renderer-stack first')
    flags = [os.environ.get('CXX', 'c++'), *renderer_flags(), '-I' + str(FRONTEND_RENDERER),
             '-Wall', '-Wextra', '-pthread']
    if args.sanitize:
        flags += ['-g', '-O1', '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-sanitize-recover=undefined']
    else:
        flags += ['-O2']
    with tempfile.TemporaryDirectory(prefix='sbk-renderer-lifecycle-') as directory:
        binary = Path(directory) / 'renderer_lifecycle_contract'
        subprocess.run(flags + [str(ROOT / 'tests/renderer_lifecycle/contract.cpp')] +
                       [str(l) for l in libraries] + ['-o', str(binary)], check=True, timeout=600)
        return subprocess.run([str(binary)], timeout=60).returncode


if __name__ == '__main__':
    sys.exit(main())
