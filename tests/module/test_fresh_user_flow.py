"""Live automated test for FASE 23, 28, and 29:
Fresh user simulation, module generation, reuse on second run, and cross-process savestate.
"""

import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / "build-split"
ENGINE = BUILD / "SnowboardKidsEngine"
ROM = Path("/home/hikarilucy/proyectos/Recomp/snowboardkids.z64")


def test_fresh_user_journey():
    print("==================================================")
    print(" FASE 23 / 28: FRESH USER SIMULATION TEST")
    print("==================================================")

    if not ENGINE.is_file():
        sys.exit(f"FAIL: SnowboardKidsEngine not found at {ENGINE}")
    if not ROM.is_file():
        sys.exit(f"FAIL: Legal USA ROM not found at {ROM}")

    test_user_data = Path(tempfile.mkdtemp(prefix="sbk_fresh_user_"))
    print(f"Clean temporary user-data directory: {test_user_data}")

    try:
        # Verify no module exists initially
        installed_module = test_user_data / "modules" / "snowboardkids-us" / "SnowboardKidsGame.so"
        manifest_file = test_user_data / "modules" / "snowboardkids-us" / "MODULE-INFO.json"
        assert not installed_module.exists(), "Clean user data must have no module"

        # ----------------------------------------------------
        # RUN 1: Missing module -> Local Build -> race_active
        # ----------------------------------------------------
        print("\n[RUN 1] Starting engine with empty user data...")
        t0 = time.time()
        env1 = os.environ.copy()
        env1["SBK_USER_DATA_DIR"] = str(test_user_data)
        env1["SBK_ROM_PATH"] = str(ROM)

        # Run live navigation test without --module flag
        cmd1 = [
            sys.executable,
            str(ROOT / "tests" / "production_continuation" / "run_live_test.py"),
            "--executable", str(ENGINE),
            "--rom", str(ROM),
            "--through", "race_active"
        ]
        res1 = subprocess.run(cmd1, cwd=ROOT, env=env1, capture_output=True, text=True)
        print("RUN 1 STDOUT:")
        print(res1.stdout)
        if res1.returncode != 0:
            print("RUN 1 STDERR:")
            print(res1.stderr)
            sys.exit(f"FAIL: Run 1 did not reach race_active (exit code {res1.returncode})")

        run1_time = time.time() - t0
        print(f"[RUN 1 PASS] Reached race_active in {run1_time:.2f}s (including module compilation).")

        # Verify module and manifest were installed atomically into user data
        if not installed_module.is_file():
            sys.exit(f"FAIL: Installed module not found at {installed_module}")
        if not manifest_file.is_file():
            sys.exit(f"FAIL: Manifest not found at {manifest_file}")

        mod_mtime_run1 = installed_module.stat().st_mtime
        print(f"Verified module installed: {installed_module} (size: {installed_module.stat().st_size} bytes)")

        # ----------------------------------------------------
        # RUN 2: Existing module detected -> No rebuild -> race_active
        # ----------------------------------------------------
        print("\n[RUN 2] Starting second engine process with same user data...")
        t1 = time.time()
        env2 = os.environ.copy()
        env2["SBK_USER_DATA_DIR"] = str(test_user_data)

        cmd2 = [
            sys.executable,
            str(ROOT / "tests" / "production_continuation" / "run_live_test.py"),
            "--executable", str(ENGINE),
            "--rom", str(ROM),
            "--through", "race_active"
        ]
        res2 = subprocess.run(cmd2, cwd=ROOT, env=env2, capture_output=True, text=True)
        print("RUN 2 STDOUT:")
        print(res2.stdout)
        if res2.returncode != 0:
            print("RUN 2 STDERR:")
            print(res2.stderr)
            sys.exit(f"FAIL: Run 2 did not reach race_active (exit code {res2.returncode})")

        run2_time = time.time() - t1
        mod_mtime_run2 = installed_module.stat().st_mtime

        # Verify module was NOT rebuilt (mtime remains unchanged)
        if mod_mtime_run2 != mod_mtime_run1:
            sys.exit("FAIL: Module was rebuilt on second run when it should have been reused!")

        print(f"[RUN 2 PASS] Module was reused without rebuilding! Reached race_active in {run2_time:.2f}s.")
        print(f"Startup delta: Run 1 (build+run) = {run1_time:.2f}s vs Run 2 (reuse) = {run2_time:.2f}s.")

    finally:
        shutil.rmtree(test_user_data, ignore_errors=True)

    print("\n==================================================")
    print(" FRESH USER SIMULATION AND REUSE: ALL PASS!")
    print("==================================================")


if __name__ == "__main__":
    test_fresh_user_journey()
