# SnowboardKids-Recomp

Experimental native recompilation / PC-port project for **Snowboard Kids (Nintendo 64, USA)** using N64Recomp, N64ModernRuntime, RT64 and RecompFrontend.

> This repository does **not** distribute the original ROM or copyrighted game assets. A legally obtained USA ROM is required locally.

## Current status

The project is now a **playable native Linux build**, not just a feasibility experiment.

Validated milestones:

- matching Snowboard Kids USA decompilation build;
- native CPU recompilation;
- audio RSP recompilation;
- N64ModernRuntime boot;
- RT64/Vulkan rendering;
- RecompFrontend + RecompInput;
- keyboard input and audible music/SFX;
- title/menu, Controller Pak warning, character/course selection;
- complete playable race;
- stable `PresentationMode::Console` with the original severe flicker eliminated;
- Options and Mods tabs;
- HD internal-resolution presets through 2160p / 4K-height;
- native savestate R&D through **P2 runtime quiescence PASS**.

Approximate maturity of the current native-port/R&D roadmap: **~94%**. This is not a claim of 94% full-game compatibility.

## Graphics

Snowboard Kids 1 currently requires:

```text
PresentationMode::Console
```

The previous `PresentEarly` default caused severe framebuffer flicker.

Validated HD presets on `feat/graphics-resolution-presets`:

| Preset | Scale | 4:3 render resolution |
|---|---:|---:|
| Original | 1x | 320x240 |
| 480p | 2x | 640x480 |
| 720p-class | 3x | 960x720 |
| 1080p-class | 4.5x | 1440x1080 |
| 1440p-class | 6x | 1920x1440 |
| 2160p / 4K-class | 9x | 2880x2160 |
| Auto | window-dependent integer | preserves 4:3 |

Validated commit:

```text
d6a5f60744e29f2e3fe7b4b36d3933ec55e5d202
feat(graphics): add HD resolution presets and frontend fixes
```

## Mods

The underlying N64ModernRuntime/RecompFrontend mod framework is real and the SBK1 Mods UI now opens correctly.

Current state:

- Mods tab: working;
- stable `mod_game_id = "snowboardkids"`: working;
- runtime mod infrastructure: present;
- SBK1 code-mod template: pending;
- SBK1 exports/hooks/events audit: pending;
- first end-to-end SBK1 code mod: pending;
- texture-pack validation: pending.

Until a real SBK1 mod is built, loaded and verified, the project should describe modding as **infrastructure-ready**, not fully validated.

## Native savestates

Savestates are being developed as a native-runtime feature, not as an RDRAM-only dump.

Current research sequence:

```text
P1    Serializable continuations              PASS
P1.5  Continuation generalization/schema      PASS
P2    Runtime quiescence                      PASS
P3    RT64 semantic graphics export/import    NEXT
P4    Complete in-memory save/load            PENDING
P5    Audio/timing restoration                PENDING
P6    .sbks persistence                       PENDING
P7    F5/F8 quick-save/load UX                PENDING
```

P2 validated:

```text
Idle -> Requested -> ParkGame -> CloseVI -> DrainDevices -> Frozen -> Resume -> Idle
```

Measured P2 coverage included:

- 600 synthetic coordinator cycles;
- 41 runtime-kernel integration cycles;
- 660 live RT64/Vulkan gameplay freeze/resume cycles;
- 0 timeouts;
- 0 deadlocks;
- 0 failed Frozen-state audits.

The stronger GPU drain exposed a Plume/Vulkan fence lifecycle issue. The working approach uses fresh queue markers with private fences rather than waiting twice on Plume-owned worker fences or using a global `vkDeviceWaitIdle`.

The next major risk is **P3: semantic RT64 state export/import**.

## Remaining major work

- P3 renderer state export/import;
- full in-memory savestate restoration;
- `.sbks` persistence and F5/F8 UX;
- Controller Pak / original save-flow compatibility;
- clean runtime shutdown;
- physical modern-controller validation/remapping;
- broader full-game regression;
- 16:9 widescreen;
- SBK1 code-mod proof;
- packaging/release workflow.

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

## Documentation

- [Current status](docs/STATUS.md)
- [Roadmap](docs/ROADMAP.md)
- [M1 feasibility](docs/M1-FEASIBILITY.md)

Savestate architecture/prototype documents currently live on the savestate feature branch until that work is merged.

## Disclaimer

This is an unofficial preservation, interoperability and reverse-engineering research project. Snowboard Kids and related trademarks/assets belong to their respective rights holders.
