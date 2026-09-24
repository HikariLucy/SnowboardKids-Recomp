# P4 — complete in-memory savestate

Status: proposed integration design; **not implemented or demonstrated**.
Inspection date: 2026-09-23. No commits made.

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

## Repository findings

The passing earlier experiments remain useful evidence, but are not yet a
complete execution backend for the game:

- `docs/P1-SERIALIZABLE-CONTINUATION.md` explicitly limits P1 to an isolated
  three-frame proof. `tests/continuation/generate.cpp` rejects indirect/lookup
  calls and jump-table generation.
- `tests/continuation_design/README.md` says the P1.5 schema and flow checks do
  not implement full-game continuation support. The corpus inventory reports
  1,981 generated functions and jump-table scratch locals.
- `librecomp/src/recomp.cpp::run_thread_function` in the pinned runtime still
  creates a stack-local `recomp_context` and invokes a native generated function.
  The application CMake target still compiles the ordinary `RecompiledFuncs`.
- P2 parks existing native execution owners and preserves their stacks. Its
  8 MiB audit and renderer roundtrip cannot substitute for a full snapshot.
- `src/main/native_boot.cpp` converts and queues audio without an owned semantic
  backlog that can reconstruct audio after advancing and loading.
- `patches/recompfrontend-quiescence.patch` already changes the initial aspect
  ratio from Expand to Original. Preserve that change and existing settings.

Therefore an implementation that copies memory and imports P3 while releasing
the old native stacks would be incorrect. No such path is proposed.

## Chosen architecture

Extend the existing explicit-frame generator and integrate an owned execution
registry with the runtime. Preserve scheduler semantics and P2 device barriers;
replace reliance on suspended generated/HLE stacks with named continuation and
blocked-operation records. Adapt P2 owner retirement/release so workers from an
old timeline cannot reenter after a load. Do not replace this with native stack
cloning or restart thread entrypoints.

Keep the snapshot service project-owned, with semantic adapters in pinned,
reproducible dependency patches. Adapters export into owned values, validate
before mutation, install under Frozen, and hash named fields. Runtime and
renderer object representations never become snapshot data.

An alternative single-host-thread scheduler would remove some reconstruction
work but substantially change the runtime's execution model. Extending the
current scheduler with explicit ownership is preferred to limit that change.

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

Move host thread ownership out of guest memory, or sanitize only explicitly
registered native-pointer slots while preserving layout and rebind those slots
after installation. Track retired slots too. Never infer pointers by scanning
for address-like values. No native function pointers, stack addresses, threads,
locks, semaphores, SDL handles, Vulkan objects or RT64 objects are captured.

## Capture and transactional restore

Capture is accepted only at a verified Frozen generation with boot complete,
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
External arrivals during a transaction cannot be silently mixed into the saved
inbox. Reject stale device completions and defer new live input until the first
post-release observation.

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

Required validation sequence:

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
| Menu, selection, race and repeated-load demonstrations | Not run |
| 100-cycle P4 stress | Not run |
| Repository continuation auditor tests | 5 passed during this inspection |

Measure freeze acquisition separately from capture and restore work; report
median, p95 and maximum. Track live snapshot, rollback, staging and adapter scratch
allocations separately, plus process RSS high-water mark and GPU staging where
available. Two dense snapshots alone require 1 GiB beyond the live memory image.
Avoid accidental additional full-memory copies. Do not introduce dirty tracking
unless measured memory/latency makes the baseline infeasible.

P5 audio/timing discrepancies are not yet characterized. Existing design limits
include the device's already-submitted audio tail and live external timing/input
observations. P4 must reconstruct the semantic audio backlog; it cannot defer
missing audio state to P5 or claim input-only deterministic replay from live runs.

## Implementation ownership and review status

Planned changes belong in the explicit-frame generator integration, project-owned
runtime/frontend/RT64 patches where necessary, `src/quiescence`, a focused
in-memory snapshot service, `src/main/native_boot.cpp`, build/patch application
scripts, and runtime/live validation tests. Regenerated proprietary game output
must follow the repository's existing generated-artifact policy.

Changes made in this continuation: this document only. Existing working-tree
changes predate this inspection and have not been rewritten. The five auditor
tests validate inventory logic, not full-game restorability.

This written architectural design awaits review before a detailed implementation
plan and code integration. No P4 acceptance claim is made.
