#!/usr/bin/env python3
"""Export the local, ROM-derived module inputs to build SnowboardKidsGame on another machine.

Run on Linux (or WSL2), where the matching decomp ELF and the continuation
corpus can be generated. The bundle holds the generated CPU corpus and the RSP
microcode recompiled from YOUR ROM, with checksums and the ROM SHA-1, so that

    python scripts\\build-game-module.py C:\\path\\to\\snowboardkids.z64 --inputs sbk-module-inputs.zip

can compile and validate the module natively on Windows. The bundle is
ROM-derived: keep it on your own machines. It is never committed or published.
"""

import argparse
import hashlib
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

from module_builder.errors import BuilderError, BuilderExitCode, GeneratorError  # noqa: E402
from module_builder.generator import find_corpus_dir, generate_rsp_code  # noqa: E402
from module_builder.inputs import (  # noqa: E402
    DEFAULT_BUNDLE_NAME,
    NOTICE,
    collect_bundle_files,
    ensure_not_tracked_location,
    write_bundle,
)
from module_builder.rom import validate_rom  # noqa: E402

CORPUS_HELP = """\
The continuation corpus has not been generated in this checkout. On Linux/WSL2:

  # 1. matching decomp ELF from your ROM (see snowboardkids-decomp README)
  cd ../snowboardkids-decomp && make extract && make && cd -
  # 2. N64Recomp tools and the continuation generator
  bash scripts/bootstrap-n64recomp.sh
  bash scripts/build-continuation-generator.sh
  # 3. corpus (reads ../snowboardkids-decomp/build/snowboardkids.elf or $SBK_ELF)
  python3 tests/production_continuation/generate_corpus.py
"""


def head_commit():
    try:
        return subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT, capture_output=True,
                              text=True, check=True).stdout.strip() or None
    except (OSError, subprocess.CalledProcessError):
        return None


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("rom", type=Path, help="your Snowboard Kids (USA) ROM (.z64/.v64/.n64)")
    parser.add_argument("--out", type=Path,
                        default=ROOT / "build-tools" / "module-inputs" / DEFAULT_BUNDLE_NAME,
                        help="bundle to write (default: build-tools/module-inputs/%(default)s)")
    parser.add_argument("--corpus", type=Path, default=None,
                        help="generated corpus directory (default: build-tools/production-continuation/corpus)")
    args = parser.parse_args(argv)

    try:
        rom_bytes, format_name, rom_sha1 = validate_rom(args.rom)
        print(f"ROM OK: {format_name}, SHA-1 {rom_sha1}")
        ensure_not_tracked_location(ROOT, args.out)
        try:
            corpus = args.corpus.resolve() if args.corpus else find_corpus_dir(ROOT)
        except GeneratorError:
            print(CORPUS_HELP, file=sys.stderr)
            raise
        print(f"Corpus: {corpus}")
        with tempfile.TemporaryDirectory(prefix="sbk_inputs_") as tmp:
            rsp_cpp = generate_rsp_code(ROOT, rom_bytes, Path(tmp) / "rsp")
            print("RSP microcode recompiled from the ROM")
            files = collect_bundle_files(corpus, rsp_cpp)
            write_bundle(args.out, files, rom_sha1, head_commit())
    except BuilderError as e:
        print(f"EXPORT FAILED: {e.message}", file=sys.stderr)
        return e.exit_code

    digest = hashlib.sha256(args.out.read_bytes()).hexdigest()
    print(f"\nWrote {args.out} ({len(files)} files)\nSHA-256 {digest}")
    print(f"NOTE: {NOTICE}")
    print("Next, on Windows (Developer prompt, in the checkout):\n"
          "  python scripts\\build-game-module.py C:\\path\\to\\snowboardkids.z64 "
          f"--inputs C:\\path\\to\\{args.out.name}")
    return BuilderExitCode.SUCCESS


if __name__ == "__main__":
    sys.exit(main())
