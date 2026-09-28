#!/usr/bin/env python3
"""ROM-free P4-A tests. Passing does not establish the real-game gate."""
from pathlib import Path
import argparse
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--sanitize', action='store_true')
args = parser.parse_args()
flags = ['-std=c++20', '-Wall', '-Wextra', '-Werror', '-I' + str(ROOT / 'src')]
if args.sanitize:
    flags += ['-g', '-O1', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
              '-fno-pie', '-no-pie']
else:
    flags += ['-O2']
with tempfile.TemporaryDirectory(prefix='sbk-p4a-') as directory:
    executable = Path(directory) / 'owner_registry'
    subprocess.run([os.environ.get('CXX', 'c++'), *flags,
                    str(ROOT / 'tests/production_continuation/owner_registry.cpp'),
                    '-o', str(executable)], check=True, timeout=120)
    subprocess.run([str(executable)], check=True, timeout=60)
