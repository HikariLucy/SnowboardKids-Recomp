"""Code generation stage: RSP microcode extraction and CPU corpus source resolution."""

import os
from pathlib import Path
import subprocess
import tempfile
from typing import List, Tuple

from .errors import GeneratorError


def find_rsp_recomp(root: Path) -> Path:
    candidates = [
        root / "build-tools" / "n64recomp" / "RSPRecomp",
        root / "build-tools" / "n64recomp" / "RSPRecomp.exe",
        root.parent / "SnowboardKids-Recomp-pfs" / "build-tools" / "n64recomp" / "RSPRecomp",
        root.parent / "SnowboardKids-Recomp-pfs" / "build-tools" / "n64recomp" / "RSPRecomp.exe",
        root / "tools" / "RSPRecomp",
        root / "tools" / "RSPRecomp.exe",
    ]
    for c in candidates:
        if c.is_file() and os.access(c, os.X_OK):
            return c.resolve()
    # Check PATH
    import shutil
    which_rsp = shutil.which("RSPRecomp")
    if which_rsp:
        return Path(which_rsp).resolve()
    raise GeneratorError(
        "RSPRecomp binary not found. Ensure tools/n64recomp/RSPRecomp is built or present."
    )


def generate_rsp_code(root: Path, rom_bytes: bytes, out_dir: Path) -> Path:
    """Extract and recompile RSP audio microcode from the validated ROM."""
    rsp_out = out_dir / "aspMain.cpp"
    if rsp_out.is_file() and rsp_out.stat().st_size > 1000:
        return rsp_out

    rsp_recomp = find_rsp_recomp(root)
    out_dir.mkdir(parents=True, exist_ok=True)

    with tempfile.NamedTemporaryFile(suffix=".z64", delete=False) as tmp_rom:
        tmp_rom.write(rom_bytes)
        tmp_rom_path = Path(tmp_rom.name)

    cfg_template = root / "aspMain.us.toml"
    if not cfg_template.is_file():
        # Fallback inline config if aspMain.us.toml not at root
        cfg_text = (
            f'rom = "{tmp_rom_path.resolve().as_posix()}"\n'
            f'output = "{rsp_out.resolve().as_posix()}"\n'
            'entrypoint = 0x1080\n'
            'data_start = 0x0000\n'
            'data_size = 0x0FE0\n'
            'text_start = 0x1080\n'
            'text_size = 0x0FC0\n'
        )
    else:
        text = cfg_template.read_text()
        text = text.replace('"../snowboardkids.z64"', f'"{tmp_rom_path.resolve().as_posix()}"')
        text = text.replace('"snowboardkids.z64"', f'"{tmp_rom_path.resolve().as_posix()}"')
        text = text.replace('"rsp/aspMain.cpp"', f'"{rsp_out.resolve().as_posix()}"')
        cfg_text = text

    tmp_cfg_path = out_dir / "rsp_build_config.toml"
    try:
        tmp_cfg_path.write_text(cfg_text)
        res = subprocess.run(
            [str(rsp_recomp), str(tmp_cfg_path)],
            cwd=root,
            capture_output=True,
            text=True
        )
        if res.returncode != 0:
            raise GeneratorError(
                f"RSPRecomp failed with exit code {res.returncode}:\n{res.stderr}\n{res.stdout}"
            )
    finally:
        if tmp_rom_path.exists():
            tmp_rom_path.unlink()
        if tmp_cfg_path.exists():
            tmp_cfg_path.unlink()

    if not rsp_out.is_file() or rsp_out.stat().st_size == 0:
        raise GeneratorError(f"Failed to generate RSP output at {rsp_out}")

    return rsp_out


def find_corpus_dir(root: Path) -> Path:
    """Finds the directory containing generated CPU translation units (funcs_*.c)."""
    candidates = [
        root / "build-tools" / "production-continuation" / "corpus",
        root.parent / "SnowboardKids-Recomp-pfs" / "build-tools" / "production-continuation" / "corpus",
        root / "RecompiledFuncs",
        root / "corpus",
    ]
    for c in candidates:
        if c.is_dir() and list(c.glob("funcs_*.c")):
            return c.resolve()
    raise GeneratorError(
        "Recompiled CPU source files not found. Ensure build-tools/production-continuation/corpus exists."
    )


def collect_module_sources(root: Path, corpus_dir: Path, rsp_cpp: Path) -> Tuple[List[Path], List[Path]]:
    """
    Collects all 43 translation units and include directories needed for compilation.
    Returns:
        (sources, include_dirs)
    """
    entry_cpp = root / "src" / "module" / "game_module_entry.cpp"
    if not entry_cpp.is_file():
        raise GeneratorError(f"Module harness entry file not found: {entry_cpp}")

    lookup_cpp = corpus_dir / "lookup.cpp"
    if not lookup_cpp.is_file():
        raise GeneratorError(f"Corpus lookup file not found: {lookup_cpp}")

    funcs = sorted(corpus_dir.glob("funcs_*.c"))
    if len(funcs) != 40:
        raise GeneratorError(
            f"Expected 40 CPU function translation units (funcs_0.c..funcs_39.c), found {len(funcs)} in {corpus_dir}"
        )

    sources = [entry_cpp, rsp_cpp, lookup_cpp] + funcs

    runtime_dir = root / ".deps-runtime" / "N64ModernRuntime"
    if not runtime_dir.is_dir():
        runtime_dir = root.parent / "SnowboardKids-Recomp-pfs" / ".deps-runtime" / "N64ModernRuntime"

    include_dirs = [
        root / "src",
        corpus_dir,
        runtime_dir / "librecomp" / "include",
        runtime_dir / "N64Recomp" / "include",
        runtime_dir / "ultramodern" / "include",
    ]

    return sources, include_dirs
