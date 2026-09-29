#!/usr/bin/env python3
"""ROM-free engine <-> game module binding test.

Builds a synthetic game module with the real module builder toolchain code and
the real module harness (src/module/game_module_entry.cpp), then asks the real
SnowboardKidsEngine to load, validate and initialize it. On Windows this proves
the PE/COFF path end to end: engine .def exports, generated import library,
LoadLibrary binding against the running executable, the RSP DMEM hand-over and
the import-thunk rebinding in init(). No ROM or ROM-derived code is involved.
"""

import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

from module_builder.compiler import compile_sources_to_shared_library  # noqa: E402
from module_builder.engine_exports import (  # noqa: E402
    ENGINE_EXECUTABLE_NAME,
    WINDOWS_RSP_DEFINES,
    import_definition_text,
    read_engine_exports,
)
from module_builder.errors import BuilderError  # noqa: E402
from module_builder.service import build_engine_import_library, default_module_filename  # noqa: E402
from module_builder.toolchain import discover_toolchain  # noqa: E402

PROBE_DIR = ROOT / "tests" / "module" / "engine_probe"
RUNTIME_DIR = ROOT / ".deps-runtime" / "N64ModernRuntime"
WINDOWS = sys.platform == "win32"
MISSING_EXPORT = "sbk_probe_missing_export"


def include_dirs():
    return [
        PROBE_DIR,  # synthetic recomp_overlays.inl
        ROOT / "src",
        RUNTIME_DIR / "librecomp" / "include",
        RUNTIME_DIR / "N64Recomp" / "include",
        RUNTIME_DIR / "ultramodern" / "include",
    ]


def build_probe(toolchain, work: Path, extra_sources=(), extra_exports=()) -> Path:
    rsp = PROBE_DIR / "probe_rsp.cpp"
    sources = [ROOT / "src" / "module" / "game_module_entry.cpp", rsp,
               PROBE_DIR / "probe_corpus.cpp", *extra_sources]
    defines = {}
    libraries = []
    if WINDOWS:
        sources.append(ROOT / "src" / "module" / "engine_imports_win32.cpp")
        defines[rsp] = WINDOWS_RSP_DEFINES
        if extra_exports:
            lib_dir = work / "engine-import"
            lib_dir.mkdir(parents=True)
            def_file = lib_dir / "SnowboardKidsEngine.def"
            def_file.write_text(import_definition_text(read_engine_exports(ROOT) + list(extra_exports)))
            out_lib = lib_dir / "SnowboardKidsEngine.lib"
            subprocess.run(toolchain.get_import_library_command(def_file, out_lib), check=True)
            libraries.append(out_lib)
        else:
            libraries.append(build_engine_import_library(toolchain, ROOT, work / "engine-import"))
    module = work / "modules" / "snowboardkids-us" / default_module_filename()
    compile_sources_to_shared_library(
        toolchain=toolchain,
        sources=sources,
        include_dirs=include_dirs(),
        out_library=module,
        workspace_dir=work / "obj",
        jobs=4,
        source_root=ROOT,
        source_defines=defines,
        link_libraries=libraries,
    )
    return module


def validate(engine: Path, module: Path):
    return subprocess.run([str(engine), "--validate-module", str(module)],
                          capture_output=True, text=True, timeout=60)


def pe_dependents(module: Path):
    dumpbin = shutil.which("dumpbin")
    if not dumpbin:
        return None
    out = subprocess.run([dumpbin, "/nologo", "/dependents", str(module)],
                         capture_output=True, text=True, check=True).stdout
    return {line.strip().lower() for line in out.splitlines() if line.strip().lower().endswith((".dll", ".exe"))}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", type=Path, required=True)
    args = parser.parse_args()
    if not args.engine.is_file():
        print(f"FAIL: engine not found: {args.engine}", file=sys.stderr)
        return 1

    toolchain = discover_toolchain()
    print(f"Probe toolchain: {toolchain}")
    if WINDOWS and not toolchain.is_msvc_like():
        print("FAIL: Windows probe needs clang-cl or MSVC", file=sys.stderr)
        return 1

    failures = []
    with tempfile.TemporaryDirectory(prefix="sbk_engine_probe_") as directory:
        work = Path(directory)
        try:
            module = build_probe(toolchain, work / "good")
        except BuilderError as error:
            print(f"FAIL: probe module build: {error.message}", file=sys.stderr)
            return 1
        result = validate(args.engine, module)
        print(result.stdout, end="")
        print(result.stderr, end="", file=sys.stderr)
        if result.returncode != 0 or "MODULE_VALID:" not in result.stdout:
            failures.append(f"engine rejected the probe module (exit {result.returncode})")

        if WINDOWS:
            dependents = pe_dependents(module)
            if dependents is None:
                print("NOTE: dumpbin unavailable; skipped PE import listing")
            else:
                print("Probe module imports: " + ", ".join(sorted(dependents)))
                if ENGINE_EXECUTABLE_NAME.lower() not in dependents:
                    failures.append(f"probe module does not import from {ENGINE_EXECUTABLE_NAME}")

        # A module needing a symbol the engine does not export must be refused
        # by the loader instead of crashing later.
        missing_src = work / "probe_missing_export.cpp"
        missing_src.write_text(
            '#include "recomp.h"\n'
            f'extern "C" void {MISSING_EXPORT}(uint8_t* rdram, recomp_context* ctx);\n'
            f'extern "C" void sbk_probe_use_missing(uint8_t* r, recomp_context* c) {{ {MISSING_EXPORT}(r, c); }}\n'
        )
        try:
            bad = build_probe(toolchain, work / "bad", [missing_src], [MISSING_EXPORT])
        except BuilderError as error:
            failures.append(f"negative probe build failed: {error.message}")
        else:
            result = validate(args.engine, bad)
            if result.returncode == 0 or "MODULE_LOAD_ERROR" not in result.stderr:
                failures.append("engine accepted a module importing a symbol it does not export:\n"
                                + result.stdout + result.stderr)
            else:
                print("Missing-export module refused as expected: " + result.stderr.strip().splitlines()[0])

    for failure in failures:
        print(f"FAIL: {failure}", file=sys.stderr)
    if failures:
        return 1
    print("PASS: engine loads, validates and initializes the synthetic game module")
    return 0


if __name__ == "__main__":
    sys.exit(main())
