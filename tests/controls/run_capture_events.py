#!/usr/bin/env python3
"""Exercise the real RecompInput frontend event gate without a ROM or window."""
import json
from pathlib import Path
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
build = root / 'build-renderer-stack'
commands = json.loads((build / 'compile_commands.json').read_text())
source = root / '.deps-renderer/RecompFrontend/recompinput/src/input_events.cpp'
entry = next(c for c in commands if Path(c['file']) == source)
command = shlex.split(entry['command'])
flags = [arg for arg in command if arg.startswith(('-I', '-D'))]
with tempfile.TemporaryDirectory(prefix='sbk-capture-events-') as temp:
    binary = Path(temp) / 'capture_events'
    subprocess.run([command[0], '-std=c++20', '-O0', '-ffunction-sections', '-fdata-sections',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    *flags, str(source), str(root / 'tests/controls/capture_events.cpp'),
                    '-Wl,--gc-sections', '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
