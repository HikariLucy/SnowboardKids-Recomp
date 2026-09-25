#!/usr/bin/env python3
"""Build a local SnowboardKidsGame dynamic module from a user-supplied ROM image."""

import argparse
import hashlib
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

EXPECTED_SHA1 = "1583bacc9046a360df8ea4d536942155247e154c"
ROOT = Path(__file__).resolve().parents[1]

def normalize_rom(rom_path: Path) -> bytes:
    data = rom_path.read_bytes()
    if len(data) < 4:
        raise ValueError(f"ROM file is too small: {len(data)} bytes")

    magic = data[:4]
    # Native Big-Endian (.z64): 0x80371240
    if magic == b'\x80\x37\x12\x40':
        return data
    # Byte-swapped (.v64): 0x37804012
    elif magic == b'\x37\x80\x40\x12':
        swapped = bytearray(data)
        for i in range(0, len(data), 2):
            swapped[i], swapped[i+1] = swapped[i+1], swapped[i]
        return bytes(swapped)
    # Little-Endian (.n64): 0x40123780
    elif magic == b'\x40\x12\x37\x80':
        swapped = bytearray(data)
        for i in range(0, len(data), 4):
            swapped[i], swapped[i+1], swapped[i+2], swapped[i+3] = (
                swapped[i+3], swapped[i+2], swapped[i+1], swapped[i]
            )
        return bytes(swapped)
    else:
        raise ValueError(f"Unknown ROM format / magic: {magic.hex()}")


def ensure_rsp_code(root: Path, rom_bytes: bytes, jobs: int = 4):
    rsp_out = root / "rsp" / "aspMain.cpp"
    if rsp_out.exists() and rsp_out.stat().st_size > 1000:
        return rsp_out

    rsp_out.parent.mkdir(parents=True, exist_ok=True)
    rsp_recomp = root / "build-tools" / "n64recomp" / "RSPRecomp"
    if not rsp_recomp.exists():
        # Check parent PFS build tools
        rsp_recomp_alt = root.parent / "SnowboardKids-Recomp-pfs" / "build-tools" / "n64recomp" / "RSPRecomp"
        if rsp_recomp_alt.exists():
            rsp_recomp = rsp_recomp_alt
        else:
            raise RuntimeError(f"RSPRecomp tool not found at {rsp_recomp}")

    with tempfile.NamedTemporaryFile(suffix=".z64", delete=False) as tmp_rom:
        tmp_rom.write(rom_bytes)
        tmp_rom_path = Path(tmp_rom.name)

    try:
        cfg_template = root / "aspMain.us.toml"
        cfg_path = root / "build-tools" / "rsp-build.toml"
        cfg_path.parent.mkdir(parents=True, exist_ok=True)
        text = cfg_template.read_text().replace('"../snowboardkids.z64"', f'"{tmp_rom_path}"')
        text = text.replace('"rsp/aspMain.cpp"', f'"{rsp_out.resolve()}"')
        cfg_path.write_text(text)

        subprocess.run([str(rsp_recomp), str(cfg_path)], cwd=root, check=True)
    finally:
        if tmp_rom_path.exists():
            tmp_rom_path.unlink()

    if not rsp_out.exists() or rsp_out.stat().st_size == 0:
        raise RuntimeError(f"Failed to generate {rsp_out}")
    return rsp_out


def find_corpus_dir(root: Path) -> Path:
    candidates = [
        root / "build-tools" / "production-continuation" / "corpus",
        root.parent / "SnowboardKids-Recomp-pfs" / "build-tools" / "production-continuation" / "corpus",
        root / "RecompiledFuncs",
    ]
    for c in candidates:
        if c.exists() and list(c.glob("funcs_*.c")):
            return c
    raise RuntimeError("Recompiled CPU source files not found. Ensure corpus is generated.")


def compile_game_module(root: Path, corpus_dir: Path, rsp_cpp: Path, out_path: Path, jobs: int = 4):
    import concurrent.futures
    cxx = os.environ.get("CXX", "c++")
    out_path.parent.mkdir(parents=True, exist_ok=True)

    runtime_dir = root / ".deps-runtime" / "N64ModernRuntime"
    if not runtime_dir.exists():
        runtime_dir = root.parent / "SnowboardKids-Recomp-pfs" / ".deps-runtime" / "N64ModernRuntime"

    sources = [
        root / "src" / "module" / "game_module_entry.cpp",
        rsp_cpp,
        corpus_dir / "lookup.cpp",
    ]
    funcs = sorted(corpus_dir.glob("funcs_*.c"))
    if not funcs:
        raise RuntimeError(f"No funcs_*.c files found in {corpus_dir}")
    sources.extend(funcs)

    print(f"Compiling {len(sources)} source files into {out_path} using {jobs} jobs...")
    t0 = time.time()

    common_flags = [
        "-std=c++20",
        "-O2",
        "-fPIC",
        "-fno-strict-aliasing",
        "-mssse3",
        "-msse4.1",
        "-Wno-unused-variable",
        "-Wno-unused-but-set-variable",
        f"-I{root / 'src'}",
        f"-I{corpus_dir}",
        f"-I{runtime_dir / 'librecomp' / 'include'}",
        f"-I{runtime_dir / 'N64Recomp' / 'include'}",
        f"-I{runtime_dir / 'ultramodern' / 'include'}",
    ]

    with tempfile.TemporaryDirectory(prefix="sbk_module_build_") as tmp_dir:
        tmp_path = Path(tmp_dir)
        obj_files = []

        def compile_one(src: Path):
            obj = tmp_path / f"{src.stem}_{hashlib.md5(str(src).encode()).hexdigest()[:6]}.o"
            cmd = [cxx] + common_flags + ["-c", str(src), "-o", str(obj)]
            res = subprocess.run(cmd, cwd=root, text=True, capture_output=True)
            if res.returncode != 0:
                raise RuntimeError(f"Failed to compile {src}:\n{res.stderr}")
            return obj

        with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as executor:
            future_to_src = {executor.submit(compile_one, src): src for src in sources}
            for future in concurrent.futures.as_completed(future_to_src):
                obj_files.append(future.result())

        link_cmd = [cxx, "-shared", "-o", str(out_path)] + [str(o) for o in obj_files]
        res = subprocess.run(link_cmd, cwd=root, text=True, capture_output=True)
        if res.returncode != 0:
            raise RuntimeError(f"Link failed with error code {res.returncode}:\n{res.stderr}")

    dt = time.time() - t0
    size_mb = out_path.stat().st_size / (1024 * 1024)
    print(f"Successfully generated {out_path} ({size_mb:.2f} MB in {dt:.1f}s)")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rom", type=Path, help="Path to Snowboard Kids (USA) ROM (.z64, .n64, .v64)")
    parser.add_argument("--out", type=Path, default=None, help="Output path for SnowboardKidsGame.so")
    parser.add_argument("--jobs", "-j", type=int, default=4, help="Compiler parallel jobs")
    args = parser.parse_args()

    if not args.rom.exists():
        sys.exit(f"ERROR: ROM file not found: {args.rom}")

    print(f"Validating ROM: {args.rom}")
    rom_bytes = normalize_rom(args.rom)
    sha1 = hashlib.sha1(rom_bytes).hexdigest()
    if sha1 != EXPECTED_SHA1:
        sys.exit(f"ERROR: ROM SHA-1 mismatch: got {sha1}, expected {EXPECTED_SHA1}\nMust be Snowboard Kids (USA).")

    print(f"PASS: Verified Snowboard Kids (USA) ROM (SHA-1: {sha1})")

    out_file = args.out
    if not out_file:
        if sys.platform == "win32":
            out_file = ROOT / "dist" / "modules" / "SnowboardKidsGame.dll"
        else:
            out_file = ROOT / "dist" / "modules" / "SnowboardKidsGame.so"

    print("Locating RSP microcode and recompiler sources...")
    rsp_cpp = ensure_rsp_code(ROOT, rom_bytes, args.jobs)
    corpus_dir = find_corpus_dir(ROOT)

    compile_game_module(ROOT, corpus_dir, rsp_cpp, out_file, args.jobs)
    print(f"\nGame module ready at: {out_file}")
    print(f"To run with engine:\n  ./SnowboardKidsRecompiled --module {out_file}")


if __name__ == "__main__":
    main()
