"""Compilation of game module translation units into a shared library."""

import concurrent.futures
import hashlib
import os
from pathlib import Path
import subprocess
import time
from typing import Callable, Dict, List, Optional, Sequence

from .errors import CompileError, LinkError
from .toolchain import Toolchain


def compile_sources_to_shared_library(
    toolchain: Toolchain,
    sources: List[Path],
    include_dirs: List[Path],
    out_library: Path,
    workspace_dir: Path,
    jobs: int = 4,
    progress_callback: Optional[Callable[[int, int, str], None]] = None,
    source_root: Optional[Path] = None,
    source_defines: Optional[Dict[Path, Sequence[str]]] = None,
    link_libraries: Sequence[Path] = ()
) -> float:
    """
    Compiles all sources into object files in parallel, then links them into out_library.
    Returns:
        elapsed_seconds
    Raises:
        CompileError if any translation unit fails to compile.
        LinkError if linking fails.
    """
    workspace_dir.mkdir(parents=True, exist_ok=True)
    out_library.parent.mkdir(parents=True, exist_ok=True)

    t0 = time.time()
    total_sources = len(sources)
    completed_count = 0
    obj_files: List[Path] = []

    def compile_one(src: Path) -> Path:
        obj_name = f"{src.stem}_{hashlib.md5(str(src).encode()).hexdigest()[:6]}.o"
        if toolchain.is_msvc_like():
            obj_name = f"{src.stem}_{hashlib.md5(str(src).encode()).hexdigest()[:6]}.obj"
        obj_path = workspace_dir / obj_name

        cmd = toolchain.get_compile_command(src, obj_path, include_dirs,
                                            (source_defines or {}).get(src, ()))

        # Keep developer home/temp paths out of the distributable module. The
        # generated corpus and RSP sources are compiled from absolute paths,
        # and __FILE__/compiler diagnostics can otherwise become embedded in
        # the shared object. Apply deterministic prefix maps without changing
        # source lookup itself.
        prefix_maps = []
        if source_root is not None:
            prefix_maps.append((source_root.resolve(), Path(".")))
        prefix_maps.append((workspace_dir.parent.resolve(), Path(".module-build")))
        for old, new in prefix_maps:
            cmd[1:1] = toolchain.get_prefix_map_args(old, new)

        res = subprocess.run(cmd, capture_output=True, text=True)
        if res.returncode != 0:
            raise CompileError(
                f"Failed compiling {src.name} (exit code {res.returncode}):\n{res.stderr}\n{res.stdout}"
            )
        return obj_path

    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as executor:
        future_to_src = {executor.submit(compile_one, src): src for src in sources}
        for future in concurrent.futures.as_completed(future_to_src):
            src = future_to_src[future]
            try:
                obj = future.result()
                obj_files.append(obj)
                completed_count += 1
                if progress_callback:
                    progress_callback(completed_count, total_sources, src.name)
            except Exception as e:
                # Cancel remaining tasks
                executor.shutdown(wait=False, cancel_futures=True)
                if isinstance(e, CompileError):
                    raise
                raise CompileError(f"Error compiling {src.name}: {e}")

    # Linking step
    # Sorted objects keep the link order, and so the module bytes, independent
    # of which compile job finished first.
    link_cmd = toolchain.get_link_command(sorted(obj_files), out_library, link_libraries)
    link_res = subprocess.run(link_cmd, capture_output=True, text=True)
    if link_res.returncode != 0:
        raise LinkError(
            f"Linker failed with exit code {link_res.returncode}:\n{link_res.stderr}\n{link_res.stdout}"
        )

    if not out_library.is_file() or out_library.stat().st_size == 0:
        raise LinkError(f"Linking reported success but output file {out_library} was not created.")

    return time.time() - t0
