#!/usr/bin/env python3
"""Compile RT64's and RecompFrontend's shaders with the pinned DXC release.

Windows packages ship dxcompiler.dll/dxil.dll from the official Microsoft
DirectXShaderCompiler release pinned in scripts/dxc_redist.py, not the
unsigned development build RT64's contrib compiles with at build time. RT64
compiles the same HLSL sources at run time through dxcompiler.dll, so every
shader command of a configured build is replayed here with the pinned
compiler (arguments unchanged, outputs to a temporary directory). A failure
means the release cannot compile a shader the renderer uses.

    python tests/release/dxc_release_compat.py --build build-engine
"""
import argparse
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
import dxc_redist  # noqa: E402

CONTRIB_DXC = re.compile(r"contrib[/\\]dxc[/\\]bin[/\\](?:x64|arm64)[/\\]dxc(?:\.exe|-linux|-macos)\"?",
                         re.IGNORECASE)


def shader_commands(build: Path):
    listing = subprocess.run(["ninja", "-C", str(build), "-t", "commands"], capture_output=True,
                             text=True, check=True).stdout
    for line in listing.splitlines():
        match = CONTRIB_DXC.search(line)
        if not match:
            continue
        tail = line[match.end():].strip()
        if tail.endswith('"') and tail.count('"') % 2:
            tail = tail[:-1]  # closing quote of a Windows "cmd /C" wrapper
        if sys.platform == "win32":
            args = [a[1:-1] if len(a) > 1 and a[0] == a[-1] == '"' else a
                    for a in shlex.split(tail, posix=False)]
        else:
            args = shlex.split(tail)
        yield args


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build", type=Path, required=True, help="configured Ninja build directory")
    parser.add_argument("--dxc", type=Path, default=dxc_redist.dll_dir() / "dxc.exe",
                        help="compiler to test (default: the pinned release dxc.exe)")
    args = parser.parse_args()
    if not args.dxc.is_file():
        print(f"FAIL: {args.dxc} not found; run: python scripts/bootstrap.py --only dxc", file=sys.stderr)
        return 1
    failures = 0
    count = 0
    with tempfile.TemporaryDirectory(prefix="sbk_dxc_compat_") as tmp:
        for count, command in enumerate(shader_commands(args.build), start=1):
            if "/Fo" not in command or command.index("/Fo") + 1 >= len(command):
                print(f"FAIL: unexpected shader command: {command}", file=sys.stderr)
                return 1
            out = command.index("/Fo") + 1
            source = next((a for a in command if a.lower().endswith(".hlsl")), "?")
            command[out] = str(Path(tmp) / f"{count}.out")
            result = subprocess.run([str(args.dxc), *command], capture_output=True, text=True)
            status = "ok" if result.returncode == 0 else "FAIL"
            print(f"{status}: {Path(source).name} {' '.join(a for a in command if a.startswith(('-T', '-E', '-spirv')))}")
            if result.returncode != 0:
                failures += 1
                print(result.stdout + result.stderr, file=sys.stderr)
    if count == 0:
        print("FAIL: no shader commands found in the build", file=sys.stderr)
        return 1
    print(f"{count - failures}/{count} shader commands compile with {args.dxc.name} "
          f"({dxc_redist.RELEASE_TAG} pinned release)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
