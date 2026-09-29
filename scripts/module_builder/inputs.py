"""Local module-inputs bundle: the ROM-derived sources a module build needs.

The CPU corpus is generated from the matching decomp ELF, and that toolchain
only runs on Linux/macOS (or WSL2). A bundle carries the generated corpus and
the RSP microcode recompiled from the same ROM to another machine, for example
a Windows PC, where the module is compiled natively. It is ROM-derived: it
stays on the user's machines and is never committed, uploaded or packaged.

Layout (a .zip, or an extracted directory with the same layout):

    INPUTS.json          format, ROM SHA-1, SHA-256 of every file below
    corpus/funcs_*.c     generated CPU translation units (+ funcs.h,
    corpus/lookup.cpp    lookup.cpp, recomp_overlays.inl)
    rsp/aspMain.cpp      RSP audio microcode recompiled from the ROM
"""

from dataclasses import dataclass
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import subprocess
from typing import Dict, Optional
import zipfile

from .errors import GeneratorError, RomValidationError, StoragePermissionError

FORMAT = "sbk-module-inputs"
FORMAT_VERSION = 1
MANIFEST_NAME = "INPUTS.json"
DEFAULT_BUNDLE_NAME = "sbk-module-inputs.zip"
NOTICE = ("ROM-derived. For local use with your own ROM only: never commit, "
          "upload, share or package this file.")
CPU_UNIT_COUNT = 40
_MEMBER = re.compile(r"^(corpus|rsp)/[A-Za-z0-9_.-]+$")
_CORPUS_SUFFIXES = (".c", ".h", ".cpp", ".inl")
# Fixed zip timestamps keep bundles from the same inputs byte-identical.
_ZIP_TIME = (1980, 1, 1, 0, 0, 0)


@dataclass
class ModuleInputs:
    corpus_dir: Path
    rsp_cpp: Path
    rom_sha1: str


def _sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def collect_bundle_files(corpus_dir: Path, rsp_cpp: Path) -> Dict[str, Path]:
    """Bundle member name -> local source file."""
    files = {f"corpus/{p.name}": p for p in sorted(corpus_dir.iterdir())
             if p.is_file() and p.suffix in _CORPUS_SUFFIXES}
    units = [n for n in files if re.fullmatch(r"corpus/funcs_\d+\.c", n)]
    if len(units) != CPU_UNIT_COUNT or "corpus/lookup.cpp" not in files:
        raise GeneratorError(
            f"{corpus_dir} is not a complete corpus: expected {CPU_UNIT_COUNT} funcs_*.c "
            f"and lookup.cpp, found {len(units)} units")
    files["rsp/aspMain.cpp"] = rsp_cpp
    return files


def write_bundle(out_zip: Path, files: Dict[str, Path], rom_sha1: str,
                 source_commit: Optional[str] = None) -> Dict:
    manifest = {
        "format": FORMAT,
        "format_version": FORMAT_VERSION,
        "notice": NOTICE,
        "game_id": "snowboardkids.n64.us",
        "rom_sha1": rom_sha1.lower(),
        "source_commit": source_commit,
        "files": {name: _sha256(path.read_bytes()) for name, path in sorted(files.items())},
    }
    out_zip.parent.mkdir(parents=True, exist_ok=True)
    staging = out_zip.with_name(out_zip.name + ".partial")
    with zipfile.ZipFile(staging, "w", zipfile.ZIP_DEFLATED) as archive:
        def add(name: str, data: bytes):
            info = zipfile.ZipInfo(name, date_time=_ZIP_TIME)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            archive.writestr(info, data)
        add(MANIFEST_NAME, (json.dumps(manifest, indent=2) + "\n").encode("utf-8"))
        for name, path in sorted(files.items()):
            add(name, path.read_bytes())
    staging.replace(out_zip)
    return manifest


def _read_member(source: Path, archive: Optional[zipfile.ZipFile], name: str) -> bytes:
    if archive is not None:
        try:
            return archive.read(name)
        except KeyError:
            raise GeneratorError(f"Module inputs bundle is missing {name}")
    path = source.joinpath(*PurePosixPath(name).parts)
    if not path.is_file():
        raise GeneratorError(f"Module inputs directory is missing {name}")
    return path.read_bytes()


def load_bundle(source: Path, rom_sha1: str, work_dir: Path) -> ModuleInputs:
    """Verify a bundle (zip or directory) against the validated ROM and
    materialize it under work_dir. Nothing outside the manifest is extracted."""
    source = Path(source)
    if not source.exists():
        raise GeneratorError(f"Module inputs not found: {source}")
    archive = None
    try:
        if source.is_file():
            try:
                archive = zipfile.ZipFile(source)
            except zipfile.BadZipFile:
                raise GeneratorError(f"{source} is not a module inputs .zip")
        try:
            manifest = json.loads(_read_member(source, archive, MANIFEST_NAME).decode("utf-8"))
        except (ValueError, UnicodeDecodeError):
            raise GeneratorError(f"{MANIFEST_NAME} in {source} is not valid JSON")
        if manifest.get("format") != FORMAT or manifest.get("format_version") != FORMAT_VERSION:
            raise GeneratorError(
                f"{source} is not a version-{FORMAT_VERSION} {FORMAT} bundle; "
                "re-export it with scripts/export-module-inputs.py from this checkout")
        if str(manifest.get("rom_sha1", "")).lower() != rom_sha1.lower():
            raise RomValidationError(
                "The module inputs were generated from a different ROM image "
                f"(bundle {manifest.get('rom_sha1')}, this ROM {rom_sha1}).")
        files = manifest.get("files")
        if not isinstance(files, dict) or "rsp/aspMain.cpp" not in files:
            raise GeneratorError(f"{MANIFEST_NAME} in {source} lists no RSP microcode")

        work_dir.mkdir(parents=True, exist_ok=True)
        for name, digest in sorted(files.items()):
            if not _MEMBER.fullmatch(name) or ".." in name:
                raise GeneratorError(f"Refusing unexpected module inputs member: {name}")
            data = _read_member(source, archive, name)
            if _sha256(data) != digest:
                raise GeneratorError(
                    f"{name} does not match its checksum; the bundle is damaged or was edited. "
                    "Copy it again in binary mode (no line-ending conversion).")
            target = work_dir.joinpath(*PurePosixPath(name).parts)
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
    finally:
        if archive is not None:
            archive.close()

    corpus_dir = work_dir / "corpus"
    collect_bundle_files(corpus_dir, work_dir / "rsp" / "aspMain.cpp")  # completeness check
    return ModuleInputs(corpus_dir=corpus_dir, rsp_cpp=work_dir / "rsp" / "aspMain.cpp",
                        rom_sha1=rom_sha1.lower())


def ensure_not_tracked_location(root: Path, out_path: Path) -> None:
    """Refuse to write ROM-derived output where git could pick it up."""
    try:
        out_path.resolve().relative_to(root.resolve())
    except ValueError:
        return  # outside the checkout
    try:
        ignored = subprocess.run(["git", "check-ignore", "-q", str(out_path.resolve())],
                                 cwd=root, capture_output=True).returncode == 0
    except OSError:
        ignored = False  # cannot ask git: assume the worst
    if not ignored:
        raise StoragePermissionError(
            f"Refusing to write ROM-derived output to {out_path}: it is inside the repository "
            "and not git-ignored. Choose a path under build-tools/ or outside the checkout.")
