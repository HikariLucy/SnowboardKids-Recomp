# P4-A Production Continuations Implementation Plan

**P4-A STATUS:**
**CODE-SIDE COMPLETE**
**FINAL MANUAL RACE_FINISH GATE PENDING**

**DO NOT START P4-B CAPTURE/RESTORE UNTIL P4-A FINAL GATE PASSES.**

Updated 2026-09-24 on `feat/savestate-architecture`. This status supersedes the
original 2026-09-23 implementation checklist and its task-1-only execution record.
Do not interpret the old unchecked implementation tasks as current pending work.
The checklist below records confirmed completion at the implemented task level;
it does not claim new runs of every test proposed in the original plan.
P4-A overall acceptance remains **PENDING**, not PASS.

**Goal:** Boot the real `SnowboardKidsRecompiled` executable, navigate menus and
complete a race through explicit serializable continuations before implementing
snapshots. Only the final manual full-race acceptance observation remains.

**Architecture:** Cooperative scheduler and P2 barriers, host-owned execution
registry, explicit frames and resumable operations. Zero native suspendable
fallbacks observed in the confirmed real-game validation.

**Spec:** [P4 in-memory savestate architecture](../../P4-IN-MEMORY-SAVESTATE.md).
**Current evidence:** [P4-A production validation](../../P4-A-PRODUCTION-VALIDATION.md).

## Global constraints

- No snapshot capture, restore/load, rollback or snapshot triggers before the
  final P4-A gate passes.
- Registry identity is guest OSThread address plus lifetime generation; host
  operation generations never rewind.
- Guest native context slots are transient caches, never authority or snapshot
  identity.
- Startup execution context is permanently retired.
- No native-call fallback is permitted on suspendable paths, including HLE and
  startup; unsupported paths must fail explicitly.
- Preserve current graphics settings and the Original aspect-ratio default.
- Generated proprietary output stays ignored. Dependency changes remain
  reproducible patches against pinned revisions.
- Preserve the existing dirty working tree; no cleanup or reimplementation is
  authorized by this historical plan.

## Task 1: Host-owned execution registry — complete

- [x] Implement the host-owned, lifetime-checked execution-owner registry.
- [x] Validate owner-registry behavior with the production continuation tests.

The registry uses guest thread address and lifetime together. Stale ownership
must never resolve to or retire a replacement owner at the same address.

## Task 2: Production generator and explicit dispatch — complete

- [x] Implement the production continuation backend and explicit dispatch.
- [x] Generate the production corpus: 1,981 functions and 56 HLE.
- [x] Pass production continuation flow tests.
- [x] Build the production executable from scratch with `SBK_CONTINUATIONS=ON`.

The generated corpus and clean build are complete code-side evidence; neither
substitutes for the final manual race-completion gate.

## Task 3: Registry-backed runtime and HLE integration — complete

- [x] Integrate the host-owned execution registry into the active production
  continuation backend.
- [x] Permanently retire the startup execution context.
- [x] Pass production ownership tests.
- [x] Observe zero native suspendable fallbacks in real-game validation.
- [x] Validate fresh patch application, idempotent second application,
  partial-state detection and wrong-pin detection.

Pinned runtime: `6ccb2e7c2e7f6708257b461097e0aaf03c445e2a`.
Pinned recompiler: `ffb39cdad1da5de07eaaa48bd1db4a89a7986771`.
Canonical runtime patch order: **osStopThread → quiescence → continuations**.
RecompFrontend patch order: **resolution → quiescence**.

## Task 4: Real-game evidence — final manual gate pending

- [x] Confirm the production continuation backend active in real-game execution.
- [x] Validate boot and Controller Pak.
- [x] Confirm the initial title demo matches original behavior.
- [x] Validate main menu, character select and course select.
- [x] Reach an interactive race scene and `race_active`.
- [x] Observe more than 208 million continuation dispatches and zero native
  suspendable fallbacks.
- [x] Record confirmed implementation, reproducibility and validation status in
  `docs/P4-A-PRODUCTION-VALIDATION.md`.
- [ ] Complete a full manual interactive race, observe `race_finish`, and confirm
  zero continuation fallbacks through race completion.

That unchecked item is the **only remaining P4-A acceptance requirement**.
Do not claim that `race_finish` or overall P4-A acceptance has passed.

## Confirmed validation record

- Fresh and idempotent patch application: PASS.
- Partial-state and wrong-pin detection: PASS.
- Clean build from scratch with `SBK_CONTINUATIONS=ON`: PASS, first attempt,
  no previous objects reused.
- Production continuation tests (`run.py`, `ownership_run.py`, `run_flow.py`): PASS.
- Auditor tests: 5 PASS. Schema tests: 6 PASS.
- Clean-build `git diff --check`: PASS.

The successful build is `/tmp/sbk-p4a-resolution-clean-cf4othzs/build`.
No executable, ROM or live test was run as part of that clean-build validation.
The earlier real-game observations are separate evidence.

## P4-B handoff — blocked

P4-A code-side work is complete. Await the final manual `race_finish` observation;
do not resume implementation tasks from the original plan. Only after the final
gate passes may P4-B capture/restore work begin under the approved architecture.
No snapshot implementation or P4 restorability acceptance is claimed.

## Historical note

The original 2026-09-23 record stopped after task 1 and stated that tasks 2–4 had
not been implemented and no real-game continuation run had occurred. Those
statements described that earlier checkpoint and are superseded by the confirmed
2026-09-24 status above. They must not drive new implementation work.
