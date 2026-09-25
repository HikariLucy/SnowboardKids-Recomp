"""Orchestration service for building and atomically installing Snowboard Kids game modules."""

from dataclasses import dataclass
import os
from pathlib import Path
import shutil
import sys
import tempfile
import time
from typing import Callable, Dict, Optional, Any

from .compiler import compile_sources_to_shared_library
from .errors import (
    BuilderError,
    BuilderExitCode,
    StoragePermissionError,
)
from .generator import collect_module_sources, find_corpus_dir, generate_rsp_code
from .manifest import generate_manifest_data, write_manifest_file
from .rom import validate_rom
from .toolchain import Toolchain, discover_toolchain
from .validator import validate_module_binary


@dataclass
class BuilderConfig:
    rom_path: Path
    out_dir: Optional[Path] = None
    explicit_out_file: Optional[Path] = None
    cxx: Optional[str] = None
    jobs: int = 4
    keep_temp: bool = False
    debug: bool = False
    root_dir: Optional[Path] = None


@dataclass
class BuilderResult:
    success: bool
    module_path: Path
    manifest_path: Path
    build_time_seconds: float
    metadata: Dict[str, Any]
    error_message: Optional[str] = None
    exit_code: int = BuilderExitCode.SUCCESS


def default_module_filename() -> str:
    if sys.platform == "win32":
        return "SnowboardKidsGame.dll"
    elif sys.platform == "darwin":
        return "SnowboardKidsGame.dylib"
    return "SnowboardKidsGame.so"


def default_user_module_dir(root_dir: Path) -> Path:
    """Computes standard user module installation directory."""
    if env_dir := os.environ.get("SBK_USER_DATA_DIR"):
        return Path(env_dir) / "modules" / "snowboardkids-us"

    # Default platform user data paths
    if sys.platform == "win32":
        app_data = os.environ.get("APPDATA", str(Path.home() / "AppData" / "Roaming"))
        return Path(app_data) / "SnowboardKids" / "modules" / "snowboardkids-us"
    elif sys.platform == "darwin":
        return Path.home() / "Library" / "Application Support" / "SnowboardKids" / "modules" / "snowboardkids-us"
    else:
        xdg_data = os.environ.get("XDG_DATA_HOME", str(Path.home() / ".local" / "share"))
        return Path(xdg_data) / "SnowboardKids" / "modules" / "snowboardkids-us"


class ModuleBuilderService:
    def __init__(self, config: BuilderConfig):
        self.config = config
        self.root_dir = config.root_dir or Path(__file__).resolve().parents[2]

    def build(self, status_callback: Optional[Callable[[str, Optional[int], Optional[int]], None]] = None) -> BuilderResult:
        """
        Executes the full builder flow:
          Validate ROM -> Discover Toolchain -> Generate RSP -> Compile Module -> Validate ABI -> Atomic Install
        """
        t_start = time.time()

        def notify(phase: str, current: Optional[int] = None, total: Optional[int] = None):
            if status_callback:
                status_callback(phase, current, total)

        # 1. Validate ROM
        notify("Validating ROM", 1, 6)
        rom_bytes, format_name, rom_sha1 = validate_rom(self.config.rom_path)

        # 2. Discover Toolchain
        notify("Discovering C++ toolchain", 2, 6)
        toolchain = discover_toolchain(self.config.cxx)

        # Determine target output paths
        if self.config.explicit_out_file:
            target_module_path = self.config.explicit_out_file.resolve()
            target_dir = target_module_path.parent
        else:
            target_dir = self.config.out_dir.resolve() if self.config.out_dir else default_user_module_dir(self.root_dir)
            target_module_path = target_dir / default_module_filename()

        target_manifest_path = target_dir / "MODULE-INFO.json"

        # Check permissions for target directory
        try:
            target_dir.mkdir(parents=True, exist_ok=True)
        except OSError as e:
            raise StoragePermissionError(f"Cannot create or access target directory '{target_dir}': {e}")

        # Setup temporary workspace for building
        tmp_dir_obj = tempfile.TemporaryDirectory(prefix="sbk_module_build_")
        tmp_workspace = Path(tmp_dir_obj.name)

        try:
            # 3. Generate RSP microcode
            notify("Generating RSP audio microcode", 3, 6)
            rsp_dir = tmp_workspace / "rsp"
            rsp_cpp = generate_rsp_code(self.root_dir, rom_bytes, rsp_dir)

            # 4. Gather CPU translation units
            notify("Locating recompiled game corpus", 4, 6)
            corpus_dir = find_corpus_dir(self.root_dir)
            sources, include_dirs = collect_module_sources(self.root_dir, corpus_dir, rsp_cpp)

            # 5. Compile sources into temporary library
            notify(f"Compiling game module ({len(sources)} translation units)", 5, 6)
            temp_out_library = tmp_workspace / default_module_filename()

            def comp_progress(cur: int, tot: int, src_name: str):
                notify(f"Compiling [{cur}/{tot}] {src_name}", cur, tot)

            compile_sources_to_shared_library(
                toolchain=toolchain,
                sources=sources,
                include_dirs=include_dirs,
                out_library=temp_out_library,
                workspace_dir=tmp_workspace / "obj",
                jobs=self.config.jobs,
                progress_callback=comp_progress
            )

            # 6. Validate ABI
            notify("Validating module ABI and exported symbols", 6, 6)
            metadata = validate_module_binary(temp_out_library)

            # Generate manifest
            manifest_data = generate_manifest_data(metadata, rom_sha1)
            temp_manifest_path = tmp_workspace / "MODULE-INFO.json"
            write_manifest_file(temp_manifest_path, manifest_data)

            # 7. Atomic installation into target directory
            # Staged into target_dir with temporary names, then atomic replaced
            staged_mod = target_dir / f".tmp_{default_module_filename()}.staging"
            staged_man = target_dir / ".tmp_MODULE-INFO.json.staging"

            shutil.copy2(temp_out_library, staged_mod)
            shutil.copy2(temp_manifest_path, staged_man)

            # Atomic rename (POSIX rename() / Windows MoveFileEx with replace)
            staged_mod.replace(target_module_path)
            staged_man.replace(target_manifest_path)

            elapsed = time.time() - t_start
            return BuilderResult(
                success=True,
                module_path=target_module_path,
                manifest_path=target_manifest_path,
                build_time_seconds=elapsed,
                metadata=manifest_data,
                exit_code=BuilderExitCode.SUCCESS
            )

        finally:
            if not self.config.keep_temp:
                try:
                    tmp_dir_obj.cleanup()
                except Exception:
                    pass
