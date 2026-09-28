#!/usr/bin/env python3
"""Bounded guest-input PFS init probe; no desktop automation or human save claim."""
import argparse
import os
from pathlib import Path
import re
import selectors
import signal
import subprocess
import tempfile
import time

parser = argparse.ArgumentParser()
parser.add_argument("--executable", required=True, type=Path)
parser.add_argument("--rom", required=True, type=Path)
parser.add_argument("--absent", action="store_true")
args = parser.parse_args()
build = args.executable.resolve().parent
with tempfile.TemporaryDirectory(prefix="sbk-pfs-live-") as temp:
    control = Path(temp) / "control.txt"
    control.write_text("0000\n")
    env = dict(os.environ, SBK_PFS_TRACE="1", SBK_SAVESTATES="0",
               SBK_P4A_LIVE_NAVIGATION="1", SBK_P2_CONTROL_FILE=str(control),
               SBK_RESOLUTION="original", SBK_PFS_ABSENT="1" if args.absent else "0")
    process = subprocess.Popen(
        [str(args.executable.resolve()), str(args.rom.resolve())],
        cwd=build, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        start_new_session=True)
    expected = 1 if args.absent else 0
    seen = []
    demo = False
    pending = b""
    deadline = time.monotonic() + 65
    selector = selectors.DefaultSelector()
    selector.register(process.stdout, selectors.EVENT_READ)
    try:
        while time.monotonic() < deadline and process.poll() is None:
            if not selector.select(0.5):
                continue
            chunk = os.read(process.stdout.fileno(), 65536)
            if not chunk:
                break
            pending += chunk
            lines = pending.split(b"\n")
            pending = lines.pop()
            for raw in lines:
                line = raw.decode(errors="replace")
                if "P4A milestone: title_demo_entered" in line:
                    demo = True
                tick = re.search(r"P4A input_frame=(\d+)", line)
                if tick and demo:
                    control.write_text("1000\n" if int(tick[1]) % 6 == 0 else "0000\n")
                match = re.search(r"PFS INIT port=1 file=-1 identity=[0-9a-f]+ offset=0 length=0 result=(\d+)", line)
                if match:
                    seen.append(int(match[1]))
                    if expected in seen:
                        break
            if expected in seen:
                break
    finally:
        os.killpg(process.pid, signal.SIGTERM)
        try:
            process.wait(timeout=1)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            process.wait()
        selector.close()
        process.stdout.close()
    assert expected in seen, f"expected guest PFS init result {expected}, observed {seen}"
    print(f"PASS guest PFS INIT result={expected} ({'absent' if args.absent else 'present'})")
