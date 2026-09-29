#!/usr/bin/env python3
"""Windows runtime DLL policy: which imported DLLs are OS-provided, which may be
bundled (and under which license evidence), and which are unknown.

The bundled set is derived from the real PE import tables of the engine and the
game module (scripts/pe_imports.py), never from a hand-written guess. An import
that is neither a known system DLL nor a reviewed redistributable fails staging.
"""
from dataclasses import dataclass
import hashlib
import os
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Tuple

import dxc_redist
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
#   "text"         every text in `licenses` ships as licenses/<notice>.txt
#   "ms-terms"     "text", plus Microsoft distributable-code terms whose
#                  distribution requirements need a recorded maintainer
#                  decision (dxc_redist.DXIL_DECISION) before a public package
#   "msvc-redist"  Microsoft Visual C++ Redistributable "Distributable Code";
#                  app-local deployment per the Visual Studio license terms
#   "pending"      no reviewed license text: blocks public packages
@dataclass(frozen=True)
class Redistributable:
    name: str
    source: str
    notice: str
    evidence: str
    licenses: Tuple[Tuple[str, str], ...] = ()  # (licenses/<notice>.txt, repo path)
    companions: Tuple[str, ...] = ()
    provenance: str = ""
    version: str = ""
    sha256: Optional[str] = None  # when set, the bundled bytes must match


_SDL2_DIR = ".deps-renderer/rt64/src/contrib/mupen64plus-win32-deps/SDL2-2.26.3"
_MSVC_SOURCE = "Microsoft Visual C++ Redistributable (VCToolsRedistDir, app-local)"
_DXC_DIR = (dxc_redist.INSTALL_DIR / "bin" / "x64").as_posix()


def _dxc(name: str, evidence: str, companions: Tuple[str, ...] = ()) -> Redistributable:
    entry = dxc_redist.DLLS[name]
    licenses = tuple((notice, (dxc_redist.LICENSE_DIR / text).as_posix())
                     for notice, text in entry["notices"].items())
    return Redistributable(name, _DXC_DIR, next(iter(entry["notices"])), evidence, licenses, companions,
                           dxc_redist.PROVENANCE, entry["version"], dxc_redist.FILES[entry["member"]])

REDISTRIBUTABLES: Dict[str, Redistributable] = {r.name.lower(): r for r in (
    Redistributable("SDL2.dll", f"{_SDL2_DIR}/lib/x64", "SDL2", "text",
                    (("SDL2", f"{_SDL2_DIR}/COPYING.txt"),),
                    provenance="SDL2 2.26.3 from RT64's pinned mupen64plus-win32-deps submodule (SDL2-2.26.3/lib/x64)",
                    version="2.26.3"),
    _dxc("dxcompiler.dll", "text", companions=("dxil.dll",)),
    _dxc("dxil.dll", "ms-terms"),
    *(Redistributable(name, _MSVC_SOURCE, "MSVC-Runtime", "msvc-redist",
                      provenance="Microsoft Visual C++ Redistributable (build machine VCToolsRedistDir)")
      for name in ("vcruntime140.dll", "vcruntime140_1.dll", "msvcp140.dll", "msvcp140_1.dll",
                   "msvcp140_2.dll", "msvcp140_atomic_wait.dll", "concrt140.dll")),
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

    def pending_licenses(self, root: Path = ROOT) -> List[str]:
        """Bundled DLLs whose redistribution is not cleared yet."""
        pending = set()
        for name in self.bundled:
            entry = REDISTRIBUTABLES[name.lower()]
            if entry.evidence == "pending":
                pending.add(f"{entry.name} (no reviewed license text)")
            elif entry.evidence == "ms-terms" and not dxc_redist.dxil_redistribution_accepted(root):
                pending.add(f"{entry.name} (Microsoft distributable-code terms not accepted in "
                            f"{dxc_redist.DXIL_DECISION.as_posix()})")
        return sorted(pending)

    def license_files(self, root: Path = ROOT) -> Dict[str, Path]:
        files = {}
        for name in self.bundled:
            entry = REDISTRIBUTABLES[name.lower()]
            if entry.evidence in ("text", "ms-terms"):
                for notice, path in entry.licenses:
                    files[notice] = root / path
        return files

    def missing_license_texts(self, root: Path = ROOT) -> List[str]:
        """Bundled DLLs whose license text is not present in this checkout."""
        missing = []
        for name in self.bundled:
            for notice, path in REDISTRIBUTABLES[name.lower()].licenses:
                if not (root / path).is_file():
                    missing.append(f"{name} (license text {path} not bootstrapped)")
        return sorted(missing)

    def pinned_hash_errors(self) -> List[str]:
        """Bundled DLLs with a pinned SHA-256 must be exactly those bytes."""
        errors = []
        for name, path in self.bundled.items():
            entry = REDISTRIBUTABLES[name.lower()]
            if entry.sha256 and sha256_file(path) != entry.sha256:
                errors.append(f"{name} at {path} is not the pinned {entry.provenance} binary "
                              f"(SHA-256 {entry.sha256}); run: python scripts/bootstrap.py --only dxc "
                              "and rebuild the engine")
        return errors


def sha256_file(path: Path) -> str:
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


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


def pe_file_version(path: Path) -> str:
    """FileVersion string from a PE version resource, or "" if absent."""
    text = Path(path).read_bytes().decode("utf-16-le", "ignore")
    marker = text.find("FileVersion\0")
    if marker < 0:
        return ""
    value = text[marker + len("FileVersion\0"):].lstrip("\0")
    return value.split("\0", 1)[0].strip()


MANIFEST_HEADER = "Snowboard Kids Recompiled - Windows runtime DLLs\nFormat: 1\n"


def runtime_manifest(resolution: RuntimeResolution) -> str:
    """Deterministic RUNTIME-DLLS.txt: no paths, only names, hashes and provenance."""
    lines = [MANIFEST_HEADER]
    for name, path in sorted(resolution.bundled.items(), key=lambda item: item[0].lower()):
        entry = REDISTRIBUTABLES[name.lower()]
        notices = [f"licenses/{notice}.txt" for notice, _ in entry.licenses]
        if entry.evidence == "msvc-redist":
            notices = ["Visual Studio license terms (Microsoft Visual C++ Redistributable)"]
        lines += [
            f"[bundled] {name}",
            f"sha256: {sha256_file(path)}",
            f"version: {entry.version or pe_file_version(path) or 'unknown'}",
            f"provenance: {entry.provenance or entry.source}",
            f"evidence: {entry.evidence}",
            f"licenses: {', '.join(notices) if notices else 'none'}",
            "",
        ]
    for name in resolution.system:
        lines += [f"[system] {name}", "provenance: Windows system DLL, never bundled", ""]
    return "\n".join(lines)
