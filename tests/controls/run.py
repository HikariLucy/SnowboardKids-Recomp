#!/usr/bin/env python3
"""CONTROL-P1 analog regression, no controller and no window.

Builds tests/controls/analog.cpp against the real RecompInput radial deadzone
header and the real N64ModernRuntime stick conversion: the input.cpp object
is taken from the built libultramodern.a, so this checks the code the game
runs. ASan and UBSan are always on. Needs a built build-renderer-stack.
"""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
ULTRAMODERN = ROOT / 'build-renderer-stack/N64ModernRuntime-renderer/ultramodern/libultramodern.a'
RECOMPINPUT = ROOT / '.deps-renderer/RecompFrontend/recompinput/include'


def main():
    if not ULTRAMODERN.exists():
        raise SystemExit(f'missing {ULTRAMODERN}; build build-renderer-stack first')
    with tempfile.TemporaryDirectory(prefix='sbk-controls-') as directory:
        subprocess.run(['ar', 'x', str(ULTRAMODERN), 'input.cpp.o'], cwd=directory, check=True)
        binary = Path(directory) / 'analog'
        subprocess.run([os.environ.get('CXX', 'clang++'), '-std=c++20', '-Wall', '-Wextra', '-Werror', '-g',
                        '-fsanitize=address,undefined', '-fno-sanitize-recover=undefined', '-I' + str(RECOMPINPUT),
                        str(ROOT / 'tests/controls/analog.cpp'), str(Path(directory) / 'input.cpp.o'),
                        '-o', str(binary)], check=True, timeout=300)
        return subprocess.run([str(binary)], timeout=60).returncode


if __name__ == '__main__':
    sys.exit(main())
