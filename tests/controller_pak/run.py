#!/usr/bin/env python3
"""Build the real backend and run isolated, cross-process raw Pak checks."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
sanitizer = os.environ.get("SBK_PFS_SANITIZER", "")
flags = ["-fsanitize=" + sanitizer, "-fno-omit-frame-pointer"] if sanitizer else []
with tempfile.TemporaryDirectory(prefix="sbk-pfs-") as scratch:
    scratch = Path(scratch)
    binary = scratch / "pak-test"
    subprocess.run([
        "c++", "-std=c++20", "-O1", "-g", "-Wall", "-Wextra", "-Werror", *flags,
        "-I" + str(root / "src"),
        str(root / "tests/controller_pak/main.cpp"),
        str(root / "src/pfs/controller_pak.cpp"), "-o", str(binary),
    ], check=True)
    def run(*args):
        subprocess.run([str(binary), *map(str, args)], check=True)
    run("normal", scratch / "normal")
    durable = scratch / "durable"
    run("a", durable)
    run("b", durable)
    run("c", durable)
    for kind in ("truncate", "wrong-size", "id", "id-all", "inode", "directory", "chain"):
        run("corrupt", scratch / kind, kind)
    hle = scratch / "hle-test"
    include = root / ".deps-runtime/N64ModernRuntime/N64Recomp/include"
    if not include.exists():
        include = Path(os.environ["SBK_RECOMP_INCLUDE"])
    subprocess.run([
        "c++", "-std=c++20", "-O1", "-g", "-Wall", "-Wextra", *flags,
        "-I" + str(root / "src"), "-I" + str(include),
        str(root / "tests/controller_pak/hle.cpp"),
        str(root / "src/pfs/hle.cpp"),
        str(root / "src/pfs/controller_pak.cpp"), "-o", str(hle),
    ], check=True)
    subprocess.run([str(hle), str(scratch / "hle")], check=True)
