"""Compiler toolchain discovery and invocation command generation for Linux and Windows."""

import os
from pathlib import Path
import shutil
import subprocess
import sys
from typing import List, Optional, Tuple

from .errors import MissingCompilerError


class ToolchainFlavor:
    GCC = "gcc"
    CLANG = "clang"
    MSVC = "msvc"
    CLANG_CL = "clang-cl"


class Toolchain:
    def __init__(self, executable: Path, flavor: str, version_string: str = ""):
        self.executable = executable
        self.flavor = flavor
        self.version_string = version_string

    def __repr__(self) -> str:
        return f"Toolchain(flavor={self.flavor}, path={self.executable}, version={self.version_string})"

    def is_msvc_like(self) -> bool:
        return self.flavor in (ToolchainFlavor.MSVC, ToolchainFlavor.CLANG_CL)

    def get_compile_command(self, src: Path, obj: Path, include_dirs: List[Path]) -> List[str]:
        """Generate command to compile one source file into an object file."""
        if self.is_msvc_like():
            cmd = [
                str(self.executable),
                "/nologo",
                "/std:c++20",
                "/O2",
                "/W3",
                "/EHsc",
                "/MD",
                "/c",
                str(src),
                f"/Fo:{obj}"
            ]
            for inc in include_dirs:
                cmd.append(f"/I{inc}")
            return cmd
        else:
            cmd = [
                str(self.executable),
                "-std=c++20",
                "-O2",
                "-fPIC",
                "-fno-strict-aliasing",
                "-mssse3",
                "-msse4.1",
                "-Wno-unused-variable",
                "-Wno-unused-but-set-variable",
                "-c",
                str(src),
                "-o",
                str(obj)
            ]
            for inc in include_dirs:
                cmd.append(f"-I{inc}")
            return cmd

    def get_link_command(self, obj_files: List[Path], out_dll: Path) -> List[str]:
        """Generate command to link object files into a shared library (.so or .dll)."""
        if self.is_msvc_like():
            cmd = [
                str(self.executable),
                "/nologo",
                "/LD",
                f"/Fe:{out_dll}"
            ]
            cmd.extend(str(obj) for obj in obj_files)
            return cmd
        else:
            cmd = [
                str(self.executable),
                "-shared",
                "-o",
                str(out_dll)
            ]
            cmd.extend(str(obj) for obj in obj_files)
            return cmd


def inspect_compiler_flavor(path: Path) -> Tuple[str, str]:
    """Inspect binary name and output of --version / /? to determine flavor and version."""
    stem = path.stem.lower()
    if "clang-cl" in stem:
        flavor = ToolchainFlavor.CLANG_CL
    elif stem == "cl":
        flavor = ToolchainFlavor.MSVC
    elif "clang" in stem:
        flavor = ToolchainFlavor.CLANG
    else:
        flavor = ToolchainFlavor.GCC

    version_str = ""
    try:
        flag = "/?" if flavor == ToolchainFlavor.MSVC else "--version"
        res = subprocess.run([str(path), flag], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=5)
        out = (res.stdout or res.stderr).splitlines()
        if out:
            version_str = out[0].strip()
            if "clang" in version_str.lower() and flavor == ToolchainFlavor.GCC:
                flavor = ToolchainFlavor.CLANG
    except Exception:
        pass

    return flavor, version_str


def discover_toolchain(explicit_cxx: Optional[str] = None) -> Toolchain:
    """
    Locates an installed, supported C++20 compiler.
    Checks:
      1. explicit_cxx (if provided)
      2. Environment variable CXX
      3. OS-specific candidate search:
         Linux/POSIX: clang++, g++, c++
         Windows: clang-cl.exe, cl.exe, g++.exe
    """
    candidates = []
    if explicit_cxx and explicit_cxx.strip():
        candidates.append(explicit_cxx.strip())

    env_cxx = os.environ.get("CXX")
    if env_cxx and env_cxx.strip():
        candidates.append(env_cxx.strip())

    if sys.platform == "win32":
        candidates.extend(["clang-cl.exe", "clang-cl", "cl.exe", "cl", "g++.exe", "g++"])
    else:
        candidates.extend(["clang++", "g++", "c++"])

    for candidate in candidates:
        found = shutil.which(candidate)
        if found:
            exe_path = Path(found)
            flavor, version = inspect_compiler_flavor(exe_path)
            return Toolchain(executable=exe_path, flavor=flavor, version_string=version)

    # If no compiler was found, construct a helpful, diagnostic error message
    if sys.platform == "win32":
        err_msg = (
            "No C++ compiler was discovered on your Windows system.\n"
            "To build the game module locally, please install:\n"
            "  1. Visual Studio Build Tools (with C++ Desktop workload), or\n"
            "  2. LLVM / Clang (clang-cl), or\n"
            "  3. MinGW-w64 (g++).\n"
            "Ensure the compiler binary is added to your PATH or set CXX."
        )
    else:
        err_msg = (
            "No C++ compiler was discovered on your system.\n"
            "To build the game module locally, please install clang++ or g++:\n"
            "  Ubuntu / Debian: sudo apt install g++   (or clang)\n"
            "  Fedora / RHEL:   sudo dnf install gcc-c++ (or clang)\n"
            "  Arch Linux:      sudo pacman -S gcc     (or clang)\n"
            "Or set the CXX environment variable to your compiler path."
        )

    raise MissingCompilerError(err_msg)
