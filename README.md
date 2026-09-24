# SnowboardKids-Recomp

Native recompilation / PC-port project for **Snowboard Kids (Nintendo 64, USA)** using N64Recomp, N64ModernRuntime, RT64 and RecompFrontend.

> This repository does **not** distribute the original ROM or copyrighted game assets. A legally obtained USA ROM is required locally.

## Current status

The project is a **playable native Linux port under active development**.

Validated today:

- matching Snowboard Kids USA decompilation baseline;
- native CPU recompilation and audio RSP recompilation;
- N64ModernRuntime boot;
- RT64/Vulkan rendering;
- RecompFrontend + RecompInput;
- keyboard input and audible music/SFX;
- title/menu, Controller Pak warning, character/course selection;
- full playable race;
- stable `PresentationMode::Console` with the original severe flicker eliminated;
- Options and Mods tabs;
- HD internal-resolution presets through 2160p / 4K-height;
- P1 / P1.5 serializable-continuation research;
- P2 runtime quiescence;
- P3 / P3.1 semantic RT64 renderer export/import, including GPU-authoritative framebuffer restoration;
- P4-A production continuation backend integrated into the real executable;
- reproducible dependency patch application with clean-build validation.

### Project maturity

**Approximate full-product maturity: ~52%.**

An earlier README reported roughly **94%**, but that number referred to a much narrower native-port / technical-R&D roadmap. It is no longer the project-wide progress metric.

The current denominator includes the complete product vision: full-game compatibility, modern controls, multiplayer, original save behavior, mods, widescreen/PC enhancements, native savestates, automated builds, packaging and public-release readiness.

The core native-port foundation is substantially further along than 52%; the lower global number reflects the expanded product scope.

## Product goal

The goal is not only to make Snowboard Kids boot on PC. The target is a polished, reproducible and eventually publicly distributable native port that can serve as a strong Snowboard Kids 1 reference implementation.

The current benchmark is the completeness and repository quality of the Snowboard Kids 2 recomp project, while keeping Snowboard Kids 1 behavior and technical requirements independent.

Major product areas:

- complete single-player game compatibility;
- Controller Pak and original save behavior;
- keyboard + modern gamepads + remapping/deadzones/rumble;
- local multiplayer;
- HD/4K rendering plus real widescreen research;
- robust audio;
- first-class mod support and templates;
- native savestates;
- Windows/Linux/macOS release engineering;
- automated CI/artifacts/releases;
- mature contributor and user documentation.

See [Project vision](docs/PROJECT-VISION.md) and [Roadmap](docs/ROADMAP.md).

## Graphics

Snowboard Kids 1 currently uses:

```text
PresentationMode::Console
```

The previous `PresentEarly` default caused severe framebuffer flicker.

Validated internal-resolution presets:

| Preset | Scale | 4:3 render resolution |
|---|---:|---:|
| Original | 1x | 320x240 |
| 480p | 2x | 640x480 |
| 720p-class | 3x | 960x720 |
| 1080p-class | 4.5x | 1440x1080 |
| 1440p-class | 6x | 1920x1440 |
| 2160p / 4K-class | 9x | 2880x2160 |
| Auto | window-dependent integer | preserves 4:3 |

New configurations default to the original 4:3 aspect ratio. True 16:9/ultrawide support remains future work.

## Controls

Validated:

- keyboard input;
- RecompInput frontend path;
- digital Xbox controller inputs such as A, D-pad and Start.

Still pending:

- analog-stick gameplay mapping on the tested Xbox controller;
- user-facing remapping;
- deadzone validation;
- rumble validation;
- additional controller families;
- multiplayer controller validation.

## Mods

The N64ModernRuntime/RecompFrontend mod framework is present and the SBK1 Mods UI opens correctly.

Current state:

- Mods tab: working;
- stable `mod_game_id = "snowboardkids"`: working;
- runtime mod infrastructure: present;
- SBK1 exports/hooks/events audit: pending;
- first end-to-end SBK1 code mod: pending;
- mod template: pending;
- texture-pack validation: pending.

Modding should currently be described as **infrastructure-ready**, not fully validated end-to-end.

## Native savestates

Savestates are being implemented as a native-runtime feature, not as an RDRAM-only dump.

Current sequence:

```text
P1     Serializable continuation feasibility          PASS
P1.5   Continuation schema/generalization             PASS
P2     Runtime quiescence                             PASS
P3     RT64 semantic renderer export/import           PASS
P3.1   GPU-authoritative framebuffer restoration      PASS
P4-A   Production continuation backend                FINAL MANUAL GATE PENDING
P4-B   Semantic in-memory capture                     BLOCKED ON P4-A
P4-C   Transactional restore                          PENDING
P4-D   Real gameplay save -> advance -> load          PENDING
P4-E   Stress / rollback / fault injection            PENDING
P5     Audio/timing restoration fidelity              PENDING
P6     Versioned .sbks persistence                    PENDING
P7     F5/F8 quick-save/load UX                       PENDING
```

### P2

Validated quiescence state machine:

```text
Idle -> Requested -> ParkGame -> CloseVI -> DrainDevices -> Frozen -> Resume -> Idle
```

Coverage included:

- 600 synthetic coordinator cycles;
- 41 runtime-kernel cycles;
- 660 live RT64/Vulkan gameplay cycles;
- zero deadlocks/timeouts/audit failures.

The renderer barrier resolves **3 GPU queue acknowledgements + 1 CPU renderer-worker acknowledgement = 4 participants**.

### P3 / P3.1

Renderer savestate work now preserves semantic RT64 state rather than raw host GPU objects.

P3.1 established an important invariant: active framebuffer/depth contents are frequently **GPU-authoritative rather than equivalent to RDRAM**, so restoration uses explicit GPU readback/import while preventing stale CPU RDRAM copies from overwriting restored framebuffer state.

### P4-A

The production executable now has a real continuation backend rather than relying only on isolated P1/P1.5 prototypes.

Validated code-side evidence includes:

- 1,981 generated functions;
- 56 HLE entries;
- host-owned lifetime-checked execution-owner registry;
- startup execution context retirement;
- real interactive path through menu -> character select -> course select -> race;
- more than 200 million continuation dispatches observed in one manual session;
- zero native suspendable fallbacks observed in that validation;
- normalized project-owned dependency patches;
- fresh and idempotent patch application;
- partial-state and wrong-upstream rejection;
- clean build from pinned dependencies with `SBK_CONTINUATIONS=ON`;
- production continuation and auditor/schema test suites passing.

**P4-A is not marked PASS yet.** The remaining acceptance gate is one complete manual interactive race reaching `race_finish` with zero continuation fallbacks. P4-B capture/restore work must not start until that gate passes.

## Remaining major work

The highest-level unfinished areas are:

- complete P4 native in-memory savestates;
- broader full-game regression and progression coverage;
- Controller Pak/original save compatibility;
- clean runtime shutdown;
- modern-controller analog/remapping/rumble support;
- local multiplayer validation;
- true widescreen/ultrawide research;
- first end-to-end SBK1 mod;
- packaging, CI and release automation;
- Windows/macOS validation and public-release readiness.

## Reproducible dependency patches

Project-specific changes must be represented as reproducible patches rather than undocumented edits inside dependency trees.

Current canonical runtime order includes:

```text
N64ModernRuntime pin
  -> osStopThread compatibility
  -> quiescence
  -> continuations
```

RecompFrontend graphics/runtime patch ordering includes:

```text
resolution
  -> quiescence
```

The patch applicator validates full file contents/permissions, supports idempotent re-application and rejects partial or incompatible upstream states.

## ROM baseline

```text
SHA-1     : 1583bacc9046a360df8ea4d536942155247e154c
XXH3-64   : F384619787B78D4B
Size      : 8388608 bytes
Game code : NSKE
```

The ROM is never stored in this repository.

## Project pipeline

```text
Legally obtained Snowboard Kids ROM
        |
        v
Matching decompilation / symbols / asset maps
        |
        v
N64Recomp + RSPRecomp
        |
        v
N64ModernRuntime
        |
        +--> RecompFrontend / RecompInput
        |
        +--> RT64 / Vulkan
        |
        v
Native Snowboard Kids executable
```

## Development rules

1. Never commit ROM files.
2. Never commit extracted copyrighted assets unless redistribution is explicitly permitted.
3. Keep build output, logs, runtime data and crash dumps out of Git.
4. Record baseline hashes and pinned upstream revisions.
5. Prefer reproducible project-owned patches over untracked dependency edits.
6. Keep compatibility fixes separable from optional PC enhancements.
7. Do not claim a feature complete until it has passed a real runtime validation path.
8. Distinguish branch-qualified validation from code merged into `main`.

## Documentation

- [Project vision](docs/PROJECT-VISION.md)
- [Current status](docs/STATUS.md)
- [Roadmap](docs/ROADMAP.md)
- [M1 feasibility](docs/M1-FEASIBILITY.md)

Some current savestate implementation/report documents remain on `feat/savestate-architecture` until that work is merged.

## Disclaimer

This is an unofficial preservation, interoperability and reverse-engineering research project. Snowboard Kids and related trademarks/assets belong to their respective rights holders.
