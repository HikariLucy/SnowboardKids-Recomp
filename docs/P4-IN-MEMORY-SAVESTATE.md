# P4 — complete in-memory savestate

**P4-A: PASS** (final manual race gate passed 2026-09-24).
**P4-B CAPTURE: CODE-SIDE PASS. P4-C RESTORE: CODE-SIDE PASS.**
**LIVE RESTORE GATE: PENDING HUMAN VALIDATION.**

Implementation, measurements and limitations are recorded in
[P4-B/P4-C validation](P4-BC-SAVESTATE-VALIDATION.md). This document keeps the
approved architecture; where it describes P4-B as blocked or not implemented,
that text is historical (written before the P4-A gate passed).

## Objective and scope

At a P2 Frozen boundary, capture an owned snapshot, resume and independently
advance gameplay, freeze again, transactionally restore the snapshot, present
its P3 framebuffer without advancing guest time, and resume saved continuations.
The same immutable snapshot must support repeated loads.

This phase includes full-game continuation integration, runtime semantic state,
host machinery reconstruction, rollback, development triggers, and validation.
It excludes `.sbks`, compression, filesystem persistence, F5/F8, slots, thumbnails,
and dirty-page optimization. Diagnostic reports are not snapshot persistence.
Current user graphics settings survive restoration. New configurations retain
`AspectRatio::Original`.

## Current repository status

P1 and P1.5 remain historical prototype and schema evidence. Their limitations
do not describe the completed P4-A production backend:

- The active production continuation backend covers 1,981 generated functions
  and 56 HLE with a host-owned, lifetime-checked execution-owner registry.
- The startup execution context has permanently retired. Zero native
  suspendable fallbacks were observed in confirmed real-game validation.
- Boot, Controller Pak, main menu, character select, course select, interactive
  race scene and `race_active` passed. The initial title demo was confirmed as
  original behavior. More than 208 million continuation dispatches were observed.
- `race_finish` remains pending manual validation; it is not implied by
  `race_active`, dispatch counts or compilation success.
- Fresh apply, idempotent second apply, partial-state detection, wrong-pin
  detection and a clean build from scratch with `SBK_CONTINUATIONS=ON` passed.
  Production continuation tests, five auditor tests and six schema tests passed.
- `recompfrontend-resolution.patch` owns the frontend HD presets, their
  `recompui/resolution.h` header and the Original aspect-ratio default. It is
  applied before `recompfrontend-quiescence.patch`; current graphics behavior
  and settings are preserved.

The runtime pin is `6ccb2e7c2e7f6708257b461097e0aaf03c445e2a`; the recompiler pin
is `ffb39cdad1da5de07eaaa48bd1db4a89a7986771`. Canonical runtime patch order is
**osStopThread → quiescence → continuations**. RecompFrontend order is
**resolution → quiescence**.

These results establish code-side completion, not full snapshot restorability.
The memory, runtime reconstruction, semantic audio backlog and transactional
restore requirements below remain P4-B work blocked by the final P4-A gate.

## Chosen architecture

P4-A has integrated the explicit-frame production backend and host-owned
execution registry. Preserve that implementation, scheduler semantics and P2
device barriers when P4-B becomes eligible. Use named continuation and
blocked-operation records for reconstruction. Adapt P2 owner retirement/release so workers from an
old timeline cannot reenter after a load. Do not replace this with native stack
cloning or restart thread entrypoints.

Keep the snapshot service project-owned, with semantic adapters in pinned,
reproducible dependency patches. Adapters export into owned values, validate
before mutation, install under Frozen, and hash named fields. Runtime and
renderer object representations never become snapshot data.

An alternative single-host-thread scheduler would remove some reconstruction
work but substantially change the runtime's execution model. Extending the
current scheduler with explicit ownership is preferred to limit that change.

## P4-A production continuation backend gate

P4-A is a hard prerequisite to any snapshot restore/load implementation. The
real `SnowboardKidsRecompiled` production executable must boot, navigate menus,
and complete a race using explicit serializable continuations. Isolated proofs,
corpus inventory, compilation, P2 freezes and P3 roundtrips do not pass this gate.
No native-call fallback may exist on suspendable paths, including generated,
indirect, overlay, startup and HLE paths. Unsupported paths must fail explicitly.

The production backend, code-side implementation and reproducibility validation
are complete. The only remaining P4-A acceptance requirement is a manual
interactive full race: observe `race_finish` and confirm zero continuation
fallbacks through race completion. The boot-to-`race_active` evidence is already
confirmed and must not be treated as unimplemented work.

**DO NOT START P4-B CAPTURE/RESTORE UNTIL P4-A FINAL GATE PASSES.** Do not implement
snapshot capture, restore/load, rollback or snapshot development triggers before
that final manual acceptance. Do not mark P4-A fully PASS yet.

## Authoritative execution ownership and startup

A host-owned side table keyed by guest `OSThread` address **and lifetime
generation** is the authoritative execution-owner/context registry. Address
reuse creates a new lifetime; an obsolete owner cannot resolve to the new thread.
The registry owns contexts, explicit frames, blocked-operation state and native
worker associations. Host operation generations remain distinct from saved
logical thread lifetimes and never rewind on load.

Any native context field retained in guest memory is reconstructable/transient,
never snapshot identity or an ownership authority. Explicitly register and
normalize these slots, including retired slots, then rebind them from the side
table. Never discover native pointers by scanning guest values.

P4 snapshots are accepted only after the boot/startup execution context has
permanently retired (and cannot reenter), or has become part of the serializable
registry with all its live frames and blocked operations represented. A boot
complete flag alone is insufficient; no unregistered startup native stack may
survive capture.

## Snapshot components

| Domain | Owned snapshot data | Reconstruction |
| --- | --- | --- |
| Memory | All 512 MiB of validated mapped logical memory, including zero pages, heap and guest stacks | Replace the complete extent; pages written only after capture become zero again |
| CPU/continuations | Named GPRs/FPR bits, HI/LO, status/FR, rounding, function/section IDs, continuation IDs and live generated locals | Rebind context pointers, dispatch tables and FP mode from compatible build metadata |
| Thread registry | Guest address, lifetime, entry/argument, stopped/started status, blocked HLE phase and pending return result | Fresh execution owners and synchronization objects; no entrypoint replay |
| Scheduler | Current/selected owner, run order and equal-priority order, queue membership and scheduling phase | Rebuild owner associations without invoking guest scheduling side effects |
| Messages/events | Guest queues in memory; registrations, ordered pending external events, identity/admission counters and retry/jam state | Rebuild host inbox in saved order, without delivering it during import |
| Time | Frozen logical time, osSetTime offset, active timer membership, relative signed deadlines and periodic reload state | Rebase wall time; rebuild timer set without calling osSetTimer or issuing expiry events |
| VI | Current/next state, both mode/register banks, retrace/field counters, next logical deadline and closed transaction phase | Rebind mode references, restart at the next complete VI transaction |
| RSP | Audio DMEM, microcode/task identity and required completed-task state | Reconstruct worker state; accepted task queues must be empty at capture |
| Audio | Source rate, logical PCM segments, submission order, consumed offsets, conversion boundary/history and feedback state | Rebuild converter for current device, clear future backlog and queue reconstructed backlog once |
| Input/devices | Channel count, pending observation results, rumble intent and supported device state | Rebind current devices, stop obsolete rumble, preserve unrelated frontend settings |
| Overlays/heap | Loaded section identities/addresses, heap offset and required semantic globals | Rebuild function lookup from stable IDs; bind saved arena without reinitializing it |
| Renderer | P3 semantic state, graphics DMEM/IMEM/registers and GPU-authoritative color/depth planes | P3 reset/import/present; reconstruct caches and GPU targets under current settings |

Use a dense memory baseline initially. Count nonzero pages while inspecting the
full extent, but do not confuse nonzero pages with accessed/active pages. Report
mapped, resident (where measurable), and nonzero pages separately; historical
write activity is not measurable without additional instrumentation.

Use the host-owned registry above for all thread ownership. No native function
pointers, stack addresses, threads, locks, semaphores, SDL handles, Vulkan objects
or RT64 objects are captured.

## Capture and transactional restore

After the P4-A gate passes, capture is accepted only at a verified Frozen
generation with startup permanently retired or registered as specified above,
all required adapters available and all device work drained. Seal event
admission while assembling the consistent snapshot; P2 currently permits host
inbox additions during Frozen, so Frozen alone is insufficient for that domain.
Publish a replacement snapshot only after all domains and canonical hashes have
been captured successfully. Failure leaves any prior snapshot intact.

Restore follows this sequence:

1. Validate snapshot/build identity, continuations, all domain shapes and
   cross-domain references before changing live state. Preallocate staging.
2. Reach Frozen, seal admissions, and capture the current timeline into a
   separate rollback snapshot. If this fails, leave the current timeline intact.
3. Retire obsolete execution owners cooperatively and join them before memory
   replacement. Cleanup must not touch the restored guest structures or emit
   events. Generation checks reject stale completions.
4. Install memory, overlay/heap bindings, contexts/frames, scheduler, messages,
   clock/timers, VI, RSP, audio and input semantic state. Reconstruct transient
   machinery, keeping all producers and execution owners parked.
5. Reset/import P3 and GPU planes, then compare all canonical domain hashes.
   Present the restored framebuffer without a guest step or synthetic VI/SP/DP/
   AI event. Verify GPU color/depth hashes and unchanged logical time/memory.
6. Commit only after reconstruction, comparisons and presentation succeed.
   Release the restored generation, then discard rollback storage when safe.

On any mutation-stage failure, reconstruct the rollback timeline under the same
closed barrier and validate it before release. If rollback or presentation
cannot be recovered, remain Frozen with both failure diagnostics. A cleanup
scope guard must not unconditionally resume. Retry/exit must remain reachable
through the existing frontend event pump.

Keep logical event identities separate from host operation generations. The
former are saved and hashed; the latter stay monotonic across repeated loads.
Maintain a transaction-deferred external-arrival queue separate from the saved
logical inbox. Sealing admission and classifying each arrival must be atomic
with respect to producers: events arriving after sealing never enter the captured
snapshot or its hashes. Preserve arrival order and host generation/provenance in
this transaction-owned queue; do not silently drop arrivals or reopen admission
between domain captures.

On save, release deferred arrivals into the continuing timeline only after
resume. On load, reject stale old-generation operation completions; admit only
still-relevant live observations after the restored generation is released.
Relevance must be checked against the restored device/request state, rather than
relabeling an old completion as new. On rollback, apply the same release and
provenance checks against the reconstructed rollback timeline. Remain sealed
while Frozen after an unrecoverable failure.

## Development API and validation

Expose opt-in capture/load requests and completion diagnostics from the existing
frontend polling path. Keep one operation in flight, reject requests while busy,
and leave SDL events with their current consumer. No final hotkeys or slot UI.
Use an in-process driver for timed navigation, repeated loads and fault injection.

Canonical comparison covers named scalar fields and memory in a fixed order,
with explicit integer widths and floating-point bit patterns. Normalize only
declared reconstructable pointers, wall-clock anchors and operation generations.
Never hash container storage, padding or native addresses. Compare individual
domains before the aggregate so failures identify the missing state.

Future P4-B snapshot validation sequence (not pending P4-A implementation):

1. Cover the actual generated corpus, including direct/indirect/tail calls,
   jump tables, delay slots and live locals. No native-call fallback may hide a
   suspendable path. Compare execution against the ordinary generated backend.
2. Exercise actual runtime stopped/blocked/not-started threads, stop/start of a
   blocked thread, full send/receive queues, retries and pending timer expiry.
   Prove exactly-once side effects across reconstructed owners.
3. Test writes after capture in initially zero pages and extended runtime memory.
   Validate the whole memory extent after load, not only the first 8 MiB.
4. Inject failure before mutation, after each domain install, during renderer
   import/presentation and during rollback. Verify successful rollback hashes or
   a safely Frozen failure; never release partially imported state.
5. Demonstrate menu navigation and character/course selection undo, then an
   active race after at least 20 seconds of independent advancement. Compare
   position, velocity, race timer, HUD and item state and continue playing.
6. Repeatedly load one immutable snapshot after separate advancements. Run at
   least 100 save/advance/load cycles with immediate guest/runtime/GPU hashes,
   event-accounting checks, timeouts, audio observations and visual inspection.
   Previous P2/P3 cycles do not count toward these P4 results.

Preserve current graphics configuration in every test, including when it changed
since capture. Unsupported renderer/configuration combinations must reject before
mutation rather than silently replacing user settings. Test a fresh Original
configuration separately from an existing Expand configuration.

## Measurements and current results

| Metric/result | Current evidence |
| --- | --- |
| Logical memory extent | Design baseline: 536,870,912 bytes; not a measured snapshot |
| Snapshot payload size | Not measured; no complete capture implementation |
| Nonzero/active/resident pages | Not measured |
| Capture latency | Not measured |
| Restore latency | Not measured |
| Peak temporary RAM / process RSS | Not measured |
| Rollback overhead | Dense memory alone adds 512 MiB, plus semantic/GPU/audio data; estimate, not measurement |
| Immediate canonical state/hash comparison | Not run for P4 |
| P3 color/depth comparison after independent advancement | Not run for P4 |
| Snapshot undo in menus, selection, race and repeated loads | Not run for P4-B; P4-A gameplay through `race_active` passed |
| 100-cycle P4 stress | Not run |
| Repository continuation auditor tests | 5 PASS, previously confirmed |
| Continuation schema tests | 6 PASS, previously confirmed |
| P4-A production tests and clean build | PASS; code-side complete |
| P4-A final manual `race_finish` gate | PENDING; only remaining P4-A acceptance requirement |

Measure freeze acquisition separately from capture and restore work; report
median, p95 and maximum. Track live snapshot, rollback, staging and adapter scratch
allocations separately, plus process RSS high-water mark and GPU staging where
available. Two dense snapshots alone require 1 GiB beyond the live memory image.
Avoid accidental additional full-memory copies. Do not introduce dirty tracking
unless measured memory/latency makes the baseline infeasible.

## P4/P5 audio boundary

P4 restores the semantic PCM backlog, source rate, submission order, consumed
offsets and conversion history/boundary state sufficiently that loading rewinds
audio to the saved timeline. Clear future software audio and converter state;
reconstruct and queue the saved backlog exactly once. Repeated loads must not
leak future audio, duplicate submissions, accumulate conversion errors or cause
progressive corruption. Audio validation must exercise source-rate changes,
partially consumed segments and repeated advancement/load cycles.

P5 covers timing/fidelity refinement, already-submitted hardware tail, drift and
deterministic observation concerns. These exclusions do not permit missing PCM
or conversion state, future software backlog leakage or progressive corruption
in P4. Live input/timing observations do not establish input-only deterministic
replay.

## Implementation ownership and review status

P4-A code-side work is complete. The updated implementation plan and
[P4-A validation report](P4-A-PRODUCTION-VALIDATION.md) supersede the original
2026-09-23 task-1-only status. Do not restart completed generator, execution-owner,
runtime or HLE work based on the old checklist.

The approved snapshot architecture remains future P4-B scope. Preserve all
existing working-tree changes and the generated-artifact policy. Auditor and
schema tests do not establish full-game restorability. Overall P4-A acceptance
remains pending only the manual full race, `race_finish` observation and zero
continuation fallbacks. No P4 snapshot acceptance is claimed.

**DO NOT START P4-B CAPTURE/RESTORE UNTIL P4-A FINAL GATE PASSES.**
