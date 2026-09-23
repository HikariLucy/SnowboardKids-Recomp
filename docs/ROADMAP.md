# Roadmap

This roadmap is intentionally milestone-driven. Dates are estimates only; technical blockers take priority over calendar targets.

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
