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


Renderer stack advanced through DXC shader generation into RecompFrontend UI compilation (545+/609 steps). The next boundary was RecompFrontend's required game-side UI ABI header. SnowboardKids-Recomp now provides project-owned event structs and the `recomp_run_ui_callbacks` declaration without importing SBK2-specific patch behavior.


Renderer stack reached 608/609 build steps. The last compile blocker was a Linux window ABI mismatch: N64ModernRuntime used `SDL_Window*`, while RecompFrontend saw Plume's X11 `RenderWindow` because the SDL/Vulkan macro was scoped only to RT64. The root build now propagates `PLUME_SDL_VULKAN_ENABLED` and `RT64_SDL_WINDOW_VULKAN` to the frontend, matching the working SBK2 Linux configuration.


## Renderer/frontend compile validation — PASS

On 2026-09-23 the pinned RT64 + RecompFrontend stack completed all 609 build steps successfully.

Observed result:

```text
[609/609] Linking CXX static library RecompFrontend/recompui/librecompui.a
Renderer/frontend compile validation passed.
```

This closes the isolated renderer/frontend dependency validation. The Linux build now has working compile-time integration for:

- SDL2
- Vulkan/Plume
- RT64
- DXC -> SPIR-V shader generation
- RecompFrontend input/UI
- N64ModernRuntime window ABI

Next: build and run `SnowboardKidsRecompiled`, a minimal graphical boot executable that registers generated overlay tables, validates the local ROM, opens an SDL/Vulkan window, starts N64ModernRuntime, and traces entry into `recomp_entrypoint`.


First graphical boot target compiled all 146 objects and reached the final executable link. The remaining unresolved references were frontend host contracts rather than game/runtime translation failures:

```text
window
supported_games
recompui::controls_page
```

`window` and `supported_games` are now exposed as program-owned globals, matching RecompFrontend's expected contract. The `controls_page` failure was static archive ordering: `recompinput` references UI state from `recompui`, while `recompui` also references input code. The boot link now re-scans `recompui` after `recompinput`.


## First graphical window — PASS

On 2026-09-23 `SnowboardKidsRecompiled` linked successfully and launched its first real SDL2/Vulkan window.

Observed runtime milestones:

```text
ROM validation: Good
Generated entrypoint: 0x80000400
Starting N64ModernRuntime...
SDL video driver: x11
SDL/Vulkan window created: 1280x720
Device Name: NVIDIA GeForce RTX 3060 Laptop GPU
Device Vendor: 0x10DE
```

RT64 reached renderer/UI initialization. The process then aborted before `recomp_entrypoint` because RecompFrontend had no registered primary font:

```text
what(): No primary font was registered with recompui::register_primary_font
```

This is a frontend asset/theme boundary, not a renderer, ROM, or recompiled-game failure.

Resolution in progress/completed in source:

- pin shared `snowboardkids-recomp-theme` at `0cb9a83a263607fbc8ab6176a758a00726e237cc`;
- call `snowboardkids::theme::apply()` before renderer startup;
- set RecompFrontend program name/id;
- package the theme's fonts, promptfont, icons and `recomp.rcss` beside the executable;
- run the executable from its build directory so RecompFrontend's Linux relative asset lookup resolves correctly.

Next success marker remains:

```text
>>> ENTERING SNOWBOARD KIDS RECOMP_ENTRYPOINT
```


Frontend assets now load successfully during the first graphical boot. The runtime loaded Noto Emoji, PromptFont, Fredoka and all shared Lato faces, then stopped at the next initialization contract:

```text
Configurations have not been loaded. Call recompui::config::finalize() first.
```

The diagnostic boot now initializes RecompFrontend's standard General, Graphics, Controls, Sound and Mods tabs and calls `recompui::config::finalize()` before starting N64ModernRuntime.


The next graphical boot passed frontend configuration, font loading and RT64 initialization, then reached the runtime's initial 48 kHz audio setup before a segmentation fault. The generated game entrypoint trace had not fired yet.

Root-cause analysis of pinned N64ModernRuntime identified an initialization race in the diagnostic launcher: it called `recomp::start_game()` before `recomp::start()`. The VI thread therefore observed the game as already running and skipped its dummy VI initialization, leaving the first `ViState::mode` null before `update_vi()`.

The diagnostic boot now leaves the game stopped while the runtime initializes. Its first VI callback fires only after the dummy VI mode/framebuffer has been seeded, then calls `recomp::start_game()`. This should wake the game thread at a valid video state and allow the generated entrypoint to run.


## Generated game entrypoint — PASS

On 2026-09-23 the first graphical native boot reached and executed Snowboard Kids' generated entrypoint:

```text
First safe VI reached; starting Snowboard Kids...
Initializing recomp heap at offset 0x01000000 with size 0x1F000000
>>> ENTERING SNOWBOARD KIDS RECOMP_ENTRYPOINT
<<< SNOWBOARD KIDS RECOMP_ENTRYPOINT RETURNED
N64 audio frequency requested: 22050 Hz
```

This completes the M1.5 exit criterion: original SBK1 code translated by N64Recomp is executing inside N64ModernRuntime on the Linux x86-64 host.

The next runtime blocker is genuine game/libultra behavior. Snowboard Kids calls:

```c
osStopThread(&gAudioThread);
...
osStartThread(&gAudioThread);
```

while processing audio state. The pinned N64ModernRuntime only implemented `osStopThread` for the current thread and asserted when stopping a different thread.

A project-owned runtime compatibility patch now implements libultra-compatible stopping of queued/blocked target threads and repairs `thread_queue_remove()` traversal. The patch is applied reproducibly by `scripts/apply-runtime-patches.sh`.


## First rendered Snowboard Kids frame — PASS

On 2026-09-23 the native port rendered its first visible Snowboard Kids scene through the full host stack.

Observed on the Linux host:

- `SnowboardKidsRecompiled` launched successfully;
- N64ModernRuntime reached and executed the generated game entrypoint;
- the game initialized its 22050 Hz audio path;
- the project-owned libultra thread compatibility patch allowed execution to continue beyond `osStopThread(&gAudioThread)`;
- RT64/Vulkan produced a visible in-game snowy mountain scene in the SDL window.

This completes the project's first-frame milestone. The screenshot confirms that original game rendering commands are reaching RT64 and producing host GPU output.

Next focus:

1. confirm boot stability beyond the first rendered scene;
2. reach title/logo/menu;
3. replace bootstrap no-input callbacks with RecompFrontend/recompinput input handling;
4. validate a complete playable path into a race.


## Title/menu reached — PASS

After the initial runtime/libultra thread compatibility patch, the native executable progressed beyond the first rendered mountain scene and reached the Snowboard Kids menu.

Observed behavior:

- game progression reached the menu;
- rendering was visible but flickered/twinkled;
- there was no audible output;
- controls appeared inactive;
- the executable later terminated after reaching the menu.

The missing audio and controls were expected limitations of the diagnostic launcher at this point: its audio callback discarded every sample and its input callback always returned zeroed controls.

The diagnostic launcher now uses the real RecompFrontend/RecompInput path:

- `recompinput::handle_events()`
- `recompinput::poll_inputs()`
- `recompinput::profiles::get_n64_input()`
- `recompinput::set_rumble()`

It also opens a real SDL float stereo audio device at 48 kHz, converts the game's requested sample rate (observed 22050 Hz), performs the N64 stereo channel swap, queues output to SDL, and reports queued frames back to N64ModernRuntime.

Default keyboard mapping includes:

- Enter -> Start
- Space -> A
- Left Shift -> B
- WASD -> analog stick
- Arrow keys -> C buttons
- IJKL -> D-pad

Remaining issues after input/audio enablement:

1. determine the exact post-menu termination from terminal output;
2. diagnose visible frame flicker independently of game progression;
3. validate sustained audio and interactive menu navigation.


## Audible audio and interactive menu — PASS

The native port now produces audible game audio and accepts real keyboard input through RecompFrontend/RecompInput. The user reached the menu, pressed Enter to confirm a player, and advanced toward race start.

Observed host behavior:

- SDL audio output is audible;
- menu input works;
- player selection works;
- rendering still visibly flickers;
- transition toward race start aborts with:

```text
Failed to find function at 0x800A3500
... librecomp/src/overlays.cpp: get_function(...): Assertion `false' failed.
```

The matching decomp identifies:

```text
0x800A3500 = sprintf_text_0000
0x800A3524 = sprintf
```

The 0x24-byte function at 0x800A3500 is the static output callback used by `sprintf` / `_Printf` (the decompiled source calls it `proutSprintf`). It is passed as a function pointer, so runtime lookup must resolve its original N64 address.

N64Recomp can discover static functions while recompiling, but automatically discovered statics are not added to the runtime `section_functions` table emitted into `recomp_overlays.inl`. The project now declares this callback explicitly as an input manual function:

```toml
manual_funcs = [
    { name = "sprintf_text_0000", section = ".main", vram = 0x800A3500, size = 0x24 },
]
```

`build-native-boot.sh` now regenerates N64Recomp output before compiling so new manual indirect targets are incorporated automatically.
