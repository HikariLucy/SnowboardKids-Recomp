# M1 — Native Recompilation Feasibility

Updated: 2026-09-23

## Objective

Determine whether the matching Snowboard Kids ELF can be consumed directly by N64Recomp and establish the minimum set of patches/runtime work needed to reach native execution.

This phase deliberately stops short of frontend, RT64, enhancements, or packaging. First we need a trustworthy CPU recompilation baseline.

## Confirmed inputs

### Original game

USA ROM SHA-1:

```text
1583bacc9046a360df8ea4d536942155247e154c
```

The ROM itself must remain local and must never be committed.

### Matching decompilation

Reference:

```text
https://github.com/cdlewis/snowboardkids-decomp
```

Baseline revision used during initial research:

```text
03088d8c5164644c7697846770b091d5265e53de
```

The local decomp successfully produces:

```text
build/snowboardkids.elf
build/snowboardkids.z64
```

with the rebuilt ROM verifying as `OK`.

### N64Recomp

Reference:

```text
https://github.com/N64Recomp/N64Recomp
```

Initial pinned revision:

```text
ffb39cdad1da5de07eaaa48bd1db4a89a7986771
```

N64Recomp supports using an ELF directly as metadata/input. That is our first approach; we do not need to create a custom symbol dump before testing the matching ELF.

## Confirmed memory layout

From the Snowboard Kids decomp configuration:

| Component | ROM | VRAM |
| --- | ---: | ---: |
| Entry code | `0x001000` | `0x80000400` |
| Main code | `0x001050` | `0x80000450` |

The entry function is named:

```text
entrypoint
```

and jumps into `main` after clearing the main BSS.

Therefore the first N64Recomp configuration uses:

```toml
entrypoint = 0x80000400
```

## RSP observations

The first game contains at least:

| Microcode | ROM start | Next block | Size |
| --- | ---: | ---: | ---: |
| `aspMain` | `0xB1AA0` | `0xB28C0` | `0xE20` |
| `f3dlx` | `0xB28C0` | — | TBD |

The `aspMain` size matches the `0xE20` audio microcode block used by Snowboard Kids 2 Recompiled, but this does **not** prove the binaries or indirect-branch targets are identical. We will not copy Snowboard Kids 2's RSP branch-target list blindly.

## Why Snowboard Kids 2 is useful

Reference project:

```text
https://github.com/cdlewis/snowboardkids2-recomp
```

Reference revision during initial research:

```text
031ee23a01ec23ef40660bfb659bd8fcb5a3e265
```

It demonstrates a production architecture built around:

- N64Recomp
- RSPRecomp
- N64ModernRuntime
- RT64
- native Linux/Windows/macOS builds
- runtime ROM loading instead of redistributing game assets
- patch recompilation
- modern frontend/input support

We will use it as an architectural reference, not copy game-specific addresses or patches without verification.

## M1 execution order

### M1.1 — Build pinned N64Recomp

Run:

```bash
bash scripts/bootstrap-n64recomp.sh
```

Expected tools:

```text
build-tools/n64recomp/N64Recomp
build-tools/n64recomp/RSPRecomp
```

### M1.2 — Parse the matching ELF

Run:

```bash
bash scripts/run-recompiler.sh --dump-context
```

This is intentionally the first experiment. It asks N64Recomp to parse the matching ELF and emit its understanding of sections/functions without yet treating generated C as a finished port.

Expected generated diagnostics:

```text
dump.toml
data_dump.toml
```

Questions to answer:

1. Does N64Recomp parse the ELF successfully?
2. Does it find the entrypoint at `0x80000400`?
3. How many functions and executable sections does it detect?
4. Are there duplicate/ambiguous symbols?
5. Are absolute symbols or MDEBUG mappings required?
6. Does the matching ELF expose enough relocation information?
7. Which functions must initially be ignored, stubbed, or renamed?

### M1.3 — Generate CPU recompilation output

If M1.2 is healthy:

```bash
bash scripts/run-recompiler.sh
```

Expected output directory:

```text
RecompiledFuncs/
```

At this point success means *N64Recomp generated native C/C++ translation output*. It does not yet mean the game boots.

### M1.4 — RSP audio microcode

Only after CPU output is understood:

- verify Snowboard Kids `aspMain` control flow;
- determine indirect branch targets;
- create a game-specific RSPRecomp configuration;
- generate `rsp/aspMain.cpp`.

### M1.5 — Runtime skeleton

After CPU + required RSP generation:

- pin N64ModernRuntime;
- pin RT64;
- implement game registration and ROM validation;
- register the entrypoint and sections;
- attempt first native execution.

## M1 exit criterion

M1 is complete when:

1. the matching ELF is parsed reproducibly;
2. N64Recomp emits the main CPU translation;
3. required RSP microcode inputs are identified;
4. all blockers to constructing the first runtime executable are documented.

The next milestone, M2, begins with native runtime integration and first boot.


## M1.3 first CPU recompilation attempt

The first full CPU translation reached the game entrypoint and then failed on:

```text
[Info] Indirect tail call in recomp_entrypoint
Unhandled cop0 register in mfc0: 11
Error in recompiling aspMainTextStart, clearing output file
Error recompiling aspMainTextStart
```

This is not a normal game-CPU function failure. `aspMainTextStart` is the embedded audio RSP microcode. N64Recomp was incorrectly attempting to interpret RSP instructions as CPU instructions because the matching ELF exposes the microcode blob as an executable symbol.

Resolution:

- explicitly ignore `aspMainTextStart` in the CPU recompilation config;
- explicitly ignore `gspF3DLX_fifoTextStart` for the same reason;
- keep `rspbootTextStart` under N64Recomp's built-in ignored microcode handling;
- process the required RSP microcode later with `RSPRecomp`.

The `cop0 register 11` message is therefore expected from decoding the wrong ISA for that blob, not evidence that Snowboard Kids requires unsupported CPU COP0 behavior at this point.


## M1.3 successful CPU translation

**Status: COMPLETE (2026-09-23)**

After excluding the embedded RSP microcode symbols from the CPU path, N64Recomp completed successfully:

```text
Function count: 5124
Working dir: .../SnowboardKids-Recomp
[Info] Indirect tail call in recomp_entrypoint
```

Generated output:

```text
RecompiledFuncs/
43 files
~16 MiB
```

The `Indirect tail call in recomp_entrypoint` message is informational and did not abort generation.

This confirms the first complete static CPU translation of the Snowboard Kids matching ELF.

## M1.4 audio RSP strategy

Snowboard Kids contains `aspMain` audio microcode at ROM `0xB1AA0`, with the next RSP text block beginning at `0xB28C0`. Therefore its text size is exactly `0xE20`.

Initial RSPRecomp parameters:

```text
text_offset  = 0xB1AA0
text_size    = 0xE20
text_address = 0x04001080
```

The IMEM address follows the standard N64 audio-task layout and matches the working Snowboard Kids 2 recomp architecture.

Important correction: the graphics F3DLX microcode is **not** planned for static RSP recompilation. The working Snowboard Kids 2 port compiles `rsp/aspMain.cpp` but leaves graphics microcode to the modern runtime/RT64 graphics path.

We intentionally start with an empty `extra_indirect_branch_targets` list. Snowboard Kids 2 has a known game-specific list, but identical size alone is not sufficient evidence that Snowboard Kids 1 uses the exact same jump table. Any required targets will be verified before being added.

Run:

```bash
bash scripts/run-rsp-recompiler.sh
```

Expected output:

```text
rsp/aspMain.cpp
```


## M1.4 indirect jump table verified

**Status: VERIFIED (2026-09-23)**

The candidate command table was extracted directly from the verified Snowboard Kids USA ROM:

```text
aspMainDataStart = ROM 0xE2B00
dispatcher table = data + 0x10 = ROM 0xE2B10
entries          = 16 x big-endian u16
valid targets    = 16/16
unique targets   = 16/16
```

Verified target list:

```text
0x1118 0x1470 0x11DC 0x1B38
0x1214 0x187C 0x1254 0x12D0
0x12EC 0x1328 0x140C 0x1294
0x1E24 0x138C 0x170C 0x144C
```

These values match the Snowboard Kids 2 RSP configuration exactly. This is no longer being treated as copied reference data: the SBK1 ROM independently contains the same 16-entry table at the address selected by its own dispatcher.

Therefore these targets are now included in `aspMain.us.toml` as `extra_indirect_branch_targets`.

Regenerate with:

```bash
bash scripts/run-rsp-recompiler.sh
```

This closes the static-analysis portion of M1.4. Runtime execution will remain the final behavioral validation.
