#!/usr/bin/env python3
"""Bounded P4-A navigation; no snapshots or race completion.

Original flow (snowboardkids-decomp): race/flow/race_flow.c routes startup
through enterMainMenuFromRace. demo/title_demo_race_intro.c consumes START,
then finishTitleDemoRaceIntro resumes menu task 3. START confirms default
selections and acknowledges Pak messages in race_setup_menu.c. Default race mode 0
uses initMultiplayerCourseSelectMenu even for one player, then initRaceSceneFlow.
Input timing uses guest controller reads, never wall-clock navigation sleeps.
"""
import fcntl
import os
from pathlib import Path
import re
import selectors
import signal
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
STAGES = ('controller_pak', 'menu_navigation', 'character_select',
          'course_select', 'race_active')


def instances(executable):
    result = []
    for entry in Path('/proc').iterdir():
        if entry.name.isdigit():
            try:
                if (entry / 'exe').resolve(strict=True) == executable:
                    result.append(int(entry.name))
            except (FileNotFoundError, PermissionError, ProcessLookupError):
                pass
    return result


def cleanup(process):
    # Signal the group even if its leader has already exited.
    try:
        os.killpg(process.pid, signal.SIGTERM)
    except ProcessLookupError:
        pass
    try:
        process.wait(timeout=0.25)
    except subprocess.TimeoutExpired:
        pass
    try:
        os.killpg(process.pid, signal.SIGKILL)
    except ProcessLookupError:
        pass
    process.wait()  # reap after SIGKILL, including graphics-driver teardown


def main():
    build = ROOT / 'build-renderer-stack'
    executable = (build / 'SnowboardKidsRecompiled').resolve()
    with open('/tmp/sbk-p4a-live-navigation.lock', 'w') as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            print('FAIL: another navigation harness is running')
            return 1
        if instances(executable):
            print('FAIL: SnowboardKidsRecompiled already running; no instance launched')
            return 1
        return run(build, executable)


def run(build, executable):
    seen = []
    backend = boot = demo = False
    errors = []
    fallbacks = 0
    log_path = Path('/tmp/p4a-live-navigation.log')
    with tempfile.TemporaryDirectory(prefix='sbk-p4a-navigation-') as tmp:
        control = Path(tmp) / 'controller.txt'

        def buttons(value):
            pending = control.with_suffix('.next')
            pending.write_text(f'{value:04x}\n')
            pending.replace(control)

        buttons(0)
        env = os.environ.copy()
        env.update(SBK_CONTINUATIONS='ON', SBK_P2_CYCLES='0', SBK_RESOLUTION='original',
                   SBK_P2_CONTROL_FILE=str(control), SBK_P4A_LIVE_NAVIGATION='1')
        # Reserve one and a half seconds of the 20-second budget for group cleanup.
        deadline = time.monotonic() + 18.5
        with log_path.open('w') as log, selectors.DefaultSelector() as selector:
            process = subprocess.Popen(
                [str(executable), str(build / 'runtime-data/snowboardkids.n64.us.z64')],
                cwd=build, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                start_new_session=True)
            try:
                selector.register(process.stdout, selectors.EVENT_READ)
                pending = b''
                done = False
                while not done:
                    remaining = deadline - time.monotonic()
                    if remaining <= 0 or not selector.select(remaining):
                        errors.append('20s budget exhausted before interactive race')
                        break
                    chunk = os.read(process.stdout.fileno(), 65536)
                    if not chunk:
                        errors.append('process exited before interactive race')
                        break
                    log.write(chunk.decode(errors='replace'))
                    pending += chunk
                    lines = pending.split(b'\n')
                    pending = lines.pop()
                    for raw in lines:
                        line = raw.decode(errors='replace')
                        backend |= 'P4A backend=continuation ' in line
                        boot |= 'P4A startup registered backend=continuation' in line
                        if re.search(r'fallback|Native generated .*forbidden|rejected unresolved|terminate called|SIGSEGV', line, re.I):
                            errors.append(line.strip())
                            fallbacks += int('fallback' in line.lower())
                        match = re.search(r'P4A milestone: (\w+) \(', line)
                        if match:
                            stage = match[1]
                            if stage == 'title_demo_entered':
                                demo = True
                                if 'menu_navigation' in seen:
                                    errors.append('returned to demo after main menu')
                            elif stage in STAGES and stage not in seen:
                                if stage != STAGES[len(seen)] or not boot:
                                    errors.append(f'out-of-order milestone: {stage}')
                                else:
                                    seen.append(stage)
                            # demo_race_players is diagnostic only, never a gate.
                        tick = re.search(r'P4A input_frame=(\d+)', line)
                        if tick and demo:
                            phase = int(tick[1]) % 6
                            if phase == 0:
                                buttons(0x1000)
                            elif phase == 2:
                                buttons(0)
                        if errors or 'race_active' in seen:
                            done = True
                            break
            finally:
                try:
                    buttons(0)
                finally:
                    cleanup(process)
                    process.stdout.close()
    residual = instances(executable)
    if residual:
        errors.append(f'residual processes: {residual}')
    print('P4-A LIVE NAVIGATION')
    print(f'boot: {"PASS" if boot else "FAIL"}')
    for stage in STAGES:
        print(f'{stage}: {"PASS" if stage in seen else "FAIL"}')
    print(f'title_demo_entered: {"YES (initial demo permitted)" if demo else "NO"}')
    print(f'continuation_backend: {"ON" if backend else "OFF"}')
    # The production dispatcher rejects native reentry; there is no fallback path.
    print(f'native_suspendable_fallbacks: {fallbacks if backend and boot else "UNKNOWN"}')
    print(f'residual_processes: {len(residual)}')
    passed = boot and backend and seen == list(STAGES) and not errors
    if not passed:
        print(f'FAIL: {"; ".join(errors) or "missing milestones"}; log={log_path}')
    return 0 if passed else 1


if __name__ == '__main__':
    raise SystemExit(main())
