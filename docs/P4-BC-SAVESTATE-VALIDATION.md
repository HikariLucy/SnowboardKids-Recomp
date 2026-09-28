# P4-B / P4-C — In-memory savestate capture and transactional restore

**STATUS (2026-09-24):**
**P4-A: PASS**
**P4-B CAPTURE: PASS** (code-side and live)
**P4-C RESTORE: PASS LIVE** (manual validation reported by user; see
[Live gate](#live-gate-manual-validation-reported-by-user))

Scope of this document is the in-memory savestate held by the development
trigger. Persistence (`.sbks`) and the user quicksave/quickload UX are
documented separately in [P6](P6-PERSISTENT-SAVESTATE.md) and
[P7](P7-SAVESTATE-UX.md). No rewind/replay and no dirty-page optimization.

P4-A is PASS (manual race gate passed; see [P4-A validation](P4-A-PRODUCTION-VALIDATION.md)).
SHUTDOWN-01 (below) is an unrelated teardown bug and stays in the backlog.

## Architecture

Capture and restore run only inside the existing P2 barrier. The coordinator
gained two transaction states appended after the P2 values (P2 numbering is
unchanged):

```
Idle -> Requested -> ParkGame -> CloseVI -> DrainDevices -> Frozen
     -> Capture | Restore -> Frozen -> Resume -> Idle
```

`begin_transaction`/`end_transaction` require the matching Frozen generation.
Device admission throws in Capture/Restore exactly as in Frozen. There is no
second freeze mechanism.

The snapshot (`src/savestate/snapshot.hpp`, `InMemorySnapshot`, magic `SBK4`,
version 1) is explicit and host independent: fixed-width guest/logical values
only. Native stacks, return addresses, TLS, `std::thread`, locks, semaphores,
SDL/Vulkan/RT64 objects and host pointers are not representable. Host
machinery is rebuilt on restore.

| Domain | Captured semantic state | Restore |
| --- | --- | --- |
| memory | Full 512 MiB logical extent, zero pages elided (not dirty tracking); `OSThread::context` native-pointer slots normalized to zero | Every page rewritten or zeroed; context slots rebound from the owner registry |
| continuations | Per owner: guest `OSThread` address, logical lifetime, entry/arg, run state (Sleeping/Running), pending scheduler op, started flag, GPR/FPR bits, HI/LO, FCSR/FR, MIPS rounding mode, frames (function/continuation IDs, live locals, scratch), blocked HLE phase/args/result; logical lifetime counter | Old owners retired and joined; fresh host owners/workers with new host lifetimes carrying the saved logical identity |
| scheduler | `running_queue` head, external inbox in delivery order (mq, msg, jam, retry) | Queue head set; inbox replaced |
| time | Logical ns since start, `osSetTime` offset, active timer set (guest `OSTimer`s, canonical order) | Paused logical clock rebased; timer set rebuilt after memory install |
| vi | Both VI mode banks (mode as guest address/dummy), regs, update-screen regs, `total_vis`, retrace countdown, SP/DP/AI/SI registrations | Installed while the VI thread is parked outside a transaction |
| audio | Guest AI frequency; host PCM backlog (not yet consumed), conversion history, backlog format | Frequency/converter rebuilt; SDL queue cleared; backlog queued once |
| input | Explicitly none host-side (guest latches are in RDRAM, SI completions in the inbox) | Nothing to inject; first post-restore read is live input |
| overlays | Loaded sections by stable section-table index + relocation table | Function map rebuilt from the build's section table |
| rsp | 4 KiB DMEM (tasks are drained at Frozen) | Copied back |
| renderer / color / depth | P3 `SBK3` semantic blob incl. P3.1 GPU-authoritative planes | P3 reset/import/present under current resolution/aspect settings |

Canonical hashes (`hash_domain`, `compute_hashes`) exist per domain plus an
aggregate over magic/version/build/present-mask/domain hashes. Integers are fed
as fixed-width little-endian values, floats as bit patterns, sequences with
explicit lengths in a fixed order. Excluded: host addresses, wall clock, host
operation generations (e.g. the P3 blob's quiescence generation), external
event identities, container storage and padding.

### Execution owners without native stack serialization

Each guest thread's native worker blocks only at a few known sites, and after
each one control returns to the `run_execution` loop with the state fully
described by `Execution` plus a semantic *pending scheduler operation*
(`None`, `CheckQueue`, `Pause`):

- **Sleeping** (scheduler semaphore): the in-flight scheduler operation has
  completed once the owner wakes — pending is normalized to `None`.
- **Parked at a safepoint**: the pending op is redone from its beginning. All
  such safepoints precede guest-visible effects. A park inside a message HLE
  (receive/send/jam) replays that HLE from its saved phase; a park inside any
  other native HLE is rejected fail-closed (capture retried at a later freeze).
- **Parked after wake**: the owner already holds the run token; resume at the loop.

The barrier records each owner's site and whether a scheduler signal is in
flight (`owner_signal`); capture waits until owners are settled. Restore:

1. detaches every live owner and signals every stored worker **under the
   registry lock** (no context can be freed concurrently);
2. `retire_owners()` wakes parked/woken owners, which leave through
   `thread_terminated` without touching guest memory;
3. waits until the barrier registry is empty and the runtime cleaner has
   joined and released every old worker;
4. after memory/time/queues/overlays are installed, creates fresh owners
   (`create_restored_owner`) and workers (`spawn_restored_thread`) that wait
   exactly where the saved owner waited (semaphore or restored park), then
   continue the saved frames. Entry points are never replayed for started threads.

Host lifetimes (`OwnerKey`) stay monotonic and are never saved or rewound;
logical lifetimes are snapshot state. Stale `{address, host lifetime}` keys
never resolve after a restore.

### Transactional restore

`SnapshotService::restore`: validate structure, integrity hashes, build
identity and adapter set (no mutation) → capture a rollback snapshot of the
current timeline (also the staging allocation) → retire owners → install
memory, time, scheduler, VI, overlays, RSP, continuations, renderer, audio,
input → re-export every domain and compare all hashes → commit. Any failure
reconstructs the rollback snapshot through the same path and validates it. If
that also fails, the barrier stays in `Restore` with admissions sealed:
`resume()` cannot release it; frontend pumping and shutdown cancel still work.

External arrivals during a transaction are sealed into a transaction-owned
deferred list (arrival order and identity kept). After a capture or a
rollback they are appended to the inbox in order; after a committed restore
they belong to the abandoned timeline and are counted as *superseded* — never
delivered, never silently dropped. Arrivals before the transaction are part of
the captured inbox.

## Files

- `src/savestate/{snapshot,hash,service,runtime_domains,app_domains,dev_trigger}.*`
- `src/quiescence/quiescence.{hpp,cpp}`: Capture/Restore transactions, owner
  sites, signal tracking, retirement, logical clock rebase.
- `src/continuation/{execution,hle,runtime_owner}.*`: pending op, resume,
  replayability, owner export/retire/reconstruction, logical lifetimes.
- `patches/n64modernruntime-savestate.patch` (4th in the canonical runtime
  series: osStopThread → quiescence → continuations → savestate): inbox seal
  and export/import, running-queue head, static timer set and time import,
  VI/event export/import, audio frequency, reconstructed workers, retire
  thrower, librecomp overlay state.
- `src/main/native_boot.cpp`: host audio ledger/adapter and dev trigger wiring.
- `tests/savestate/`: non-live integration test.

## Non-live validation (measured)

`python3 tests/savestate/run.py [--sanitize]` links the actual patched
ultramodern scheduler, message queues, timer thread and thread lifetimes; the
actual continuation dispatcher/HLE/owner registry; the P2 coordinator and the
savestate service. The guest is a hand-written continuation corpus: idle
thread in `Pause`, producer paced by a real periodic `osSetTimer` sending two
values per tick into a capacity-1 queue, consumer verifying sequence
continuity, a never-started thread, a self-stopped thread, and a worker
recreated at the same `OSThread` address every 16 values. Memory extent: 16 MiB.

| Check | Result |
| --- | --- |
| A capture integrity, structural validation, no guest mutation, no logical time advance | PASS |
| B each domain mutation changes only its own hash (+ aggregate) | PASS |
| C capturing the same Frozen state twice gives identical hashes | PASS |
| D canonical bytes contain no host addresses (RDRAM base, contexts, snapshot) | PASS |
| E full queue: saved blocked sender restored, pending send completes exactly once in order; blocked receivers captured | PASS |
| F self-stopped thread; never-started thread | PASS |
| G address reuse with new lifetimes; stale host keys invalid after restore | PASS |
| H page zero at capture and written later returns to zero; extended pages beyond 8 MiB | PASS |
| I renderer blob parse/hash: color, depth and semantic hashes independent; host generation excluded; truncated planes rejected | PASS |
| Restore after independent advancement: all domain hashes equal, counters/checksum equal, play continues without sequence error | PASS |
| External arrivals before Frozen / during Frozen / during capture / during restore / after Resume: no duplication, no loss (1 superseded, accounted) | PASS |
| Rollback faults: after-retire, after-memory, after-time, after-scheduler, after-continuations, after-audio, post-validate | PASS (pre-restore hashes restored exactly, no stale owners, still playable) |
| Unrecoverable rollback: stays in Restore, sealed, `resume()` refused | PASS |
| 100 cycles capture/advance/restore (fresh capture every 10) | PASS: 0 deadlocks, 0 hash mismatches, 0 stale owners, 0 sequence errors |
| ASan + UBSan | Clean (after fixing two findings: fixture signed overflow; `memcpy` null source for empty hash input) |
| TSan (`setarch -R`) | 0 reports in runtime/continuation/savestate code; 6 reports only in the test harness polling guest counters. It found a real use-after-free in the first retire design (signalling a context the cleaner could free); fixed by signalling under the registry lock |

Fixture timings from the 100-cycle stress across six normal runs (16 MiB
extent, ~20 nonzero pages — **not** the 512 MiB game extent):

| Metric | Observed range across runs |
| --- | --- |
| Capture median / p95 / max | 1.3–2.2 ms / 1.8–5.0 ms / 1.8–5.0 ms |
| Restore median / p95 / max (incl. rollback capture + post-validate) | 4.4–6.7 ms / 7.6–17.6 ms / 9.9–64.0 ms |
| Snapshot payload | ~81.8 KB |

The 64 ms maximum is a single outlier (one run); the others stayed ≤ 21.4 ms.
Peak temporary bytes are dominated by the rollback snapshot (same order as the
payload); a separate RSS high-water measurement was not taken.

The P2 coordinator stress, P2 kernel (frozen and baseline), P4-A production
continuation tests (3), auditor (5) and schema (6) tests pass unchanged.

Game-scale memory helper estimate (scratch benchmark, not the game: 512 MiB
anonymous extent, 8 MiB populated, 7 runs): `capture_memory` median 34.8–38.2 ms,
`install_memory` median 20.1–20.8 ms, memory hash ~1.8 ms. A restore performs
three full-extent passes (rollback capture, install, post-validate capture), so
roughly 100 ms plus renderer/owner work is expected at game scale. Real
in-game capture/restore latency is **not yet measured**; the dev trigger prints
it (with per-phase timings) on every operation.

`resident_bytes` is sampled with `mincore` before the scan. The scan itself
maps untouched pages to the shared zero page, which later `mincore` calls count
as resident, so only the first capture in a process reports true residency.

## Clean build

Isolated tree `/tmp/sbk-p4bc-clean-1Y6K8p` (temporary local evidence, not a
repository artifact): project copy without build/deps directories, dependencies
cloned from their upstream repositories at the pinned commits by the repository
bootstrap scripts, generated P4-A corpus as input.

| Step | Result |
| --- | --- |
| Fresh apply: runtime osStopThread → quiescence → continuations → savestate | PASS |
| rt64 and RecompFrontend (resolution → quiescence) series | PASS |
| Second apply (idempotent, all complete canonical state) | PASS |
| Configure `SBK_CONTINUATIONS=ON`, renderer stack, native boot | PASS |
| Build `SnowboardKidsRecompiled` from scratch, first attempt | PASS (0 errors, 2 min 32 s) |
| P4-B/P4-C tests against the freshly patched pinned dependencies | PASS |

Two small source edits made after the tree was copied (dev-trigger early
rejection of a restore with no snapshot; residency sampling order) were synced
into that tree and rebuilt incrementally there (PASS); its `src/` is identical
to the repository's.

## P4 / P5 audio boundary

> Update: the P5 boundary is now a tested unit (`src/savestate/host_audio.*`)
> with pre-mutation validation and an explicit flush-then-requeue-once
> policy; see [P6 — P5 audio boundary](P6-PERSISTENT-SAVESTATE.md#p5-audio-boundary).
> The text below is the original P4 description.

P4 captures the not-yet-consumed PCM backlog (mirrored in a bounded ledger
because SDL cannot read back queued audio; the device is paused at CloseVI so
the queued byte count selects the exact backlog), the conversion history and
the guest AI frequency. Restore rebuilds the converter for the current
device, clears queued future audio and queues the saved backlog exactly once.
No AI task is duplicated (RSP work is drained at Frozen; AI events are in the
captured inbox). Deferred to P5: the already-submitted hardware tail at the
freeze instant, drift and deterministic audio observation. If the device
output format changed since capture, the audio install fails and the restore
rolls back.

## Other explicit limitations

- Persistent save media (controller pak / EEPROM buffers and files) are not
  rewound by a restore; loading an older state does not rewrite save files.
  In this build no such file exists (Controller Pak HLE reports no pak, save
  type None); see [P6 — Controller Pak policy](P6-PERSISTENT-SAVESTATE.md#controller-pak--save-media-policy).
- Rumble motor state is host output only and is not restored.
- A capture is rejected (retry later) if an owner is parked inside a
  non-message native HLE, a scheduler signal stays in flight, or a guest
  address is being reused while its old worker is still exiting.
- Restore rejects snapshots from a different corpus/build or adapter set.

## Development trigger (DEVELOPMENT ONLY)

> Update: the trigger is now an input source for the shared savestate driver
> (`src/savestate/driver.*`), which also runs the user quick save/load (F5/F8,
> [P7](P7-SAVESTATE-UX.md)). Keys, log lines and control-file commands below
> are unchanged; the control file additionally accepts `<seq> quicksave [slot]`
> and `<seq> quickload [slot]`.

`SBK_P4_SAVESTATE_DEV=1` enables the coordinator and the driver (refused if
`SBK_P2_CYCLES` is set). Ctrl+F6 captures, Ctrl+F7 restores (window focus
required, read with `SDL_GetKeyboardState` so no SDL event is consumed).
Optionally `SBK_P4_SAVESTATE_CONTROL=<file>` accepts lines `<seq> capture`,
`<seq> restore` or `<seq> restore <fault-name>`, each new larger sequence once.
One snapshot is kept in memory and replaced only by a successful capture.
Logs: `P4 DEV CAPTURE ok ...`, `P4 DEV RESTORE ok|ROLLED_BACK|UNRECOVERABLE ...`,
per-domain `HASHES` and `PHASES` lines.

## Live gate (manual validation reported by user)

Procedure:

1. Start Snowboard Kids, enter an interactive race, reach a recognizable state.
2. CAPTURE (Ctrl+F6); confirm `P4 DEV CAPTURE ok`.
3. Play at least 20 s: change position, speed, race timer, HUD, item if possible.
4. RESTORE (Ctrl+F7); confirm `P4 DEV RESTORE ok`.
5. Verify position, speed, timer, HUD, item, framebuffer restored; audio
   continues without duplication or gross corruption; input works; the race
   stays playable at least 20 s.

**Result: PASS — manual validation reported by user (2026-09-24).** The
interaction (gameplay, visual/audio judgement, timings of play) was performed
and judged by the user; the values below are the dev-trigger log lines the
user reported. They were not measured by the implementing agent.

| Reported item | Value |
| --- | --- |
| `P4 DEV CAPTURE ok` | 1 |
| `P4 DEV RESTORE ok` | 1 |
| capture `total_us` | 172506 |
| capture `payload_bytes` | 3023639 |
| capture `mapped_bytes` | 536870912 |
| capture `resident_bytes` | 3575808 |
| capture `nonzero_page_bytes` | 2699264 |
| capture `nonzero_bytes` | 1965356 |
| capture `threads` | 6 |
| capture `renderer_bytes` | 311839 |
| restore `total_us` | 193378 |
| restore `rollback_bytes` | 3023663 |
| CAPTURE hashes == RESTORED hashes | aggregate, memory, continuations, scheduler, time, vi, audio, input, overlays, rsp, renderer, color, depth |
| hash/invariant failures | 0 |
| continuation fallbacks/errors | 0 |
| gameplay ≥ 20 s after restore | PASS |

## SHUTDOWN-01

Manual application exit may SIGSEGV during teardown:
`MQ_IS_EMPTY → do_recv → sbk::continuation::advance_hle → run_execution →
run_thread_function`. Observed after the P4-A race gate when the user closed
the window, and again after the P4-C live gate. It does not affect
capture/restore.

Update: diagnosed as a use-after-unmap (RDRAM unmapped by `recomp::start`
while detached guest workers still ran) and fixed code-side by
`n64modernruntime-shutdown.patch` plus `_Exit` in `main`; live confirmation is
part of the P6/P7 gate. Details in
[P6 — SHUTDOWN-01](P6-PERSISTENT-SAVESTATE.md#shutdown-01).
