# Roadmap

Updated: 2026-09-23

Compatibility work and optional PC enhancements remain separable so original behavior can always be regression-tested.

## M0 — Reproducible matching decomp

Status: **Complete**

- [x] Normalize and verify legal USA ROM dump
- [x] Build upstream toolchain
- [x] Extract code/data/assets
- [x] Rebuild matching ROM
- [x] Verify byte-identical output
- [x] Record hashes and revisions

## M1 — Native recomp feasibility

Status: **Complete**

- [x] Process matching ELF with N64Recomp
- [x] Generate native CPU translation
- [x] Translate audio RSP microcode
- [x] Validate indirect RSP targets
- [x] Resolve native compile/link blockers
- [x] Build CPU/RSP host libraries

## M2 — Native runtime boot

Status: **Complete**

- [x] Integrate N64ModernRuntime
- [x] Register/validate Snowboard Kids ROM
- [x] Build graphical native executable
- [x] Reach generated game entrypoint
- [x] Apply reproducible runtime compatibility fixes
- [x] Reach stable startup

## M3 — Renderer / audio / frontend

Status: **Complete for current baseline**

- [x] RT64/Vulkan rendering
- [x] RecompFrontend/RecompInput
- [x] Audible SDL audio
- [x] Keyboard input
- [x] Options UI
- [x] Mods UI
- [x] Eliminate screen flicker with `PresentationMode::Console`

## M4 — Menu and progression

Status: **Complete for first-race path**

- [x] Title/menu
- [x] Controller Pak warning flow
- [x] Character/player selection
- [x] Course/race loading
- [x] Gameplay input

## M5 — First playable race

Status: **Complete**

- [x] Player movement
- [x] Camera
- [x] Course geometry
- [x] Collision
- [x] HUD
- [x] Music/SFX
- [x] Complete a full race

Explicit regression coverage still pending:

- [ ] Items
- [ ] AI behavior matrix
- [ ] Multiple courses/characters

## M6 — Full-game compatibility

Status: **In progress**

- [ ] All courses
- [ ] All characters
- [ ] Multiplayer
- [ ] Training
- [ ] Replays
- [ ] Controller Pak / original save flows
- [ ] Physical rumble/controller validation
- [ ] Ending/credits
- [ ] Clean runtime shutdown
- [ ] Full regression matrix

## M7 — PC-port enhancements

### Graphics

- [x] Original 4:3
- [x] HD internal-resolution presets
- [x] 1080p-class
- [x] 1440p-class
- [x] 2160p / 4K-height
- [x] Auto integer scaling
- [ ] 16:9 widescreen without stretching
- [ ] HD texture-pack validation
- [ ] Higher-framerate research where timing permits

### Controls / UX

- [x] Keyboard support
- [x] RecompFrontend controls infrastructure
- [ ] Physical Xbox/SDL controller validation
- [ ] Remapping UX polish
- [ ] Controller-label presets
- [ ] Quick restart
- [ ] Fast-forward
- [ ] Screenshots
- [ ] Packaging/release UX

### Mods

- [x] Mods tab
- [x] Stable SBK1 `mod_game_id`
- [x] Runtime mod infrastructure present
- [ ] Audit/enable SBK1 exports, hooks and events
- [ ] Build first end-to-end SBK1 code mod
- [ ] Publish SBK1 mod template
- [ ] Validate texture packs
- [ ] Document mod author workflow

## M8 — Native savestates / QoL

Status: **R&D in progress**

The v1 target is faithful restoration under the existing live runtime behavior. Input-only deterministic execution is a separate future mode.

- [x] Architecture design
- [x] P1 — serializable continuation feasibility
- [x] P1.5 — continuation schema/generalization validation
- [x] P2 — coordinated runtime quiescence
- [ ] Reconcile P2 GPU participant/fence-count documentation
- [ ] P3 — RT64 semantic renderer export/import
- [ ] P4 — complete in-memory save/load
- [ ] P5 — audio/timing restoration fidelity
- [ ] P6 — versioned `.sbks` persistence
- [ ] P7 — F5/F8 quick-save/load UX
- [ ] Multiple slots
- [ ] Optional thumbnails/metadata
- [ ] Savestate + mods compatibility strategy

### P2 validation

```text
Idle -> Requested -> ParkGame -> CloseVI -> DrainDevices -> Frozen -> Resume -> Idle
```

Measured validation:

- 600 synthetic coordinator cycles;
- 41 runtime-kernel cycles;
- 660 live RT64/Vulkan gameplay cycles;
- 0 timeouts;
- 0 deadlocks;
- 0 failed Frozen-state audits.

GPU quiescence uses fresh queue markers with private fences rather than re-waiting Plume-owned worker fences or using a global device-idle wait.

## Current priority

1. P3 — RT64 semantic graphics export/import
2. Publish/merge validated P2 implementation and reports
3. Full in-memory savestate restoration
4. Controller Pak/original save compatibility
5. Clean runtime shutdown
6. Modern-controller validation/remapping
7. Wider full-game regression
8. Widescreen 16:9
9. End-to-end SBK1 mod proof
10. Packaging/release

## Branch-qualified work

Some validated work is still on feature branches and is not necessarily merged into `main` yet.

- `feat/graphics-resolution-presets`: HD/4K presets, aspect fixes, Mods initialization.
- `feat/savestate-architecture`: savestate architecture and continuation/quiescence R&D.

Documentation should distinguish runtime validation from merge status.
