"""Command-line interface for the Snowboard Kids game module builder."""

import argparse
from pathlib import Path
import sys

from .errors import BuilderError, BuilderExitCode
from .service import BuilderConfig, ModuleBuilderService

VERSION = "1.0.0"


def emit_status(phase: str, current=None, total=None, *, protocol=False):
    """Version-one line protocol. Counts describe completed compile units only."""
    if protocol:
        name = None
        if phase.startswith(("Validating ROM", "Discovering")):
            name = "validate"
        elif phase.startswith("Locating recompiled"):
            name = "cpu"
        elif phase.startswith("Generating RSP"):
            name = "rsp"
        elif phase.startswith("Compiling"):
            name = "compile"
        elif phase.startswith("Validating module"):
            name = "validate_module"
        elif phase.startswith("Installing"):
            name = "install"
        if name:
            cur, tot = (current, total) if phase.startswith("Compiling [") else (0, 0)
            print(f"SBK_PROGRESS\t{name}\t{cur}\t{tot}", flush=True)
        return
    if current is not None and total == 6 and not phase.startswith("Compiling ["):
        print(f"[{current}/{total}] {phase}...", flush=True)
    else:
        print(f"  -> {phase}", flush=True)


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(
        prog="SnowboardKidsModuleBuilder",
        description="Compile a local Snowboard Kids (USA) game module from your commercial ROM image.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Exit Codes:
  0: Success
  1: Wrong or unsupported ROM
  2: Missing C++ compiler (clang++, g++, or MSVC required)
  3: Code generation failure (RSPRecomp)
  4: C++ compilation failure
  5: Linker failure
  6: Module ABI validation failure
  7: Storage or permission error
  8: User cancelled
        """
    )
    parser.add_argument("rom", nargs="?", type=Path, help="Path to Snowboard Kids (USA) ROM (.z64, .n64, .v64)")
    parser.add_argument("--rom", dest="rom_opt", type=Path, help="Explicit path to ROM")
    parser.add_argument("--out", type=Path, default=None, help="Explicit output path for the compiled library (.so/.dll)")
    parser.add_argument("--out-dir", type=Path, default=None, help="Output directory to place module and MODULE-INFO.json")
    parser.add_argument("--inputs", type=Path, default=None,
                        help="Module inputs bundle (.zip or directory) from scripts/export-module-inputs.py; "
                             "replaces the local corpus and RSPRecomp (Windows builds)")
    parser.add_argument("--engine", type=Path, default=None,
                        help="SnowboardKidsEngine executable used for --validate-module "
                             "(default: $SBK_ENGINE, then build-engine/)")
    parser.add_argument("--jobs", "-j", type=int, default=4, help="Compiler worker threads (default: 4)")
    parser.add_argument("--cxx", type=str, default=None, help="Explicit C++ compiler executable")
    parser.add_argument("--non-interactive", action="store_true", help="Run without interactive prompts")
    parser.add_argument("--keep-temp", "--debug", action="store_true", help="Preserve temporary build files for diagnostics")
    parser.add_argument("--progress-protocol", action="store_true", help="Emit machine-readable first-run phase events")
    parser.add_argument("--version", action="version", version=f"%(prog)s {VERSION}")

    args = parser.parse_args(argv)

    rom_path = args.rom_opt or args.rom
    if not rom_path:
        print("ERROR: No ROM path specified. Provide path to Snowboard Kids (USA) ROM image.", file=sys.stderr)
        parser.print_usage(sys.stderr)
        return BuilderExitCode.WRONG_ROM

    config = BuilderConfig(
        rom_path=rom_path,
        out_dir=args.out_dir,
        explicit_out_file=args.out,
        cxx=args.cxx,
        jobs=max(1, args.jobs),
        keep_temp=args.keep_temp,
        debug=args.keep_temp,
        inputs=args.inputs,
        engine=args.engine,
    )

    def print_status(phase: str, current: int = None, total: int = None):
        emit_status(phase, current, total, protocol=args.progress_protocol)

    print("==================================================")
    print(" Snowboard Kids Local Game Module Builder")
    print("==================================================")
    print(f"ROM Image: {rom_path}")
    if args.inputs:
        print(f"Inputs:    {args.inputs}")

    builder = ModuleBuilderService(config)
    try:
        result = builder.build(status_callback=print_status)
    except BuilderError as e:
        print(f"\nBUILD FAILED: {e.message}", file=sys.stderr)
        return e.exit_code
    except Exception as e:
        print(f"\nUNEXPECTED ERROR: {e}", file=sys.stderr)
        return BuilderExitCode.COMPILE_FAILURE

    size_mb = result.module_path.stat().st_size / (1024 * 1024)
    print("\nBUILD SUCCESS!")
    print(f"Module Path:   {result.module_path} ({size_mb:.2f} MiB)")
    print(f"Manifest Path: {result.manifest_path}")
    print(f"Build Time:    {result.build_time_seconds:.2f}s")
    print(f"Corpus Digest: {result.metadata.get('corpus_digest')}")
    print(f"Functions:     {result.metadata.get('function_count')}")
    print(f"Validation:    {result.metadata.get('validation')}")
    print("==================================================")
    return BuilderExitCode.SUCCESS


if __name__ == "__main__":
    sys.exit(main())
