# P4-A Production Continuations Validation

**P4-A STATUS:**
**CODE-SIDE COMPLETE**
**FINAL MANUAL RACE_FINISH GATE PENDING**

**DO NOT START P4-B CAPTURE/RESTORE UNTIL P4-A FINAL GATE PASSES.**

Status frozen on 2026-09-24 for `feat/savestate-architecture`. This report records
confirmed existing implementation and validation evidence. This documentation
update did not compile, execute the game or ROM, or run live tests. P4-A overall
acceptance is **PENDING**, not PASS.

## Production continuations — complete

- 1,981 generated functions and 56 HLE.
- Production continuation backend active.
- Host-owned, lifetime-checked execution-owner registry.
- Startup execution context permanently retired.
- Zero native suspendable fallbacks observed in real-game validation.

## Confirmed real-game evidence

| Milestone | Result |
| --- | --- |
| Boot | PASS |
| Controller Pak | PASS |
| Initial title demo | Confirmed original behavior |
| Main menu | PASS |
| Character select | PASS |
| Course select | PASS |
| Interactive race scene | PASS |
| `race_active` | PASS |
| Continuation dispatches | More than 208 million observed |
| Native suspendable fallbacks | Zero observed |
| `race_finish` | PENDING manual validation |

The only remaining P4-A acceptance requirement is:

- [ ] Play a full manual interactive race, observe `race_finish`, and confirm
  zero continuation fallbacks through race completion.

Reaching `race_active` and passing the clean build do not establish
`race_finish`. Do not mark P4-A fully PASS before that final observation.

## Reproducibility — complete

| Dependency | Pinned revision |
| --- | --- |
| N64ModernRuntime | `6ccb2e7c2e7f6708257b461097e0aaf03c445e2a` |
| N64Recomp | `ffb39cdad1da5de07eaaa48bd1db4a89a7986771` |
| RecompFrontend | `e85b912d9df677b04f9358867dd010c8af27ea05` |

Canonical runtime patch order:

1. `n64modernruntime-osstopthread.patch`
2. `n64modernruntime-quiescence.patch`
3. `n64modernruntime-continuations.patch`

RecompFrontend patch order:

1. `recompfrontend-resolution.patch`
2. `recompfrontend-quiescence.patch`

`recompfrontend-resolution.patch` owns the existing HD presets and their
`recompui/resolution.h` header, together with the frontend consumers. The
previous clean-build failure came from including that local, untracked header
without creating it in the patch chain. The split reproduces the existing
frontend file contents byte for byte and preserves current graphics behavior.
The project patch mechanism applies resolution before quiescence.

| Validation | Result |
| --- | --- |
| Fresh apply from pinned dependencies | PASS |
| Idempotent second apply | PASS |
| Partial-state detection | PASS |
| Wrong-pin detection | PASS |
| Fresh RecompFrontend apply, header present before compilation | PASS |
| RecompFrontend second apply and `git diff --check` | PASS |
| Clean build from scratch, `SBK_CONTINUATIONS=ON` | PASS |
| Production continuation tests | PASS |
| Auditor tests | 5 PASS |
| Schema tests | 6 PASS |

The successful clean build used
`/tmp/sbk-p4a-resolution-clean-cf4othzs/build` and built
`SnowboardKidsRecompiled` on the first attempt without reusing previous objects.
Generated corpus sources were inputs; compiled objects were rebuilt. The binary
and ROM were not executed, and no live tests ran during this build validation.
Temporary build evidence is at `/tmp/sbk-p4a-resolution-clean-cf4othzs/`, including
`build-attempt-1.log`, bootstrap/patch/configure logs, and the three test logs.
These temporary paths are local evidence, not persistent repository artifacts.

The three production test commands passed after that build:

```text
python3 tests/production_continuation/run.py
python3 tests/production_continuation/ownership_run.py
python3 tests/production_continuation/run_flow.py
```

## Frozen next action

Only the final manual `race_finish` gate remains. No further P4-A implementation
is pending in the old plan. P4-B capture, restore/load, rollback and snapshot
triggers remain blocked until that gate passes. Snapshot implementation and
full-game restorability are not claimed by this report.

See the [architecture](P4-IN-MEMORY-SAVESTATE.md) and the
[updated implementation plan](superpowers/plans/2026-09-23-p4a-production-continuations.md).
