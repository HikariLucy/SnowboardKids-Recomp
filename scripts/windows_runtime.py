#!/usr/bin/env python3
"""Windows runtime DLL policy: which imported DLLs are OS-provided, which may be
bundled (and under which license evidence), and which are unknown.

The bundled set is derived from the real PE import tables of the engine and the
game module (scripts/pe_imports.py), never from a hand-written guess. An import
that is neither a known system DLL nor a reviewed redistributable fails staging.
"""
from dataclasses import dataclass
import os
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Tuple

from pe_imports import read_imports_file

ROOT = Path(__file__).resolve().parents[1]
ENGINE_EXE = "snowboardkidsengine.exe"

# Provided by every supported Windows 10/11 x64 installation.
SYSTEM_DLLS = frozenset({
    "advapi32.dll", "bcrypt.dll", "cfgmgr32.dll", "comctl32.dll", "comdlg32.dll",
    "crypt32.dll", "d3d11.dll", "d3d12.dll", "d3dcompiler_47.dll", "dbghelp.dll",
    "dinput8.dll", "dwmapi.dll", "dxgi.dll", "gdi32.dll", "hid.dll", "imm32.dll",
    "kernel32.dll", "kernelbase.dll", "msvcrt.dll", "ntdll.dll", "ole32.dll", "oleaut32.dll",
    "powrprof.dll", "propsys.dll", "rpcrt4.dll", "sechost.dll", "setupapi.dll",
    "shell32.dll", "shlwapi.dll", "ucrtbase.dll", "user32.dll", "userenv.dll",
    "uxtheme.dll", "version.dll", "winmm.dll", "ws2_32.dll", "xinput1_4.dll",
})
SYSTEM_PREFIXES = ("api-ms-win-", "ext-ms-win-")

# License evidence kinds for a bundled DLL:
#   "text"         the license text at `license` ships in licenses/<notice>.txt
#   "msvc-redist"  Microsoft Visual C++ Redistributable "Distributable Code";
#                  app-local deployment per the Visual Studio license terms
#   "pending"      no reviewed license text in the pinned tree: blocks public
#                  packages until the text is added and reviewed
@dataclass(frozen=True)
class Redistributable:
    name: str
    source: str
    notice: str
    evidence: str
    license: Optional[str] = None
    companions: Tuple[str, ...] = ()


_SDL2_DIR = ".deps-renderer/rt64/src/contrib/mupen64plus-win32-deps/SDL2-2.26.3"
_MSVC_SOURCE = "Microsoft Visual C++ Redistributable (VCToolsRedistDir, app-local)"

REDISTRIBUTABLES: Dict[str, Redistributable] = {r.name.lower(): r for r in (
    Redistributable("SDL2.dll", f"{_SDL2_DIR}/lib/x64", "SDL2", "text", f"{_SDL2_DIR}/COPYING.txt"),
    Redistributable("dxcompiler.dll", ".deps-renderer/rt64/src/contrib/dxc/bin/x64",
                    "DirectXShaderCompiler", "pending", companions=("dxil.dll",)),
    Redistributable("dxil.dll", ".deps-renderer/rt64/src/contrib/dxc/bin/x64",
                    "DirectXShaderCompiler-dxil", "pending"),
    Redistributable("vcruntime140.dll", _MSVC_SOURCE, "MSVC-Runtime", "msvc-redist"),
    Redistributable("vcruntime140_1.dll", _MSVC_SOURCE, "MSVC-Runtime", "msvc-redist"),
    Redistributable("msvcp140.dll", _MSVC_SOURCE, "MSVC-Runtime", "msvc-redist"),
    Redistributable("msvcp140_1.dll", _MSVC_SOURCE, "MSVC-Runtime", "msvc-redist"),
    Redistributable("msvcp140_2.dll", _MSVC_SOURCE, "MSVC-Runtime", "msvc-redist"),
    Redistributable("msvcp140_atomic_wait.dll", _MSVC_SOURCE, "MSVC-Runtime", "msvc-redist"),
    Redistributable("concrt140.dll", _MSVC_SOURCE, "MSVC-Runtime", "msvc-redist"),
)}


def is_system_dll(name: str) -> bool:
    lower = name.lower()
    return lower in SYSTEM_DLLS or lower.startswith(SYSTEM_PREFIXES)


def msvc_redist_dirs() -> List[Path]:
    """The x64 CRT directory of the active Visual Studio toolset, if any."""
    base = os.environ.get("VCToolsRedistDir")
    if not base:
        return []
    return sorted((Path(base) / "x64").glob("Microsoft.VC*.CRT"), reverse=True)


@dataclass
class RuntimeResolution:
    bundled: Dict[str, Path]
    system: List[str]
    errors: List[str]

    def pending_licenses(self) -> List[str]:
        return sorted({REDISTRIBUTABLES[n.lower()].name for n in self.bundled
                       if REDISTRIBUTABLES[n.lower()].evidence == "pending"})

    def license_files(self, root: Path = ROOT) -> Dict[str, Path]:
        files = {}
        for name in self.bundled:
            entry = REDISTRIBUTABLES[name.lower()]
            if entry.evidence == "text" and entry.license:
                files[entry.notice] = root / entry.license
        return files


def resolve_runtime(images: Iterable[Path], search_dirs: Iterable[Path]) -> RuntimeResolution:
    """Follow imports from `images` through bundled DLLs found in `search_dirs`."""
    search = [Path(d) for d in search_dirs]
    bundled: Dict[str, Path] = {}
    system = set()
    errors: List[str] = []
    pending = [Path(p) for p in images]
    seen = set()

    def locate(name: str) -> Optional[Path]:
        for directory in search:
            candidate = directory / name
            if candidate.is_file():
                return candidate
        return None

    def require(name: str, needed_by: str):
        lower = name.lower()
        if lower in bundled or lower in seen:
            return
        seen.add(lower)
        entry = REDISTRIBUTABLES.get(lower)
        if entry is None:
            errors.append(f"{needed_by} imports {name}, which is neither a Windows system DLL "
                          "nor a reviewed redistributable (scripts/windows_runtime.py)")
            return
        path = locate(entry.name)
        if path is None:
            errors.append(f"{needed_by} needs {entry.name}, not found in: "
                          + ", ".join(str(d) for d in search))
            return
        bundled[entry.name] = path
        pending.append(path)
        for companion in entry.companions:
            require(companion, entry.name)

    while pending:
        image = pending.pop(0)
        info = read_imports_file(image)
        for name in info["imports"] + info["delay_imports"]:
            if name.lower() == ENGINE_EXE:
                continue  # the game module binds back to the engine executable
            if is_system_dll(name):
                system.add(name.lower())
            else:
                require(name, image.name)

    return RuntimeResolution(dict(sorted(bundled.items())), sorted(system), errors)
