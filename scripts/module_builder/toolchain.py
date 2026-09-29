"""Compiler toolchain discovery and invocation command generation for Linux and Windows."""

import os
from pathlib import Path
import shutil
import subprocess
import sys
from typing import List, Optional, Sequence, Tuple

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

    def get_compile_command(self, src: Path, obj: Path, include_dirs: List[Path],
                            defines: Sequence[str] = ()) -> List[str]:
        """Generate command to compile one source file into an object file.

        Generated CPU units are .c files holding C++ continuation code; every
        flavor compiles them as C++ (g++/clang++ do so by driver name).
        """
        if self.is_msvc_like():
            cmd = [
                str(self.executable),
                "/nologo",
                "/TP",
                "/std:c++20",
                "/O2",
                "/W3",
                "/EHsc",
                "/MD",
            ]
            if self.flavor == ToolchainFlavor.CLANG_CL:
                # Same RSP vector ISA as the GCC/Clang build; MSVC needs no flag.
                cmd.extend(["/clang:-mssse3", "/clang:-msse4.1", "-Wno-unused-variable"])
            cmd.extend(["/c", str(src), f"/Fo:{obj}"])
            for inc in include_dirs:
                cmd.append(f"/I{inc}")
            for define in defines:
                cmd.append(f"/D{define}")
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
            for define in defines:
                cmd.append(f"-D{define}")
            return cmd

    def get_prefix_map_args(self, old: Path, new: Path) -> List[str]:
        """Arguments that keep `old` out of embedded __FILE__ strings."""
        if self.flavor == ToolchainFlavor.MSVC:
            return [f"/pathmap:{old}={new}"]
        if self.flavor == ToolchainFlavor.CLANG_CL:
            # clang-cl has no /pathmap and would read it as an input file.
            return [f"/clang:-ffile-prefix-map={old}={new}"]
        return [f"-ffile-prefix-map={old}={new}"]

    def get_link_command(self, obj_files: List[Path], out_dll: Path,
                         libraries: Sequence[Path] = ()) -> List[str]:
        """Generate command to link object files into a shared library (.so or .dll)."""
        if self.is_msvc_like():
            cmd = [
                str(self.executable),
                "/nologo",
                "/LD",
                "/MD",
                f"/Fe:{out_dll}"
            ]
            cmd.extend(str(obj) for obj in obj_files)
            cmd.extend(str(lib) for lib in libraries)
            return cmd
        else:
            cmd = [
                str(self.executable),
                "-shared",
                "-o",
                str(out_dll)
            ]
            cmd.extend(str(obj) for obj in obj_files)
            cmd.extend(str(lib) for lib in libraries)
            return cmd

    def find_librarian(self) -> Path:
        """lib.exe or llvm-lib, used to turn a .def export list into an import library."""
        candidates = []
        if self.flavor == ToolchainFlavor.CLANG_CL:
            candidates.extend([self.executable.parent / "llvm-lib.exe", self.executable.parent / "llvm-lib"])
        else:
            candidates.append(self.executable.parent / "lib.exe")
        for candidate in candidates:
            if candidate.is_file():
                return candidate
        for name in ("lib.exe", "lib", "llvm-lib.exe", "llvm-lib"):
            found = shutil.which(name)
            if found:
                return Path(found)
        raise MissingCompilerError(
            "No import librarian (lib.exe or llvm-lib) was found next to the compiler or on PATH.\n"
            "Run the builder from a Visual Studio Developer Command Prompt."
        )

    def get_import_library_command(self, def_file: Path, out_lib: Path,
                                   librarian: Optional[Path] = None) -> List[str]:
        """Command creating an x64 import library from a module-definition file."""
        tool = librarian or self.find_librarian()
        return [str(tool), "/nologo", "/machine:x64", f"/def:{def_file}", f"/out:{out_lib}"]


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
        # The module shares the engine's MSVC ABI and C runtime, so MinGW g++
        # is not a candidate on Windows.
        candidates.extend(["clang-cl.exe", "clang-cl", "cl.exe", "cl"])
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
            "  1. LLVM / Clang (clang-cl, recommended), or\n"
            "  2. Visual Studio Build Tools (with C++ Desktop workload).\n"
            "Run from a Developer Command Prompt so lib.exe and the Windows SDK are available,\n"
            "or set CXX to the compiler path."
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
