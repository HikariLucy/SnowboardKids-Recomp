#!/usr/bin/env python3
"""Live GPU restore-lifecycle gate (P6-XPROC-01). Needs the ROM, a display and a GPU.

No keyboard input: requests go through the DEVELOPMENT ONLY control file
(SBK_P4_SAVESTATE_DEV). After every restore the next freeze must reach Frozen:
that proves the Graphics participant drained, i.e. RT64 kept consuming
workloads and presents. Guest dispatches and the frontend heartbeat must also
keep advancing after the last restore.

  same-process : one process, dev capture, then --restores dev restores.
  cross-process: process A quicksaves slot 'lifecycle-gate' during the
                 attract loop and exits; for each --delays value a fresh
                 process B quickloads it and must pass the same oracle.

Before the fix a hang appeared in roughly half of the restores. Passing is not
the human gate: image, input and window close are still checked by a person.
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
SLOT = 'lifecycle-gate'
HEARTBEAT = re.compile(r'P4A heartbeat frame=(\d+) live_owners=(\d+) .*total_dispatches=(\d+)')


class Game:
    def __init__(self, workdir, tag):
        self.control = workdir / f'{tag}.control'
        self.control.write_text('')
        self.log_path = workdir / f'{tag}.log'
        self.sequence = 0
        env = dict(os.environ, SBK_P4_SAVESTATE_DEV='1', SBK_P4_SAVESTATE_CONTROL=str(self.control))
        self.log = self.log_path.open('w')
        self.process = subprocess.Popen(
            [str(BUILD / 'SnowboardKidsRecompiled'), 'runtime-data/snowboardkids.n64.us.z64'],
            cwd=BUILD, env=env, stdout=self.log, stderr=subprocess.STDOUT)

    def text(self):
        return self.log_path.read_text(errors='replace')

    def wait_for(self, pattern, timeout):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            match = re.search(pattern, self.text())
            if match:
                return match
            if self.process.poll() is not None:
                raise RuntimeError(f'game exited early ({self.process.returncode}); see {self.log_path}')
            time.sleep(0.1)
        raise RuntimeError(f'timeout waiting for {pattern!r}; see {self.log_path}')

    def request(self, command):
        self.sequence += 1
        self.control.write_text(f'{self.sequence} {command}\n')
        return self.sequence

    def heartbeat(self):
        beats = HEARTBEAT.findall(self.text())
        return (int(beats[-1][0]), int(beats[-1][2])) if beats else (0, 0)

    def close(self):
        self.process.kill()
        self.process.wait()
        self.log.close()


def outcome(game, generation, timeout=20):
    # Driver prints one of these per generation; TIMEOUT means a participant never drained.
    match = game.wait_for(rf'(?:RESTORE|CAPTURE) (ok|ROLLED_BACK|REJECTED|UNRECOVERABLE) gen={generation} '
                          rf'|TIMEOUT gen={generation} state=(\w+)', timeout)
    return match.group(1) or f'TIMEOUT:{match.group(2)}'


def check_progress(game, label):
    frame, dispatches = game.heartbeat()
    time.sleep(3)
    later_frame, later_dispatches = game.heartbeat()
    ok = later_frame > frame and later_dispatches > dispatches
    print(f'{"PASS" if ok else "FAIL"} {label}: heartbeat {frame}->{later_frame} dispatches {dispatches}->{later_dispatches}')
    return ok


def same_process(workdir, restores, warmup):
    game = Game(workdir, 'same')
    ok = True
    try:
        game.wait_for(r'live_owners=5', 120)
        time.sleep(warmup)
        game.request('capture')
        ok &= outcome(game, 1) == 'ok'
        for generation in range(2, restores + 2):
            game.request('restore')
            result = outcome(game, generation)
            ok &= result == 'ok'
            print(f'{"PASS" if result == "ok" else "FAIL"} same-process restore gen={generation}: {result}')
            time.sleep(2 + generation % 3)
        game.request('capture')  # the freeze after the last restore must drain too
        result = outcome(game, restores + 2)
        ok &= result == 'ok'
        print(f'{"PASS" if result == "ok" else "FAIL"} same-process drain after last restore: {result}')
        ok &= check_progress(game, 'same-process progress')
    finally:
        game.close()
    return ok


def cross_process(workdir, delays, warmup):
    save = BUILD / 'runtime-data/savestates' / f'{SLOT}.sbks'
    game = Game(workdir, 'process-a')
    try:
        game.wait_for(r'live_owners=5', 120)
        time.sleep(warmup)
        game.request(f'quicksave {SLOT}')
        game.wait_for(rf'SAVESTATE QUICKSAVE ok slot={SLOT}', 30)
    finally:
        game.close()
    ok = True
    try:
        for delay in delays:
            game = Game(workdir, f'process-b-{delay}')
            try:
                game.wait_for(r'live_owners=5', 120)
                time.sleep(delay)
                game.request(f'quickload {SLOT}')
                result = outcome(game, 1)
                good = result == 'ok'
                if good:
                    time.sleep(4)
                    game.request('capture')
                    drained = outcome(game, 2)
                    good = drained == 'ok'
                    result += f', next freeze {drained}'
                    good &= check_progress(game, f'process B delay={delay}s progress')
                ok &= good
                print(f'{"PASS" if good else "FAIL"} cross-process delay={delay}s: {result}')
            finally:
                game.close()
    finally:
        save.unlink(missing_ok=True)
    return ok


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--restores', type=int, default=8)
    parser.add_argument('--delays', type=int, nargs='+', default=[30, 35, 40, 45, 55, 60])
    parser.add_argument('--warmup', type=int, default=30, help='seconds of attract loop before capture/save')
    parser.add_argument('--keep-logs', type=Path, help='copy logs here')
    args = parser.parse_args()
    if not (BUILD / 'runtime-data/snowboardkids.n64.us.z64').exists():
        raise SystemExit('ROM missing: build-renderer-stack/runtime-data/snowboardkids.n64.us.z64')
    with tempfile.TemporaryDirectory(prefix='sbk-lifecycle-live-') as directory:
        workdir = Path(directory)
        ok = same_process(workdir, args.restores, args.warmup)
        ok &= cross_process(workdir, args.delays, args.warmup)
        if args.keep_logs:
            args.keep_logs.mkdir(parents=True, exist_ok=True)
            for log in workdir.glob('*.log'):
                (args.keep_logs / log.name).write_bytes(log.read_bytes())
    print(f'renderer lifecycle live gate: {"PASS" if ok else "FAIL"}')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
