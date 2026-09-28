#!/usr/bin/env python3
"""Guest-visible isolation through two to four SDL virtual controllers."""
import argparse
import os
from pathlib import Path
import re
import selectors
import signal
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / 'build-renderer-stack'
ROM = BUILD / 'runtime-data/snowboardkids.n64.us.z64'
EXECUTABLE = BUILD / 'SnowboardKidsRecompiled'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--players', type=int, choices=(2, 3, 4), default=4)
    players = parser.parse_args().players
    if not ROM.exists() or not EXECUTABLE.exists():
        raise SystemExit('build and private ROM are required')
    with tempfile.TemporaryDirectory(prefix='sbk-multi-live-') as directory:
        control = Path(directory) / 'pads'

        def write(state):
            next_file = control.with_suffix('.next')
            next_file.write_text(state + '\n')
            next_file.replace(control)

        # SDL left X, left Y, SDL button bitmask. Port 2 has A, port 3 Start.
        initial = ['-32768 0 0', '32767 0 0', '0 0 1', '0 0 40']
        write(f'{players} ' + ' '.join(['0 0 0'] * players))
        env = os.environ.copy()
        env.update(SBK_CONTINUATIONS='ON', SBK_P2_CYCLES='0',
                   SBK_RESOLUTION='original', SBK_TEST_MULTI_PAD_FILE=str(control))
        process = subprocess.Popen([str(EXECUTABLE), str(ROM)], cwd=BUILD, env=env,
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                   start_new_session=True)
        stage = 0
        deadline = time.monotonic() + 25
        lines = []
        pending = b''
        try:
            with selectors.DefaultSelector() as selector:
                selector.register(process.stdout, selectors.EVENT_READ)
                while stage < 3:
                    remaining = deadline - time.monotonic()
                    if remaining <= 0 or not selector.select(remaining):
                        raise AssertionError(f'timeout in stage {stage}')
                    chunk = os.read(process.stdout.fileno(), 65536)
                    if not chunk:
                        raise AssertionError(f'guest exited in stage {stage}')
                    pending += chunk
                    complete = pending.split(b'\n')
                    pending = complete.pop()
                    for raw in complete:
                        line = raw.decode(errors='replace')
                        lines.append(line)
                        if re.search(r'fallback|SIGSEGV|terminate called', line, re.I):
                            raise AssertionError(line)
                        if line.startswith(f'VMULTI assigned={players}'):
                            write(f'{players} ' + ' '.join(initial[:players]))
                        if not line.startswith('VMULTI guest'):
                            continue
                        initial_guest = ('p0=0000,-8[0-2],0,0 p1=0000,8[0-2],0,0' +
                                         (' p2=8000,0,0,0' if players >= 3 else '') +
                                         (' p3=1000,0,0,0' if players == 4 else ''))
                        if stage == 0 and re.search(
                                rf'mask={(1 << players) - 1:02x} count={players} {initial_guest}', line):
                            print(f'{players} ports: independent inputs')
                            if players == 2:
                                write('2 0 0 0 0 0 0')
                                stage = 2
                                deadline = time.monotonic() + 10
                                continue
                            detached = ['-32768 0 40', '32767 0 0', '-99999 0 0', '0 0 40']
                            write(f'{players} ' + ' '.join(detached[:players]))
                            stage = 1
                            deadline = time.monotonic() + 25
                        elif stage == 1 and re.search(
                                r'p0=1000,-8[0-2],0,0 p1=0000,8[0-2],0,0 '
                                r'p2=0000,0,0,8' +
                                (r' p3=1000,0,0,0' if players == 4 else ''), line):
                            print('P3 detached: neutral/no response; P1/P2/P4 intact')
                            neutral = ['0 0 0', '0 0 0', '-99999 0 0', '0 0 0']
                            write(f'{players} ' + ' '.join(neutral[:players]))
                            stage = 2
                            deadline = time.monotonic() + 10
                        elif stage == 2 and ('p0=0000,0,0,0 p1=0000,0,0,0' +
                                              (' p2=0000,0,0,8' if players >= 3 else '') +
                                              (' p3=0000,0,0,0' if players == 4 else '')) in line:
                            print('release/neutral: PASS')
                            stage = 3
        except Exception:
            Path('/tmp/sbk-multi-live-failure.log').write_text('\n'.join(lines))
            raise
        finally:
            if process.poll() is None:
                os.killpg(process.pid, signal.SIGTERM)
                try:
                    process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    os.killpg(process.pid, signal.SIGKILL)
                    process.wait(timeout=5)
        return 0


if __name__ == '__main__':
    raise SystemExit(main())
