#!/usr/bin/env python3
"""Build and run the presentation geometry regression (GRAPHICS-ASPECT-01), no GPU.

Links the real RT64 geometry decisions (build-renderer-stack/rt64/rt64.a) and the
production presentation reset from RecompFrontend. Before running, checks that
the pinned RT64 presentation path still has the rules the test assumes.
Requires a configured and built build-renderer-stack.
"""
from pathlib import Path
import argparse
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'renderer_lifecycle'))
from run import BUILD, FRONTEND_RENDERER, ROOT, RT64, renderer_flags  # noqa: E402

# (file, text) pairs the test assumes about how a frame reaches the window.
MODELED_RULES = [
    ('src/common/rt64_enhancement_configuration.cpp', 'presentation.removeBlackBorders = true;'),
    ('src/hle/rt64_present_queue.cpp', 'renderParams.resolutionScale = colorTarget->resolutionScale;'),
    ('src/render/rt64_projection_processor.cpp', 'fbPair.projectionCoversWidth(intersectionRect)'),
    ('src/render/rt64_framebuffer_renderer.cpp', 'fbPair.projectionCoversWidth(intersectionRect)'),
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sanitize', action='store_true', help='ASan+UBSan build')
    args = parser.parse_args()

    for relative, text in MODELED_RULES:
        if text not in (RT64 / relative).read_text():
            raise SystemExit(f'RT64 rule changed in {relative}: {text!r}; revisit geometry.cpp')
    print(f'PASS pinned RT64 still has the {len(MODELED_RULES)} modeled presentation rules', flush=True)

    # WorkloadQueue drags in the rest of RT64's static link set.
    libraries = [BUILD / relative for relative in (
        'rt64/rt64.a', 'rt64/src/contrib/plume/libplume.a', 'rt64/src/contrib/re-spirv/libre-spirv.a',
        'rt64/src/contrib/nativefiledialog-extended/src/libnfd.a', 'rt64/src/contrib/zstd/build/cmake/lib/libzstd.a')]
    for library in libraries:
        if not library.exists():
            raise SystemExit(f'missing {library}; build build-renderer-stack first')
    system = subprocess.run(['pkg-config', '--libs', 'sdl2', 'gtk+-3.0'], capture_output=True, text=True,
                            check=True).stdout.split() + ['-ldl']
    # rt64.a is built with clang; a different C++ compiler does not share its hlslpp layout.
    flags = [os.environ.get('CXX', 'clang++'), *renderer_flags(), '-I' + str(FRONTEND_RENDERER),
             '-Wall', '-Wextra', '-pthread']
    if args.sanitize:
        flags += ['-g', '-O1', '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-sanitize-recover=undefined']
    else:
        flags += ['-O2']
    with tempfile.TemporaryDirectory(prefix='sbk-renderer-geometry-') as directory:
        binary = Path(directory) / 'renderer_geometry'
        subprocess.run(flags + [str(ROOT / 'tests/renderer_geometry/geometry.cpp')] +
                       ['-Wl,--start-group', *map(str, libraries), '-Wl,--end-group', *system] +
                       ['-o', str(binary)], check=True, timeout=600)
        return subprocess.run([str(binary)], timeout=60).returncode


if __name__ == '__main__':
    sys.exit(main())
