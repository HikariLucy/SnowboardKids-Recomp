#!/usr/bin/env python3
from __future__ import annotations

from collections import Counter, defaultdict
from pathlib import Path
import sys
import tomllib

ROOT = Path(__file__).resolve().parent.parent
FUNC_DUMP = ROOT / "dump.toml"
DATA_DUMP = ROOT / "data_dump.toml"
ENTRYPOINT = 0x80000400


def load(path: Path) -> dict:
    if not path.is_file():
        raise FileNotFoundError(path)
    with path.open("rb") as fh:
        return tomllib.load(fh)


def hx(value: int | None) -> str:
    return "—" if value is None else f"0x{value:08X}"


def main() -> int:
    missing = [p for p in (FUNC_DUMP, DATA_DUMP) if not p.is_file()]
    if missing:
        for path in missing:
            print(f"Missing: {path}", file=sys.stderr)
        print(
            "Run 'bash scripts/run-recompiler.sh --dump-context' first.",
            file=sys.stderr,
        )
        return 1

    funcs_doc = load(FUNC_DUMP)
    data_doc = load(DATA_DUMP)

    func_sections = funcs_doc.get("section", [])
    data_sections = data_doc.get("section", [])

    functions = []
    relocs = []
    for section in func_sections:
        section_name = section.get("name", "<unnamed>")
        for func in section.get("functions", []):
            functions.append((section_name, func))
        for reloc in section.get("relocs", []):
            relocs.append((section_name, reloc))

    symbols = []
    for section in data_sections:
        section_name = section.get("name", "<unnamed>")
        for sym in section.get("symbols", []):
            symbols.append((section_name, sym))

    names = Counter(func.get("name") for _, func in functions)
    vrams = defaultdict(list)
    for section_name, func in functions:
        vrams[func.get("vram")].append((section_name, func.get("name")))

    duplicate_names = sorted(
        (name, count) for name, count in names.items() if name and count > 1
    )
    duplicate_vrams = sorted(
        (vram, values) for vram, values in vrams.items()
        if vram is not None and len(values) > 1
    )

    entry_matches = [
        (section_name, func)
        for section_name, func in functions
        if func.get("vram") == ENTRYPOINT
    ]

    total_code_bytes = sum(int(f.get("size", 0)) for _, f in functions)

    print("Snowboard Kids — N64Recomp context summary")
    print("=" * 48)
    print(f"Function sections : {len(func_sections)}")
    print(f"Functions         : {len(functions)}")
    print(f"Function bytes    : {total_code_bytes} ({total_code_bytes / 1024:.1f} KiB)")
    print(f"Relocations       : {len(relocs)}")
    print(f"Data sections     : {len(data_sections)}")
    print(f"Data symbols      : {len(symbols)}")
    print(f"Duplicate names   : {len(duplicate_names)}")
    print(f"Duplicate VRAMs   : {len(duplicate_vrams)}")
    print()

    print(f"Entrypoint {hx(ENTRYPOINT)}")
    if not entry_matches:
        print("  NOT FOUND")
    else:
        for section_name, func in entry_matches:
            print(
                f"  {section_name}: {func.get('name', '<unnamed>')} "
                f"size={hx(func.get('size'))}"
            )
    print()

    print("Executable sections")
    for section in func_sections:
        print(
            f"  {section.get('name', '<unnamed>')}: "
            f"ROM={hx(section.get('rom'))} "
            f"VRAM={hx(section.get('vram'))} "
            f"size={hx(section.get('size'))} "
            f"functions={len(section.get('functions', []))} "
            f"relocs={len(section.get('relocs', []))}"
        )

    if duplicate_names:
        print()
        print("Duplicate function names")
        for name, count in duplicate_names[:25]:
            print(f"  {name}: {count}")
        if len(duplicate_names) > 25:
            print(f"  ... {len(duplicate_names) - 25} more")

    if duplicate_vrams:
        print()
        print("Multiple functions at the same VRAM")
        for vram, values in duplicate_vrams[:25]:
            joined = ", ".join(f"{sec}:{name}" for sec, name in values)
            print(f"  {hx(vram)} -> {joined}")
        if len(duplicate_vrams) > 25:
            print(f"  ... {len(duplicate_vrams) - 25} more")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
