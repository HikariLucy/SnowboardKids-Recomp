#!/usr/bin/env python3
"""Run Snowboard Kids live quiescence test with controller automation and GPU fence checks."""
import argparse
import os
import signal
import subprocess
import sys
import threading
import time
from pathlib import Path

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cycles', type=int, default=30)
    parser.add_argument('--start-ms', type=int, default=2000)
    parser.add_argument('--interval-ms', type=int, default=200)
    parser.add_argument('--hold-ms', type=int, default=15)
    parser.add_argument('--log', type=Path, default=Path('/tmp/p2_live.log'))
    parser.add_argument('--timeout', type=int, default=120)
    parser.add_argument('--race-nav', action='store_true', help='Send buttons to navigate menus into a race')
    args = parser.parse_args()

    root = Path(__file__).resolve().parents[2]
    build_dir = root / 'build-renderer-stack'
    executable = build_dir / 'SnowboardKidsRecompiled'
    rom = build_dir / 'runtime-data/snowboardkids.n64.us.z64'

    control_file = build_dir / 'test_controller.txt'
    control_file.write_text('0000\n')

    env = os.environ.copy()
    env['SBK_P2_CYCLES'] = str(args.cycles)
    env['SBK_P2_START_MS'] = str(args.start_ms)
    env['SBK_P2_INTERVAL_MS'] = str(args.interval_ms)
    env['SBK_P2_HOLD_MS'] = str(args.hold_ms)
    env['SBK_P2_CONTROL_FILE'] = str(control_file)

    graphics_json = build_dir / 'runtime-data/graphics.json'
    if graphics_json.exists():
        import json
        cfg = json.loads(graphics_json.read_text())
        assert cfg.get('ar_option') == 'Original', f"Initial ar_option must be Original, got {cfg.get('ar_option')}"

    stop_input = threading.Event()

    def feed_input():
        # Sequence of button presses to navigate intro -> menu -> race
        # Start = 0x1000, A = 0x8000, B = 0x4000
        # Button timing in seconds from launch
        schedule = [
            (1.5, '1000'), (1.8, '0000'),
            (2.5, '1000'), (2.8, '0000'),
            (3.5, '8000'), (3.8, '0000'),
            (4.5, '8000'), (4.8, '0000'),
            (5.5, '8000'), (5.8, '0000'),
            (6.5, '8000'), (6.8, '0000'),
            (7.5, '8000'), (7.8, '0000'),
            (8.5, '8000'), (8.8, '0000'),
            (9.5, '8000'), (9.8, '0000'),
            (10.5, '8000'), (10.8, '0000'),
        ]
        start_time = time.time()
        sched_idx = 0
        while not stop_input.is_set():
            now = time.time() - start_time
            if args.race_nav:
                if sched_idx < len(schedule):
                    t, btn = schedule[sched_idx]
                    if now >= t:
                        control_file.write_text(f'{btn}\n')
                        sched_idx += 1
                else:
                    # During race: repeatedly tap A and occasionally hold forward
                    cycle_pos = int((now - 11.0) * 4) % 4
                    btn = '8000' if (cycle_pos % 2 == 0) else '0000'
                    control_file.write_text(f'{btn}\n')
            time.sleep(0.05)

    input_thread = threading.Thread(target=feed_input, daemon=True)
    input_thread.start()

    log_file = open(args.log, 'w')
    p = subprocess.Popen(
        [str(executable), str(rom)],
        cwd=str(build_dir),
        env=env,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
        text=True,
        bufsize=1
    )

    complete_seen = False
    failed_seen = False
    timeout_seen = False
    deadline = time.time() + args.timeout

    def stream_stderr():
        nonlocal complete_seen, failed_seen, timeout_seen
        for line in p.stderr:
            log_file.write(line)
            log_file.flush()
            if 'P2 COMPLETE' in line:
                complete_seen = True
            if 'P2 FAILED' in line:
                failed_seen = True
            if 'P2 TIMEOUT' in line:
                timeout_seen = True

    err_thread = threading.Thread(target=stream_stderr, daemon=True)
    err_thread.start()

    while time.time() < deadline:
        if p.poll() is not None:
            break
        if complete_seen or failed_seen or timeout_seen:
            # Let it run briefly to ensure any trailing diagnostic flushed
            time.sleep(0.5)
            break
        time.sleep(0.1)

    stop_input.set()

    # Gracefully shut down the process
    if p.poll() is None:
        p.send_signal(signal.SIGINT)
        try:
            p.wait(timeout=2)
        except subprocess.TimeoutExpired:
            p.send_signal(signal.SIGKILL)
            p.wait()

    log_file.close()
    if control_file.exists():
        control_file.unlink()

    if graphics_json.exists():
        import json
        cfg = json.loads(graphics_json.read_text())
        assert cfg.get('ar_option') == 'Original', f"Final ar_option must be Original, got {cfg.get('ar_option')}"

    # Run summarize.py to strictly validate the log
    summarize_cmd = [
        sys.executable, str(root / 'tests/quiescence/summarize.py'), str(args.log),
        '--require-gpu-fences', '--check-aspect-ratio', '--graphics-config', str(graphics_json)
    ]
    res = subprocess.run(summarize_cmd)
    if res.returncode != 0:
        sys.exit(res.returncode)

if __name__ == '__main__':
    main()
