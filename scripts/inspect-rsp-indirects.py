#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parent.parent
PATH = ROOT / "rsp" / "aspMain.cpp"

# Reference only: targets explicitly supplied by the working Snowboard Kids 2
# recomp. They are NOT assumed valid for Snowboard Kids 1.
SBK2_REFERENCE_TARGETS = {
    0x1118, 0x1470,
    0x11DC, 0x1B38,
    0x1214, 0x187C,
    0x1254, 0x12D0,
    0x12EC, 0x1328,
    0x140C, 0x1294,
    0x1E24, 0x138C,
    0x170C, 0x144C,
}

def main() -> int:
    if not PATH.is_file():
        print(f"Missing generated file: {PATH}", file=sys.stderr)
        print("Run: bash scripts/run-rsp-recompiler.sh", file=sys.stderr)
        return 1

    lines = PATH.read_text(encoding="utf-8").splitlines()

    case_re = re.compile(r"\bcase 0x([0-9A-Fa-f]+):")
    cases: set[int] = set()
    indirect_lines: list[int] = []

    for lineno, line in enumerate(lines, start=1):
        match = case_re.search(line)
        if match:
            cases.add(int(match.group(1), 16))
        if "goto do_indirect_jump;" in line:
            indirect_lines.append(lineno)

    print("Snowboard Kids — generated aspMain indirect control-flow summary")
    print("=" * 64)
    print(f"Generated file        : {PATH}")
    print(f"Indirect jump sites   : {len(indirect_lines)}")
    print(f"Generated case targets: {len(cases)}")
    print()

    print("Generated targets")
    for target in sorted(cases):
        print(f"  0x{target:04X}")

    overlap = cases & SBK2_REFERENCE_TARGETS
    reference_missing = SBK2_REFERENCE_TARGETS - cases

    print()
    print("Comparison with Snowboard Kids 2 reference targets")
    print(f"  overlap             : {len(overlap)}")
    print(f"  SBK2-only candidates: {len(reference_missing)}")
    if overlap:
        print("  overlapping targets : " + ", ".join(f"0x{x:04X}" for x in sorted(overlap)))
    if reference_missing:
        print("  reference-only list : " + ", ".join(f"0x{x:04X}" for x in sorted(reference_missing)))

    print()
    print("Indirect-jump source snippets")
    for n, lineno in enumerate(indirect_lines, start=1):
        print(f"\n--- site {n}, line {lineno} ---")
        start = max(1, lineno - 7)
        end = min(len(lines), lineno + 2)
        for current in range(start, end + 1):
            marker = ">" if current == lineno else " "
            print(f"{marker}{current:5}: {lines[current - 1]}")

    print()
    print("NOTE: SBK2 targets are comparison data only. Do not add them to SBK1")
    print("without evidence from SBK1 control flow or runtime behavior.")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
