# Roadmap

Updated: 2026-09-24

SnowboardKids-Recomp is now tracked against the **full product vision**, not only the native-port feasibility roadmap.

**Approximate full-product maturity: ~52%.**

The former ~94% figure represented a much narrower technical roadmap and is retired.

See [Project vision](PROJECT-VISION.md) for the scope behind the current percentage.

## Foundation — Native port

Status: **Strong baseline / largely complete**

- [x] Reproducible matching decompilation baseline
- [x] N64Recomp CPU translation
- [x] Audio RSP translation
- [x] N64ModernRuntime integration
- [x] RT64/Vulkan rendering
- [x] RecompFrontend/RecompInput integration
- [x] Audible music/SFX
- [x] Keyboard input
- [x] Title/menu flow
- [x] Controller Pak warning flow
- [x] Character/course selection
- [x] Full playable race
- [x] Stable Console presentation mode
- [x] HD/4K internal-resolution presets
- [x] Reproducible project-owned dependency patches
- [x] Clean build from pinned dependencies with production continuations enabled

Remaining foundation/stability work:

- [ ] Clean runtime shutdown
- [ ] Broader game-wide regression matrix
- [ ] Long-session stability across all modes

## Phase A — Native savestates

Status: **In progress**

The v1 target is faithful restoration under the existing live runtime model. Deterministic replay is a separate future problem.

### P1 — Serializable continuation feasibility

Status: **PASS**

- [x] Fresh-process restoration
- [x] Guest registers/RDRAM equivalence
- [x] Exactly-once resume
- [x] Live locals/stable continuation IDs
- [x] FR0/FR1 coverage

### P1.5 — Continuation generalization/schema

Status: **PASS**

- [x] Schema/auditor tests
- [x] Fresh-process restore
- [x] Sanitizer flow validation
- [x] Cross-binary snapshot exchange

### P2 — Runtime quiescence

Status: **PASS**

- [x] Idle -> Requested -> ParkGame -> CloseVI -> DrainDevices -> Frozen -> Resume -> Idle
- [x] 600 synthetic coordinator cycles
- [x] 41 runtime-kernel cycles
- [x] 660 live RT64/Vulkan cycles
- [x] Zero timeouts/deadlocks/Frozen-state audit failures
- [x] Participant accounting resolved: 3 GPU queues + 1 CPU renderer worker

### P3 — RT64 semantic renderer export/import

Status: **PASS**

- [x] Semantic graphics-state export/import
- [x] Reconstruction of transient renderer machinery
- [x] No raw SDL/Vulkan/RT64 object serialization
- [x] Present restored image without advancing guest time

### P3.1 — Framebuffer authority

Status: **PASS**

- [x] Prove active framebuffer can be GPU-authoritative
- [x] GPU color readback/import
- [x] GPU depth readback/import
- [x] Preserve target dimensions/resolution scale
- [x] Prevent stale RDRAM framebuffer copies from clobbering restored GPU state
- [x] Validate Original, Auto and 2160p configurations

### P4-A — Production continuation backend

Status: **CODE-SIDE COMPLETE; FINAL MANUAL GATE PENDING**

Validated:

- [x] Full generated corpus: 1,981 functions
- [x] 56 HLE entries
- [x] Production continuation backend in the real executable
- [x] Host-owned lifetime-checked execution-owner registry
- [x] Startup execution context retirement
- [x] Blocking/scheduler integration tests
- [x] Zero native suspendable fallbacks observed in real-game validation
- [x] Manual boot -> menu -> character select -> course select -> interactive race_active
- [x] >200 million continuation dispatches observed in one session
- [x] Canonical runtime patch order: osStopThread -> quiescence -> continuations
- [x] RecompFrontend patch order: resolution -> quiescence
- [x] Fresh patch application
- [x] Idempotent second application
- [x] Partial-state rejection
- [x] Wrong-upstream/pin rejection
- [x] Clean build from scratch with SBK_CONTINUATIONS=ON
- [x] Production continuation tests
- [x] Auditor/schema tests
- [ ] Complete one manual interactive race and observe race_finish with zero continuation fallbacks

**Do not start P4-B until the final P4-A gate passes.**

### P4-B — Semantic in-memory capture

Status: **BLOCKED ON P4-A**

Planned capture set:

- [ ] Required mapped guest memory
- [ ] Continuations/contexts
- [ ] Thread registry
- [ ] Scheduler runnable/blocked state
- [ ] Message queues and deferred external ordering
- [ ] Logical time/timers
- [ ] VI state
- [ ] RSP/audio semantic state
- [ ] Input observations
- [ ] Overlay/function dispatch state
- [ ] Heap state
- [ ] P3/P3.1 renderer state

### P4-C — Transactional restore

Status: **PENDING**

- [ ] Validate/preallocate before mutation
- [ ] Rollback snapshot
- [ ] Cooperative retirement/join of prior execution owners
- [ ] Install semantic state while producers remain parked
- [ ] Renderer import/present without guest event/time advance
- [ ] Canonical validation hashes
- [ ] Commit/release only after successful restore
- [ ] Roll back or remain safely Frozen on failure

### P4-D — Real gameplay save/load

Status: **PENDING**

- [ ] Capture during menu
- [ ] Capture during active race
- [ ] Advance >=20 seconds
- [ ] Restore position/velocity/timer/HUD/item/audio/render state
- [ ] Repeated loads of the same snapshot

### P4-E — Stress / rollback / fault injection

Status: **PENDING**

- [ ] >=100 capture/restore cycles
- [ ] Blocked/stopped thread cases
- [ ] Full message queues
- [ ] Timer ordering
- [ ] No duplicate/missing SP/DP/VI/AI events
- [ ] Fault-injection rollback
- [ ] Capture/restore latency metrics
- [ ] Payload/RSS metrics

### P5 — Audio/timing restoration fidelity

Status: **PENDING**

- [ ] PCM semantic backlog restoration
- [ ] Source-rate/conversion-history restoration
- [ ] Hardware-tail/timing fidelity
- [ ] Drift/regression validation

### P6 — Versioned .sbks persistence

Status: **PENDING**

- [ ] Versioned file format
- [ ] Compatibility validation
- [ ] Metadata/checksums
- [ ] Safe load failure behavior

### P7 — Quick-save/load UX

Status: **PENDING**

- [ ] F5/F8 UX
- [ ] Multiple slots
- [ ] Optional thumbnails/metadata
- [ ] Savestate + mods compatibility strategy

## Phase B — Full-game compatibility

Status: **Early / incomplete**

- [ ] Validate all characters
- [ ] Validate all courses
- [ ] Validate items
- [ ] AI behavior matrix
- [ ] Training
- [ ] Replays
- [ ] Secondary menus
- [ ] Progression/unlocks
- [ ] Ending/credits
- [ ] Controller Pak
- [ ] Original save behavior
- [ ] Clean shutdown
- [ ] Full regression matrix

## Phase C — Controls and multiplayer

Status: **Partial**

Validated:

- [x] Keyboard
- [x] RecompFrontend/RecompInput path
- [x] Digital Xbox inputs observed: A / D-pad / Start

Pending:

- [ ] Xbox analog gameplay control
- [ ] User-facing remapping
- [ ] Deadzones
- [ ] Rumble
- [ ] Controller prompts/labels
- [ ] DualShock/DualSense validation
- [ ] Switch Pro validation
- [ ] Generic SDL controller matrix
- [ ] Second controller
- [ ] All original local multiplayer player counts

## Phase D — Release engineering

Status: **Early**

- [ ] Linux distributable build
- [ ] Windows build
- [ ] macOS build
- [ ] Automated GitHub Actions
- [ ] PR artifacts
- [ ] Release pipeline
- [ ] Semantic versioning
- [ ] Changelog
- [ ] Checksums
- [ ] No-ROM packaging verification
- [ ] ARM64 feasibility
- [ ] Flatpak feasibility

## Phase E — Mods

Status: **Infrastructure present; end-to-end proof pending**

- [x] Mods tab
- [x] Stable SBK1 mod_game_id
- [x] Runtime mod infrastructure present
- [ ] Audit/enable SBK1 exports
- [ ] Audit/enable hooks/events
- [ ] First real SBK1 code mod
- [ ] Reusable SBK1 mod template
- [ ] Mod author documentation
- [ ] Texture-pack validation
- [ ] Sample mods

## Phase F — PC enhancements

Status: **Partial**

### Graphics

- [x] Original 4:3
- [x] HD internal-resolution presets
- [x] 1080p-class
- [x] 1440p-class
- [x] 2160p / 4K-height
- [x] Auto integer scaling
- [ ] True 16:9 widescreen
- [ ] Ultrawide research
- [ ] HUD/menu adaptation for wider layouts
- [ ] HD texture packs
- [ ] Higher-framerate research without gameplay-timing changes

### QoL

- [ ] Quick restart
- [ ] Fast-forward research
- [ ] Screenshot UX
- [ ] User-friendly diagnostics/system report
- [ ] Installer/updater UX

## Public/open-source readiness

Status: **In progress**

- [x] ROM/assets non-distribution policy
- [x] Pinned dependency revisions
- [x] Reproducible project-owned dependency patching
- [x] Current README/status/roadmap refresh
- [ ] License audit
- [ ] Contribution guide
- [ ] Issue/PR templates
- [ ] Build/release documentation for supported platforms
- [ ] CI required checks
- [ ] Protected main branch/tags
- [ ] Public beta readiness review

## Current priority

1. Finish the final manual P4-A race_finish gate.
2. Start P4-B semantic in-memory capture only after P4-A passes.
3. Complete P4-C/P4-D/P4-E.
4. Finish P5-P7.
5. Expand full-game compatibility coverage.
6. Complete modern controls and local multiplayer.
7. Build release engineering/CI.
8. Prove real SBK1 mod support.
9. Research widescreen and additional PC enhancements.

## Branch-qualified work

Some validated work remains on feature branches and is not necessarily merged into main yet.

- feat/graphics-resolution-presets: HD/4K presets and related frontend work.
- feat/savestate-architecture: savestate architecture, quiescence, renderer-state and production-continuation work.

Documentation must distinguish runtime validation from merge status.
