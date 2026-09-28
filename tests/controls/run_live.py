#!/usr/bin/env python3
"""CONTROL-P1 live input check with a virtual SDL game controller (ROM + window).

Uses the DEVELOPMENT ONLY probe (SBK_TEST_PAD_FILE, src/main/virtual_pad.cpp):
a virtual controller goes through the real SDL GameController, RecompInput
bindings and deadzone, the runtime and osContGetReadData into the guest. The
probe logs the guest's pad bytes and the game's mapped stick after its
response curve. Runs at the title screen, so no save is needed. Nothing is
written to the configuration. This does not replace the human gate: it has no
physical stick, no remap UI and no feel.
"""
from pathlib import Path
import argparse
import os
import re
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / 'build-renderer-stack'
GUEST = re.compile(r'VPAD guest pad0 button=([0-9a-f]{4}) stick_x=(-?\d+) stick_y=(-?\d+) err=\d+ \| '
                   r'stickX=(-?\d+) stickY=(-?\d+) held=([0-9a-f]{8})')
# SDL_GameControllerButton bits
A, X, START, DPAD_UP = 1 << 0, 1 << 2, 1 << 6, 1 << 11


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--workdir', type=Path, default=BUILD, help='directory holding runtime-data/')
    args = parser.parse_args()
    rom = args.workdir / 'runtime-data/snowboardkids.n64.us.z64'
    if not rom.exists():
        raise SystemExit(f'ROM missing: {rom}')

    with tempfile.TemporaryDirectory(prefix='sbk-controls-live-') as directory:
        pad = Path(directory) / 'pad.txt'
        log_path = Path(directory) / 'game.log'
        pad.write_text('0 0 0\n')
        env = dict(os.environ, SBK_TEST_PAD_FILE=str(pad))
        with log_path.open('w') as log:
            game = subprocess.Popen([str(BUILD / 'SnowboardKidsRecompiled'), 'runtime-data/snowboardkids.n64.us.z64'],
                                    cwd=args.workdir, env=env, stdout=log, stderr=subprocess.STDOUT)
        failures = 0

        def text():
            return log_path.read_text(errors='replace')

        def apply(line, expect, timeout=4.0):
            # The attract loop has scene transitions of a few seconds without pad
            # reads: wait for a guest read that satisfies the expectation.
            start = len(text())
            pad.write_text(line + '\n')
            deadline = time.monotonic() + timeout
            reads = []
            while time.monotonic() < deadline:
                reads = GUEST.findall(text()[start:])
                if any(expect(r) for r in reads):
                    return next(r for r in reversed(reads) if expect(r)), True
                time.sleep(0.1)
            return (reads[-1] if reads else None), False

        def check(ok, what):
            nonlocal failures
            print(f"{'PASS' if ok else 'FAIL'} {what}", flush=True)
            failures += not ok

        def expect(line, predicate, what):
            state, ok = apply(line, predicate)
            check(ok, f'{what} ({state})')

        stick = lambda r: (int(r[1]), int(r[2]), int(r[3]), int(r[4]))
        neutral = lambda r: stick(r) == (0, 0, 0, 0)

        try:
            deadline = time.monotonic() + 60
            while 'live_owners=5' not in text():
                if game.poll() is not None or time.monotonic() > deadline:
                    raise SystemExit('game did not reach its threads; see log')
                time.sleep(0.5)
            time.sleep(3)
            check('VPAD attach ok' in text(), 'virtual controller attached as an SDL game controller')

            expect('32767 0 0', lambda r: stick(r)[0] >= 80 and stick(r)[1] == 0 and stick(r)[2] == 31,
                   'full right -> stick_x>=80, game stickX 31')
            expect('0 0 0', neutral, 'release -> neutral')
            expect('-32768 0 0', lambda r: stick(r)[0] <= -80 and stick(r)[2] == -31, 'full left -> stick_x<=-80, game stickX -31')
            expect('0 -32768 0', lambda r: stick(r)[1] >= 80 and stick(r)[0] == 0 and stick(r)[3] == 31,
                   'full up -> stick_y>=80, game stickY 31')
            expect('16384 0 0', lambda r: 30 <= stick(r)[0] <= 45 and 0 < stick(r)[2] < 31, 'half right -> partial stick and game stickX')
            expect('23170 -23170 0', lambda r: stick(r)[0] == stick(r)[1] and 60 <= stick(r)[0] <= 72,
                   'diagonal -> equal axes on the octagon')
            expect('0 0 0', neutral, 'release -> neutral')
            # Default single-player controller profile: SDL A -> N64 A, SDL X -> N64 B.
            for bit, n64, name in ((A, 0x8000, 'A'), (X, 0x4000, 'X (N64 B)'), (DPAD_UP, 0x0800, 'D-pad up')):
                expect(f'0 0 {bit:x}', lambda r, n64=n64: int(r[0], 16) & n64 and stick(r)[:2] == (0, 0),
                       f'{name} -> N64 {n64:04x}, stick stays neutral')
                expect('0 0 0', lambda r: r[0] == '0000', f'{name} released')
            expect(f'32767 0 {A:x}', lambda r: int(r[0], 16) & 0x8000 and stick(r)[0] >= 80, 'hold right+A')
            expect('detach', lambda r: r[0] == '0000' and neutral(r) and r[5] == '00000000',
                   'disconnect while holding right+A -> released and neutral')
            expect('-32768 0 0', lambda r: stick(r)[0] <= -80, 'reconnect -> input resumes')
            expect(f'0 0 {START:x}', lambda r: int(r[0], 16) & 0x1000, 'Start -> N64 1000')
        finally:
            game.kill()
            game.wait()
        print(f"{'FAIL' if failures else 'PASS'} controls live ({failures} failure{'s' if failures != 1 else ''})")
        return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
