# Project Status

Updated: 2026-09-23

## Baseline validated

The original Snowboard Kids USA dump has been normalized to big-endian Z64 format and verified with:

```text
SHA-1: 1583bacc9046a360df8ea4d536942155247e154c
Size: 8388608 bytes
```

The source file encountered during setup used the N64 extension but had the V64 byte-order header:

```text
37 80 40 12
```

It was byte-swapped to the canonical Z64 representation before validation.

## Upstream decomp baseline

Reference project:

```text
https://github.com/cdlewis/snowboardkids-decomp
```

Local environment validated on Ubuntu/Linux with:

- Python 3.12.3
- Clang 18.1.3
- GNU MIPS binutils
- IDO 5.3 static recomp toolchain
- splat64 0.39.1
- spimdisasm 1.42.4
- m2c
- n64-decomp-workbench

Submodules were initialized successfully:

- asm-differ
- asm-processor
- decomp-permuter

## Extraction result

The upstream extraction completed successfully.

Observed baseline:

- 3021 symbols loaded
- 23 relocations loaded
- 254 ROM segments migrated into 105 semantic bundles
- 254 readable assets
- 0 partial assets
- 0 unclassified assets
- 558 GLB exports
- 0 GLB export errors

## Matching build result

The upstream decompilation successfully rebuilt the game:

```text
[ linker ]  Linking build/snowboardkids.elf
[ objcpy ]  build/snowboardkids.elf
[ n64crc ]  build/snowboardkids.z64
crc1: 0xDBF4EA9D, crc2: 0x333E82C0
[ verify ]  Checking snowboardkids.sha1
build/snowboardkids.z64: OK
```

Compiler warnings observed during the matching build are currently non-blocking and belong to the upstream decomp baseline. They should not be "cleaned up" blindly because source changes can alter matching code generation.

## Current milestone

**M0 — Reproducible matching decomp build: COMPLETE**

Next milestone:

**M1 — Native recomp feasibility and project skeleton**

The next work should focus on identifying the exact N64Recomp configuration, runtime integration, patches, and symbol inputs required to execute Snowboard Kids natively without depending on an emulator.
