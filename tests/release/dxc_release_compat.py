#!/usr/bin/env python3
"""Check which DXC compiles RT64's and RecompFrontend's shaders in a build.

Windows builds must compile every shader with the pinned official DXC release
(scripts/dxc_redist.py, passed to RT64 by rt64-dxc-executable.patch): RT64
links run-time shaders against build-time library shaders, and IDxcLinker only
accepts libraries from the same compiler. This lists the shader commands of a
configured Ninja build and fails if any uses another compiler (e.g. RT64's
contrib dxc.exe).

    python tests/release/dxc_release_compat.py --build build-engine --expect-pinned

With --replay DXC, every command is re-run with that compiler instead
(arguments unchanged, outputs to a temporary directory): used to evaluate a
candidate release against the renderer's shaders before pinning it.
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

# The compiler path, quoted when it contains spaces.
DXC_TOOL = re.compile(r'"([^"]*[/\\]dxc(?:\.exe|-linux|-macos))"|([^\s"]*[/\\]dxc(?:\.exe|-linux|-macos))',
                      re.IGNORECASE)


def shader_commands(build: Path):
    """(compiler path, arguments) for every DXC invocation of the build."""
    listing = subprocess.run(["ninja", "-C", str(build), "-t", "commands"], capture_output=True,
                             text=True, check=True).stdout
    for line in listing.splitlines():
        match = DXC_TOOL.search(line)
        if not match:
            continue
        tool = match.group(1) or match.group(2)
        tail = line[match.end():].strip()
        if tail.endswith('"') and tail.count('"') % 2:
            tail = tail[:-1]  # closing quote of a Windows "cmd /C" wrapper
        if sys.platform == "win32":
            args = [a[1:-1] if len(a) > 1 and a[0] == a[-1] == '"' else a
                    for a in shlex.split(tail, posix=False)]
        else:
            args = shlex.split(tail)
        yield tool, args


def same_file(a: str, b: Path) -> bool:
    try:
        return Path(a).resolve() == b.resolve()
    except OSError:
        return False


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build", type=Path, required=True, help="configured Ninja build directory")
    parser.add_argument("--expect-pinned", action="store_true",
                        help=f"fail unless every shader command uses the pinned {dxc_redist.RELEASE_TAG} dxc.exe")
    parser.add_argument("--replay", type=Path, default=None, help="re-run every command with this compiler")
    args = parser.parse_args()
    commands = list(shader_commands(args.build))
    if not commands:
        print("FAIL: no shader commands found in the build", file=sys.stderr)
        return 1
    failures = 0
    if args.expect_pinned:
        pinned = dxc_redist.dxc_exe()
        others = sorted({tool for tool, _ in commands if not same_file(tool, pinned)})
        for tool in others:
            print(f"FAIL: shader command uses {tool}, not the pinned {pinned}", file=sys.stderr)
        failures += len(others)
        if not others:
            print(f"{len(commands)} shader commands, all compiled by the pinned "
                  f"{dxc_redist.RELEASE_TAG} dxc.exe")
    if args.replay is not None:
        with tempfile.TemporaryDirectory(prefix="sbk_dxc_compat_") as tmp:
            for index, (_, command) in enumerate(commands, start=1):
                if "/Fo" not in command or command.index("/Fo") + 1 >= len(command):
                    print(f"FAIL: unexpected shader command: {command}", file=sys.stderr)
                    return 1
                command[command.index("/Fo") + 1] = str(Path(tmp) / f"{index}.out")
                source = next((a for a in command if a.lower().endswith(".hlsl")), "?")
                result = subprocess.run([str(args.replay), *command], capture_output=True, text=True)
                print(f"{'ok' if result.returncode == 0 else 'FAIL'}: {Path(source).name} "
                      f"{' '.join(a for a in command if a.startswith(('-T', '-E', '-spirv')))}")
                if result.returncode != 0:
                    failures += 1
                    print(result.stdout + result.stderr, file=sys.stderr)
        print(f"{len(commands) - failures}/{len(commands)} shader commands compile with {args.replay}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
