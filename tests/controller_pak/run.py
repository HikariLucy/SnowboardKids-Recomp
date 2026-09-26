#!/usr/bin/env python3
"""Developer wrapper: build the CMake Controller Pak suite and run it via CTest.

CTest registers the same binary directly (test `controller_pak`); this only
adds a one-command sanitizer build, e.g. `run.py --sanitize address,undefined`.
The compiler is whatever CMake selects; nothing is compiled from here.
"""
from pathlib import Path
import argparse
import os
import subprocess

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument("--sanitize", default=os.environ.get("SBK_PFS_SANITIZER", ""),
                    help="-fsanitize= list (GCC/Clang only)")
parser.add_argument("--build-dir", type=Path)
args = parser.parse_args()
# build-pfs/ is git-ignored; one subdirectory per sanitizer set.
build = args.build_dir or root / "build-pfs" / (args.sanitize.replace(",", "-") or "plain")

subprocess.run(["cmake", "-S", str(root), "-B", str(build), "-DSBK_ROM_FREE_CI=ON",
                "-DCMAKE_BUILD_TYPE=Release", "-DSBK_PFS_SANITIZER=" + args.sanitize], check=True)
subprocess.run(["cmake", "--build", str(build), "--config", "Release",
                "--target", "SnowboardKidsControllerPakTest"], check=True)
subprocess.run(["ctest", "--test-dir", str(build), "-C", "Release", "-R", "^controller_pak$",
                "--output-on-failure", "-V"], check=True)
