# P2 — runtime quiescence feasibility prototype

> **Historical baseline (2026-09-23).** P2 phase report, kept as written. Later phases (P3-P7) built savestates on this barrier; current status is in `docs/P6-PERSISTENT-SAVESTATE.md` and `docs/P7-SAVESTATE-UX.md`.

Date: 2026-09-23. Scope: **in-process cooperative freeze/resume**, not a
savestate implementation. There is no `.sbks` format, snapshot file, renderer
serialization, restored memory, or F5/F8 binding.

## Result and acceptance boundary

The prototype implements and exercises:

```
Idle -> Requested -> ParkGame -> CloseVI -> DrainDevices -> Frozen -> Resume -> Idle
```

The live game has completed hundreds of repeated freezes with its existing
Console presentation mode. The audit checks that the first 8 MiB of RDRAM,
logical time, and queued SDL audio do not change during Frozen. Actual runtime
scheduler/message/timer tests additionally compare their guest message results
against a run with freezing disabled.

**This is not yet proof of full-game equivalence to a run that never froze.**
The production game observes host audio consumption, live input and asynchronous
completion order. No complete recorded-observation replay or canonical hash of
all runtime/renderer state exists in this phase. The measurements below establish
barrier feasibility and specific invariants, not a portable checkpoint or a
bit-identical future timeline. P2's stronger whole-game equivalence gate remains
open.

## Implementation

Project-owned code:

- `src/quiescence/quiescence.{hpp,cpp}`: coordinator, owner registry, generation
  checks, bounded diagnostic ring, logical clock exclusion, and barrier API.
- `src/quiescence/probe.{hpp,cpp}`: opt-in frontend stress driver and in-memory
  Frozen audit. It never writes the audited bytes back into guest memory.
- `src/main/native_boot.cpp`: frontend polling, SDL audio pause callback,
  RDRAM audit registration, and optional test-controller input.
- `patches/n64modernruntime-quiescence.patch`: runtime integration.
- `patches/recompfrontend-quiescence.patch`: RT64 adapter drain/resume contract.
- `patches/rt64-quiescence.patch`: cooperative RT64 idle-worker parking.
- `scripts/apply-quiescence-patches.py`: pinned, idempotent patch application.
- `tests/quiescence/`: concurrent coordinator stress and real runtime fixtures.

Pinned revisions are N64ModernRuntime
`6ccb2e7c2e7f6708257b461097e0aaf03c445e2a`, RecompFrontend
`e85b912d9df677b04f9358867dd010c8af27ea05`, and RT64
`6a4166b2cfa952d931a08481d1037da995f28b54`. Existing local thread-stop,
presentation, resolution and frontend changes were retained. The new patches
contain only P2 changes; they do not incorporate unrelated local dependency
edits. Their forward/reverse applicability is checked separately.

### Game execution ownership

The boot entrypoint and every native OS-thread execution wrapper register a
unique lifetime token and guest address. Requests are unavailable until boot
has returned. Scheduler semaphore waits mark an owner dormant before waiting;
wake-up checks the barrier before touching guest memory. Safepoints precede
external-message delivery and scheduler priority checks, including idle waits.
No native thread is asynchronously suspended.

A dormant/stopped/blocked thread acknowledges through its existing dormant
registry state. It receives no artificial scheduler signal. An active owner
parks at its next safepoint on the coordinator condition variable. All owners
must be dormant or parked before CloseVI. Native stacks, registers, pending HLE
arguments, queue links, semaphores and scheduler ownership remain in place.
Resume releases the same parked calls. No guest run queue is rebuilt or reordered.
Creation's initialization handshake remains active so osCreateThread cannot
wait on a child whose registration is blocked by the barrier.

Safepoints are cooperative, not universal generated-instruction safepoints.
A guest loop that never reaches one can time out. The probe cancels a request
that takes over ten seconds rather than declaring an unsafe Frozen state.

### VI transaction and logical clock

VI parks outside the complete loop transaction: screen-update submission,
register/state swap, retrace accounting, VI/AI messages, and callback. An
in-flight transaction finishes normally. The next loop cannot start until
release. A request arriving exactly at the boundary either parks there or
closes the already-started transaction; it never cuts the transaction in half.

**Logical time stops only after VI acknowledges closure.** Parking game owners
alone is too early. An initial implementation stopped time before the sleeping
VI completed; that repeated a VI index, overproduced audio notifications, and
ultimately produced a negative guest audio length interpreted as a huge unsigned
host allocation. Live stress found this; debugger stacks located the allocation
in `queue_samples`. Moving the clock cut after VI closure fixed the observed
memory-growth regression. The coordinator test now explicitly checks that time
continues advancing while an in-flight VI is still closing.

The existing runtime clock type and tick conversion are retained. osGetCount,
osGetTime, VI scheduling and timer deadlines use the rebased logical clock.
OSTimer timestamps and osSetTime's offset are not rewritten to compensate for
a pause. Release excludes the closed-boundary/drain/Frozen interval from elapsed
host time. Time spent cooperatively reaching the boundary remains elapsed time.
Frontend polling and probe deadlines use an independent steady host clock.

### Accepted device work and graphics

Counters cover queued **and executing** RSP and graphics work, including VI
screen updates and dummy/config graphics actions. They increment before enqueue
and decrement only after ordinary completion handling. No approximate queue-size
query or cross-producer sentinel is used to infer drain completion.

RSP work runs through its normal completion, including audio DMEM/RDRAM writes
and SP notification. Graphics retains the existing early SP notification before
send_dl and DP/extension completion after send_dl. These events enter the pending
external inbox; they are not force-delivered to parked guest message queues.
Frontend configuration updates are deferred/coalesced until Idle, without
blocking SDL polling. New guest task producers are already parked before drain.

The graphics adapter waits for the submitted RT64 workload/present IDs and both
CPU worker tails. Because an ID can advance before GPU work finishes, it also
parks RT64's idle GPU-work producer and submits private empty queue markers for
workload, present and framebuffer queues, waiting once on each marker's fence.
An initial experiment re-waited RT64 worker fences and timed out: Plume's Vulkan
wait consumes/resets those fences. Fresh marker fences avoid that error without
changing renderer state or issuing guest completions. The idle condition-variable wait releases its mutex. No
coordinator, scheduler, audio or renderer mutex is held by the caller while
waiting for participant acknowledgements or GPU fences. Missing renderer drain
support fails closed: the default adapter returns false and Frozen is unreachable.

The adapter changes no presentation setting; `PresentationMode::Console` stays
the default. Existing development overrides remain available. No framebuffer
readback, cache invalidation, renderer reconstruction or state serialization occurs.
The host compositor/display scanout is outside the barrier; this is not a promise
that physical screen refresh stops.

### Timers, external messages and audio

The timer worker applies accepted add/remove actions and reaches its barrier
only with all timers back in its active set. A selected timer is reinserted before
an interruptible wait slice. There is no selected timer hidden outside the set
at acknowledgement. Empty waits and long deadlines are interruptible through
bounded polling, without adding a fake guest timer or message. Recurring deadlines
retain the runtime's existing reload rule.

External messages use one serialized producer token, providing a total admission
order across worker producers. Records have stable identities, with separately
sequenced queue admissions for retries. Jam, full-queue drop, and requeue behavior
stay in the existing delivery code. Queuing an external observation while Frozen
can extend the host inbox but cannot mutate a guest queue or wake a guest owner.
Delivery resumes at the existing HLE operation, rather than flushing the inbox
as part of Resume.

SDL audio is paused at the closed VI boundary and resumed before game owners are
released. It is never cleared, resampled differently, or replaced. Conversion
history and rate/volume/latency formulas are untouched. SDL2's pause API suspends
callback processing while supplying silence to the device ([SDL2 documentation](https://wiki.libsdl.org/SDL2/SDL_PauseAudioDevice)).
Already-submitted hardware audio can have a physical tail. Live SDL consumption
at entry/exit remains an external timing observation; preserving the queue does
not establish deterministic audio feedback against another live run.

### Generations, responsiveness and diagnostics

Requests return a monotonically increasing operation generation; busy/not-ready
requests return zero. Stale generations cannot resume a barrier. Resume stays
separate from Idle until parked calls have left, preventing a new operation from
stranding an old acknowledgement. Cancellation releases the current generation
without manufacturing Frozen.

The frontend calls poll after its existing `recompinput::handle_events()` call.
It does not wait for game/device acknowledgements and creates no second SDL event
consumer. Keyboard/controller handling and window pumping continue while frozen.

The bounded 32,768-entry trace includes a monotonic sequence, operation generation,
state, participant, operation and detail. It records owner registration/address,
dormant/safepoint acknowledgements and resume, VI transactions/closure, accepted
and completed device work, SP/DP/SI completion, timer actions/expiry/canonical
acknowledgement, external event identity/admission/delivery/retry/drop, audio pause,
renderer idle acknowledgement/GPU fences, and cancellation. It is a diagnostic
ring, not a complete replay log; sufficiently old entries roll off.

## Running the prototype

```sh
bash scripts/apply-runtime-patches.sh
python3 scripts/apply-quiescence-patches.py
python3 tests/quiescence/run.py
```

The native/runtime/renderer build scripts apply the P2 dependency patch set.
CMake rejects an integrated runtime build whose VI hook is missing. No network
or regeneration is required just to run the ROM-free tests against existing
pinned dependencies.

From a directory with frontend assets and an isolated runtime-data directory:

```sh
SBK_P2_CYCLES=600 SBK_P2_START_MS=2000 \
SBK_P2_INTERVAL_MS=200 SBK_P2_HOLD_MS=15 \
/path/to/SnowboardKidsRecompiled /path/to/snowboardkids.z64
```

Unset/zero `SBK_P2_CYCLES` leaves the coordinator disabled. Parameters are test
controls, not product UI. `SBK_P2_CONTROL_FILE` can supply hexadecimal N64 button
masks for controller 1 without taking desktop focus; omit it for normal input.
Only diagnostic text is written to stderr. The transient 8 MiB audit copy is
never persisted or loaded. Reaching the requested cycle count leaves the game
running; the probe deliberately does not call the baseline's unsafe quit path.

## Measurements and remaining limitations

Measured test results from completed validation:

### 1. Coordinator concurrency tests (`SnowboardKidsQuiescenceTest`)
- **Cycles tested**: 600 freeze/resume cycles; 1,800 tasks per device.
- **Scenarios verified**: In-flight-VI clock advancement, cancellation mid-barrier, stale generation rejection, dormant owner handling, clock exclusion/pause, callbacks executing outside locks, exactly-once SP/DP task completions.
- **Sanitizers**: ASan (AddressSanitizer) clean, UBSan (UndefinedBehaviorSanitizer) clean, TSan (ThreadSanitizer) clean (0 data races detected).
- **Timeouts / Deadlocks**: 0.

### 2. Kernel integration tests (`SnowboardKidsQuiescenceKernelTest`)
- **Cycles tested**: 41 freeze/resume cycles with actual patched runtime scheduler, blocked receive, external events, and one-shot timer expiry (holding each freeze 60 ms, longer than the 40 ms guest deadline).
- **Blocked message queues**: Cycle 41 verified sender blocking on full message queue (`blocked_on_send`) across freeze, resuming without data corruption.
- **Scheduler & time**: Guest RAM, `osGetCount`, and `osGetTime` verified bit-for-bit unchanged across each 60 ms freeze.
- **Baseline comparison**: Guest message transcript under quiescence matched the unfrozen `--baseline` run identically (FIFO, drop, retry, and jam semantics preserved).
- **Sanitizers**: ASan / UBSan clean.
- **Timeouts / Deadlocks**: 0.

### 3. Live graphical game tests (`SnowboardKidsRecompiled` with USA ROM)
- **Environment**: Linux x86_64, Vulkan backend (RADV / NVIDIA RTX 3060 Laptop GPU), `PresentationMode::Console`, 48000 Hz SDL audio, 1280x720 / 2160p (4K) pipeline.
- **Cycle counts tested**:
  - Smoke test: 10 cycles, 153 VI transactions, 671 external deliveries.
  - Menu-to-race navigation: 50 cycles, 638 VI transactions, 2,934 external deliveries, audio queued range [0, 5944] bytes.
  - Extended stress test: 600 cycles, 5,721 VI transactions, 28,316 unique external deliveries, audio queued range [0, 11984] bytes.
  - Total live freeze/resume cycles: 660.
- **Coverage**:
  - Menus: repeated freeze/resume during copyright, title screen, game mode select, character select, and board select.
  - Loading/transitions: freeze/resume during track loading and screen mode initialization.
  - Gameplay/race: active 3D racing on Rookie Mountain with camera motion, snowboarder physics, and active audio BGM.
  - Audio: SDL audio paused cleanly at CloseVI, hardware audio queue preserved during Frozen, unpaused on Resume without overflow or starvation.
  - In-flight VI: VI transaction boundary respected without regression of previous screen flicker.
  - Frozen audit: 8 MiB RDRAM, logical clock, and SDL queued audio verified strictly unchanged on all 660 live audits (`all_frozen_audits_unchanged: true`).
- **GPU marker/fence validation (`--require-gpu-fences`)**:
  - Plume's Vulkan fence reset behavior: `VulkanCommandQueue::waitForCommandFence` calls `vkResetFences` immediately upon completion. Re-waiting a worker's existing fence deadlocks on a reset fence.
  - Solution: In `RT64Context::drain_for_quiescence()`, fresh empty command lists and private `RenderCommandFence` objects are submitted to `workloadGraphicsWorker`, `presentGraphicsWorker`, and `framebufferGraphicsWorker` after parking `idleThread`.
  - All four synchronization participants acknowledged on every cycle: one CPU idle-thread park acknowledgement (`renderer-idle park-ack`) plus three GPU queue marker fences (`gpu-workload fence-ack`, `gpu-present fence-ack`, `gpu-framebuffer fence-ack`).
  - No global `vkDeviceWaitIdle` used.
  - Zero hangs, deadlocks, or fence wait timeouts across all 660 live cycles.
- **Shutdown after stress**:
  - Process clean termination verified via `cancel("shutdown")` waking parked calls without leaving hanging threads.

Additional limitations and blockers before P3:

- **State serialization (.sbks)**: P2 only implements cooperative in-process freeze/resume. Snapshot files, file formats, disk persistence, and F5/F8 keybindings are not implemented.
- **Renderer state serialization**: RT64 internal textures, render targets, descriptors, shaders, and presentation state are drained to complete idle, but are not serialized or restored across process boundaries.
- **Memory coverage**: The live audit validates the 8 MiB guest RDRAM, logical clock, and queued audio bytes. Host globals, native thread execution stacks, and 512 MiB virtual address space are not serialized.
- **Runtime teardown**: The pinned runtime's native thread cleanup does not join all detached guest/timer worker threads before unmapping guest RAM on process quit; a complete process restart or savestate load will require clean thread recreation.
- **External observation determinism**: Audio device consumption and physical presentation refresh continue in host time. Frozen execution preserves guest invariants, but bit-identical full-game replay against an unfrozen baseline is an open gate for subsequent phases.
