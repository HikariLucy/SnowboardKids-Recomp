# Roadmap

This roadmap is intentionally milestone-driven. Dates are estimates only; technical blockers take priority over calendar targets.


## Finalization and public-release plan

Approximate project status: **~96%**.

The repository remains private while the current playability objectives are being closed. The first public release is **not** intended to wait for every planned enhancement. The publication gate is a reliable, understandable, playable PC port that another user can set up from their own supported ROM without access to the development environment.

### First public playable release — required

The repository may be made public once the current objectives below are closed:

- [ ] Finish the current Controls / Audio / Accessibility live-validation gates
- [ ] Complete a full race through finish/results/return without a progression blocker
- [ ] Validate the original Controller Pak save flow live: create/progress/restart/load
- [ ] Validate the basic physical-controller path on real hardware
- [x] Keep Model D first-run generation working from the user's own supported ROM
- [x] Keep the distributed engine artifact ROM-free and game-module-free
- [x] Keep Linux and Windows ROM-free CI green
- [x] Keep savestates and Controller Pak persistence working cross-process
- [x] Keep graphics, VSync, input configuration, host volume and accessibility configuration persistent
- [ ] Resolve the remaining release/license blockers before distributing binaries
- [ ] Finish public-facing setup/troubleshooting documentation and supported-ROM instructions
- [ ] Produce and audit the first public beta artifact

The first public version should be presented as a **beta / pre-release**, not as a claim that every enhancement in this roadmap is complete.

### Supported ROM policy for the first public release

The initial public build should support the single, explicitly validated Snowboard Kids (USA) corpus used by the project. The builder may accept `.z64`, `.v64` or `.n64` byte-order variants when normalization yields that same supported dump.

Do **not** accept arbitrary ROMs solely because their game ID matches. Additional revisions or regions require their own verified corpus/configuration and compatibility evidence.

### Performance conclusions and optimization plan

Current evidence is positive but is not yet a formal hardware benchmark:

- [x] Guest timing remains at the original 60.000 Hz VI rate in validated live runs
- [x] Repeated race-active and overlay/settings runs complete with zero recompiler fallbacks
- [x] Test harness shutdowns have repeatedly left zero residual game processes
- [x] High internal resolution (including the validated 4.5x case) remains stable on the current Linux/NVIDIA test machine
- [x] The previous audio FIFO bug capable of causing an enormous host allocation was fixed by preserving N64 AI FIFO semantics
- [x] ASan/UBSan-backed suites cover important persistence/input/audio paths
- [ ] Record a formal CPU/RAM/GPU/VRAM/frame-time baseline before declaring 1.0 performance targets

The project must **measure before optimizing**. The lack of a formal CPU/RAM/GPU benchmark does not block the first playable public beta unless a real performance regression is found.

Planned `PERFORMANCE-PROFILING-P1` should measure representative menu/race/overlay scenarios at multiple internal resolutions and record:

- CPU average/peak
- resident memory (RSS)
- GPU utilization and VRAM where available
- CPU/GPU frame time
- presentation time
- audio queue behavior
- long-session memory/thread/file-descriptor stability

### Post-public versions

The following work is intentionally **not required to publish the first playable repository**. It belongs to later releases unless a dependency is discovered during the current closure work:

- True widescreen / ultrawide rather than simple Expand
- High-framerate feasibility and, only if safe, higher-FPS modes
- Mod hooks, mod packaging and public mod templates
- Extended performance optimization after profiling
- Additional Snowboard Kids ROM revisions/regions
- Further accessibility and controller polish
- Additional physical multiplayer validation and convenience features
- Broader release packaging and distribution formats
- Android ARM64/Vulkan investigation and port
- Android/mobile controller and touch-control UX
- PC ↔ Android save portability if the module/corpus compatibility model permits it

### Android direction

Android is a planned future platform, not a blocker for the PC public release. The preferred eventual target is **Android ARM64 + Vulkan**, reusing the Model D engine/game-module ABI where practical. A mobile release will need a dedicated ROM-selection flow, ARM64 module-generation strategy, storage integration and mobile input UX.

PC remains the reference platform until the first public playable release is stable.


## M0 — Reproducible matching decomp

Status: **Complete**

- [x] Obtain and normalize a legal local ROM dump
- [x] Verify expected SHA-1
- [x] Build upstream toolchain
- [x] Extract code/data/assets
- [x] Rebuild ROM
- [x] Verify byte-identical matching output
- [x] Record baseline

Exit criterion:

```text
build/snowboardkids.z64: OK
```

## M1 — Recomp feasibility

Status: **In progress**

- [ ] Pin exact upstream decomp commit
- [ ] Pin exact N64Recomp revision
- [ ] Inspect game entry point and memory layout
- [ ] Determine required symbol/function mapping
- [ ] Identify overlays or relocatable code
- [x] Identify RSP microcode usage
- [ ] Identify libultra dependencies used by the game
- [x] Generate audio RSP translation
- [x] Validate audio RSP indirect jump-table targets
- [ ] Create initial N64Recomp config
- [x] Produce first generated native code
- [x] Document initial unsupported CPU/RSP boundary

Exit criterion:

N64Recomp can process the selected game code and produce a reproducible native build artifact, even if it does not boot yet.

## M2 — Native boot

- [x] Compile generated CPU + RSP as native host libraries
- [x] Create native executable skeleton
- [x] Initialize modern runtime
- [x] Compile RT64 + RecompFrontend stack
- [x] Map ROM/data access
- [ ] Implement required patches/hooks
- [x] Build first graphical native executable
- [x] Reach game entry point
- [ ] Eliminate first startup crashes

Exit criterion:

Native executable starts and executes original game logic consistently.

## M3 — First rendered frame

- [ ] Integrate renderer/runtime graphics path
- [ ] Submit first valid display list
- [ ] Validate framebuffer output
- [ ] Validate timing
- [ ] Validate basic audio initialization

Exit criterion:

A correct Snowboard Kids frame is rendered by the native executable.

## M4 — Menu and input

- [ ] Controller mapping
- [ ] Main menu rendering
- [ ] Character selection
- [ ] Course selection
- [ ] Save/controller-pak behavior strategy

Exit criterion:

A user can navigate from startup into a race using a modern controller.

## M5 — First playable race

- [x] Player movement
- [x] Camera
- [x] Course geometry
- [x] Collision
- [ ] Items
- [ ] AI racers
- [x] HUD
- [x] Music/SFX

Exit criterion:

One race can be started, played, and completed natively.

## M6 — Full-game compatibility

- [ ] All courses
- [ ] All characters
- [ ] Multiplayer modes
- [ ] Training
- [ ] Replays
- [ ] Controller Pak / save flows
- [ ] Rumble
- [ ] Ending/credits
- [ ] Regression test matrix

Exit criterion:

Original game content is completable with no known progression blockers.

## M7 — PC-port enhancements

Only after baseline compatibility.

Possible work:

- Widescreen
- Modern resolutions
- Higher framerate where timing permits
- Modern controller UX
- Keyboard support
- Linux packaging
- Windows packaging
- Mod hooks
- Debug tools

Enhancements must remain separable from compatibility fixes so original behavior can always be tested.


### Current next milestones

- [x] Reach title/menu
- [x] Wire real keyboard/controller input through RecompFrontend
- [x] Validate audible SDL output and sustained audio tasks
- [x] Reach first playable race


- [ ] Diagnose post-menu termination
- [ ] Diagnose visible frame flicker


- [ ] Resolve indirect callback targets needed for race start


### Current stability / UX backlog

- [ ] Capture symbolic backtrace for gameplay crash
- [x] Diagnose and eliminate visible frame flicker
- [ ] Validate Xbox controller path through SDL GameController
- [ ] Expose user-selectable/remappable controls


- [x] RecompFrontend options UI reachable
