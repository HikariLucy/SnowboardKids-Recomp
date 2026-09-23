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


## N64Recomp feasibility result

On 2026-09-23 the pinned N64Recomp toolchain built successfully at:

```text
ffb39cdad1da5de07eaaa48bd1db4a89a7986771
```

The verified matching ELF from the sibling decomp repository was accepted directly by N64Recomp:

```bash
bash scripts/run-recompiler.sh --dump-context
```

Result:

```text
Dumping context
```

The command exited successfully and generated the function/data context dumps. This completes M1.2 and confirms that a separate hand-authored symbol database is not required for the initial CPU recompilation experiment.

Next: summarize the generated context and run the first full CPU translation into `RecompiledFuncs/`.


## Audio RSP generation

On 2026-09-23 the Snowboard Kids audio RSP microcode was successfully translated with the pinned `RSPRecomp` toolchain.

Command:

```bash
bash scripts/run-rsp-recompiler.sh
```

Observed result:

```text
Generated: rsp/aspMain.cpp
2241 lines
~72 KiB
```

The generated file contains 4 indirect-jump sites and 14 automatically discovered `case` targets.

This completes **RSP code generation**, but not yet runtime validation. Audio microcode can use jump-table targets that are not statically discoverable from linked jumps. The working Snowboard Kids 2 recomp supplies 16 additional indirect targets explicitly; those values are being treated only as comparison evidence, not copied into Snowboard Kids 1 without verification.

Next validation command:

```bash
python3 scripts/inspect-rsp-indirects.py
```

After indirect control flow is understood, M1.5 moves to N64ModernRuntime + RT64 integration.


## Audio RSP indirect-target investigation

The generated SBK1 `aspMain.cpp` contains:

```text
4 indirect jump sites
14 automatically discovered case targets
```

The automatically discovered SBK1 targets have **zero exact overlap** with the 16 explicit targets used by the Snowboard Kids 2 recomp. Several SBK1 targets do, however, align with the SBK2 microcode family after a `-0x14` shift, which is evidence of structural similarity but not identity.

The first SBK1 indirect dispatcher is:

```text
lh  $2, 0x10($2)
jr  $2
```

The matching decomp also confirms:

```text
aspMainDataStart ROM = 0xE2B00
ucodeDataSize        = 0x800
```

Therefore the next check extracts the likely 16-entry audio command jump table directly from the verified SBK1 ROM instead of copying SBK2 addresses.

Run:

```bash
python3 scripts/extract-audio-jump-table.py
```

If the 16 halfwords at `aspMainDataStart + 0x10` all resolve inside the SBK1 `aspMain` IMEM text range, they become the candidate `extra_indirect_branch_targets` for runtime validation.


## Native-core compile attempt

The pinned N64ModernRuntime dependency bootstrap completed successfully at:

```text
6ccb2e7c2e7f6708257b461097e0aaf03c445e2a
```

The regenerated RSP output contains 30 indirect-jump cases: the 14 statically discovered targets plus the 16 ROM-verified command-table targets.

The first Clang host compilation failed only on the original game function named `main`. Clang interpreted it as the hosted process entrypoint and rejected the N64 recomp signature. The project now renames that function via N64Recomp's supported `renamed` mechanism.

Next:

```bash
rm -rf RecompiledFuncs build-native-core
bash scripts/run-recompiler.sh
bash scripts/build-native-core.sh
```


RSP host compile currently requires SSSE3 + SSE4.1 on x86-64 because the pinned N64ModernRuntime vector path uses those intrinsics. The project CMake now enables those flags only for `SnowboardKidsRsp`.


## Native core compile validation — PASS

On 2026-09-23 the generated Snowboard Kids CPU and audio RSP translations both compiled successfully as native host code with Clang 18.1.3.

Observed result:

```text
[43/43] Linking CXX static library libSnowboardKidsRsp.a
Native core compile validation passed.

CPU archive:
build-native-core/libSnowboardKidsCpu.a

RSP archive:
build-native-core/libSnowboardKidsRsp.a
```

This confirms:

- the N64Recomp CPU output compiles successfully for x86-64;
- the RSPRecomp `aspMain.cpp` output compiles successfully for x86-64;
- the `main -> main_recomp` rename resolved the hosted-C name collision;
- the RSP SIMD path compiles correctly with SSSE3 + SSE4.1 enabled.

The two remaining warnings originate in the pinned N64ModernRuntime RSP implementation and are non-fatal compiler precedence warnings.

**M1.5a is complete.**

Next: M1.5b — create the first native runtime executable skeleton and register Snowboard Kids with N64ModernRuntime.


Runtime smoke link reached the final executable after compiling all 149 objects. Four ignored libultra/debug symbols remained unresolved; project-local compatibility shims now mirror the pinned runtime's no-Controller-Pak policy and SBK1's no-op `rmonPrintf`.


## Runtime smoke validation — PASS

On 2026-09-23 the first native N64ModernRuntime smoke executable linked and ran successfully.

Observed result:

```text
GameEntry       : registered
Internal name   : SNOWBOARD KIDS
ROM hash        : 0xF384619787B78D4B
Entrypoint      : 0x80000400
ROM validation  : Good
Runtime ROM load: OK (8388608 bytes)
Result          : PASS
```

This confirms the generated game code can link into a real N64ModernRuntime executable, the runtime accepts the verified SBK1 ROM, and the complete 8 MiB normalized ROM is loaded successfully.

The next isolated validation is the graphics/frontend dependency stack:

```text
RT64              6a4166b2cfa952d931a08481d1037da995f28b54
RecompFrontend    e85b912d9df677b04f9358867dd010c8af27ea05
```

These are the exact revisions pinned by the working Snowboard Kids 2 recomp reference.


Renderer stack configuration reached RT64 Vulkan + RecompFrontend successfully. The first compile stopped in `recompinput` because SDL2's include directory was not propagated from the parent CMake project. SDL2 is installed; the root build now resolves it with `find_package(SDL2 REQUIRED)` before adding RecompFrontend, matching the working SBK2 build order.


Renderer stack advanced past SDL2 integration to shader generation (536+/609 steps). RecompFrontend then inherited RT64's shader helper functions without RT64's directory-local `DXC` variable, causing HLSL files to be invoked directly. The root CMake now exposes the pinned RT64 DXC command/options before adding RecompFrontend, following the working SBK2 build pattern.
