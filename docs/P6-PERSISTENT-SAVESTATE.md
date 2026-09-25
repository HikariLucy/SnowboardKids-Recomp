# P6 — Persistent savestates (`.sbks`)

**STATUS (2026-09-24):**
**P4 in-memory savestate: PASS LIVE** (manual validation reported by user; see
[P4-B/P4-C validation](P4-BC-SAVESTATE-VALIDATION.md)).
**P5 audio boundary: CODE-SIDE PASS.**
**P6 persistent format/storage/load: CODE-SIDE PASS.**
**P6 LIVE (disk save → new process → load): PENDING HUMAN VALIDATION — not claimed.**

The quick save/load UX that drives this is in [P7](P7-SAVESTATE-UX.md).

## Flow

```
quick save:  freeze → capture (P4) → resume → encode .sbks → atomic write     (worker thread)
quick load:  read (bounded) → decode + validate everything (worker thread)
             → only then freeze → transactional restore (P4-C) → resume
```

A file that fails any check never freezes the game. The restore itself keeps
the P4-C guarantees: validation before mutation, rollback to the pre-load
timeline on failure, sealed barrier only if the rollback itself fails.

## Format version 1

All integers little-endian at fixed offsets. No C++ struct is written with
`fwrite`; the P3 renderer header blocks are `#pragma pack(1)` fixed-width
layouts stored as length-checked opaque blocks.

### Header (128 bytes)

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 8 | magic `53 42 4B 53 0D 0A 1A 0A` (`SBKS\r\n\x1A\n`) |
| 8 | 2 | format version (1) |
| 10 | 2 | header size (128) |
| 12 | 4 | flags (0; unknown bits → UNSUPPORTED_VERSION) |
| 16 | 2 | endianness marker `0x1234` |
| 18 | 1 | compression (0 = none; 1 = zstd reserved, rejected by this build) |
| 19 | 1 | reserved (0) |
| 20 | 4 | section count (1..64) |
| 24 | 16 | game id, ASCII, NUL padded: `snowboardkids` |
| 40 | 8 | ROM identity: XXH3-64 of the ROM image validated by `recomp::select_rom` (`0xF384619787B78D4B`) |
| 48 | 8 | build identity: continuation corpus digest |
| 56 | 4 | build identity: generated function count (1981) |
| 60 | 4 | build identity: HLE count (56) |
| 64 | 4 | snapshot schema version (`kSnapshotVersion` = 1) |
| 68 | 4 | present domain mask |
| 72 | 8 | uncompressed payload bytes |
| 80 | 8 | stored payload bytes |
| 88 | 8 | aggregate canonical hash (P4 `compute_hashes`) |
| 96 | 8 | XXH3-64 of the section table |
| 104 | 16 | reserved (0) |
| 120 | 8 | XXH3-64 of header bytes [0, 120) |

Magic, version and header size are layout-stable for every future version;
the header checksum always occupies the last 8 header bytes, so a corrupted
version field is reported as corruption, not as a newer version.

### Section table (48 bytes per entry, immediately after the header)

`u32 type, u16 version (1), u16 flags (bit0 = REQUIRED), u64 offset,
u64 uncompressed length, u64 stored length, u64 canonical hash, u64 XXH3-64 of
the stored bytes`. The canonical hash is the P4 domain hash for domain
sections (XXH3-64 of the payload for METADATA).

| Type | Section | Payload |
| --- | --- | --- |
| 1 | MEMORY | canonical encoding: extent, page size, (page index, 4 KiB page) for every nonzero page, normalized `OSThread::context` slots |
| 2 | CONTINUATIONS | lifetime counter; per owner: guest address, logical lifetime, entry/arg, run state, pending op, started, GPR/FPR/HI/LO/FCSR/FR/rounding, frames (IDs, live locals, scratch), blocked HLE phase/args/result |
| 3 | SCHEDULER | running-queue head, external inbox in delivery order |
| 4 | TIME | logical ns, `osSetTime` offset, active timer set |
| 5 | VI | both VI mode banks, registers, counters, SP/DP/AI/SI registrations |
| 6 | AUDIO | guest AI frequency, rates, conversion history, unconsumed PCM backlog |
| 7 | INPUT | explicit empty host latch record |
| 8 | OVERLAYS | loaded sections by stable section-table index, relocation table |
| 9 | RSP | 4 KiB DMEM |
| 10 | RENDERER | P3 `SBK3` header (generation normalized to 0), RDP/VI blocks, framebuffer headers |
| 11 | FRAMEBUFFER_COLOR | GPU-authoritative color planes, in framebuffer order |
| 12 | FRAMEBUFFER_DEPTH | GPU-authoritative depth planes, in framebuffer order |
| 13 | METADATA | optional (not REQUIRED): `slot`, `writer`; no timestamps |

Domain payloads 1–9 are exactly the canonical byte encoding that P4 already
hashes (explicit field order, fixed widths, explicit lengths); the reader is
its bounds-checked inverse. Sections are written in type order and tile the
file after the table exactly (no gaps, no trailing bytes). Readers accept any
table order and any physical order.

### Determinism

The same logical snapshot always produces the same bytes: no wall-clock time,
no host addresses, the P3 host generation counter normalized, deterministic
section order and metadata. Verified by tests (same snapshot twice; the same
Frozen state captured twice; blobs differing only in host generation).

Never serialized: host pointers, native stacks/return addresses, `std::thread`,
TLS, mutex/semaphore bytes, SDL/Vulkan/RT64 objects, host audio buffers,
wall-clock time, the ROM.

## Compatibility policy

A load requires, in order:

| Check | Failure status | User message |
| --- | --- | --- |
| file exists | NOT_FOUND | "No quick save exists" |
| size ≤ 256 MiB, magic, header size, header checksum, endianness, reserved bytes | TOO_LARGE / CORRUPT_HEADER / TRUNCATED | "Save corrupted" |
| format version ≤ 1, no unknown flags, compression none | UNSUPPORTED_VERSION | "Save incompatible" |
| game id = `snowboardkids` | WRONG_GAME | "Save incompatible" |
| ROM identity = validated ROM XXH3-64 | WRONG_ROM | "Save incompatible" |
| snapshot schema = 1 (newer → UNSUPPORTED_VERSION, older → INCOMPATIBLE_BUILD) | as noted | "Save incompatible" |
| continuation corpus digest, function count and HLE count equal | INCOMPATIBLE_BUILD | "Save incompatible" |
| table bounds/overlap/duplicates, sizes, section checksums | CORRUPT_HEADER / CORRUPT_SECTION / TRUNCATED / TOO_LARGE | "Save corrupted" |
| every present domain has its REQUIRED section | MISSING_REQUIRED_SECTION | "Save corrupted" |
| unknown REQUIRED section / newer section version | UNSUPPORTED_VERSION | "Save incompatible" |
| canonical hashes (every domain + aggregate) after decoding | HASH_MISMATCH | "Save corrupted" |
| snapshot structure (P4 `validate_snapshot`) | CORRUPT_SECTION | "Save corrupted" |
| adapter set, memory extent, continuation frame shapes, audio device format, runtime ready (at restore, before mutation) | rejected by the P4-C service | "Save incompatible" / "Not ready yet" |

The build identity is deliberately **not** a commit SHA: it is the generated
continuation corpus (the manifest that fixes function/continuation IDs and
frame shapes) plus the snapshot schema and the adapter set. Builds that differ
only in unrelated code load each other's files; any change that alters what a
saved frame means changes the corpus digest or schema and is refused. Unknown
OPTIONAL sections are skipped (still checksum-verified), so later versions can
add optional data.

## Parser hardening

`.sbks` is untrusted input. Nothing is allocated from a file-provided length
before it is checked against the bytes actually present:

- file ≤ 256 MiB (checked from the file size before reading), sections
  ≤ 256 MiB each, ≤ 64 sections (`sbks::Limits`);
- 64-bit overflow checks on offset+length and on sums;
- sections must lie inside the payload region, must not overlap, and must add
  up to the declared payload; no trailing bytes;
- duplicate known sections, unknown entry flags, version 0 are corruption;
- stored length ≠ uncompressed length is corruption (no compression in v1);
  the uncompressed length is bounded before any decompression would happen;
- element counts inside payloads are bounded by the remaining bytes divided by
  the minimum element size; booleans must be 0/1; enums are range-checked by
  the P4 structural validation;
- decoding fills a staging snapshot; the output is replaced only on success.

## Storage

- Location: `<frontend config path>/savestates/<slot>.sbks`. The config path
  is the one registered with RecompFrontend (`recomp::register_config_path`,
  read back with `recomp::get_config_path()`); in this build it is
  `<working directory>/runtime-data`. The quick slot is `quick.sbks`.
  Nothing is written next to the ROM and no path is hard-coded.
- Slots: `storage::slot_path(dir, slot)` accepts `[a-z0-9_-]{1,32}`; the
  driver API takes a slot identifier, the UX uses only `quick` for now.
- Atomic write (POSIX): unique temp `.<name>.tmp-<pid>-<n>` in the same
  directory (`O_CREAT|O_EXCL`, mode 0644 subject to umask) → full write
  (EINTR/short writes handled) → `fsync(file)` → `close` → `rename` over the
  target (atomic replace) → `fsync(directory)`. Any failure before the rename
  unlinks the temp file and leaves the previous save untouched. Temps older
  than one minute left by a crash are removed on the next save of that slot.
  A partially written file can never carry the final name.
- Windows (compiled, not tested here): temp + `FlushFileBuffers` +
  `MoveFileExW(REPLACE_EXISTING | WRITE_THROUGH)`.
- Reads are bounded by the size limit before allocation; a file that changes
  size while being read is rejected.
- At application exit an in-flight quick save gets up to 5 s to finish its
  atomic write; if it cannot, the previous file stays valid.

## P5 audio boundary

Not serialized: the SDL device, its mixer, callbacks or opaque host buffers.
Semantic state kept: guest AI frequency, conversion history, rates, and the
PCM backlog that was submitted but not yet consumed (mirrored in a bounded
ledger because SDL cannot read queued audio back; the ledger now records only
what SDL accepted). AI events come from the VI thread through the external
inbox and are captured, sealed and accounted like every other external event.

Restore policy, explicitly: **flush the host queue, then requeue the saved
backlog exactly once.** The conversion history is restored, so the next guest
buffer converts as it would have. Checks that can fail (device format, history
shape, frame alignment, ledger bounds) now run in `validate`, before any
mutation, instead of inside the install step.

Guarantees (tested with a synthetic device, and through the real
AudioDomain in the runtime fixture): no audio duplicated, no consumed buffer
requeued, no accumulation over repeated restores, no artificial AI completion
(restore emits no events), backlog exact under partial consumption, rollback
restores the pre-load backlog.

Known limitation (P5.5): samples already handed below the SDL queue (one
device period, 256 frames ≈ 5.3 ms at 48 kHz, plus OS mixer latency) cannot be
recalled or rewound. At capture they are not part of the backlog; at restore
the abandoned timeline's tail drains during the Frozen window (restore
≈ 190 ms live). The result is a short discontinuity, never duplicated or
replayed audio. Sample-exact audio is not attempted. The device is opened at a
fixed 48 kHz float stereo format (SDL converts to the hardware), so the saved
backlog format is the same on every machine; if it ever differs, the load is
rejected before mutation.

## Controller Pak / save media policy

In this build there is no external save file to protect: the Controller Pak
HLE functions (`osPfs*`) report `PFS_ERR_NOPACK`, the controller reports no pak
and the save type is `SaveType::None`, so librecomp's saving thread never
writes. Policy: a savestate restores guest gameplay state only; external
persistent save media are never rewound and never written by save/load. If a
future build enables a Controller Pak or EEPROM file, a load will not rewrite
it; only the game's own later save writes (guest behaviour) would update it,
from the restored timeline. Documented, not redesigned.

## SHUTDOWN-01

Diagnosis from the recorded P4-A and P4-C logs: `P4A shutdown` is printed after
`recomp::start` returned — i.e. after it had `munmap`ped RDRAM — and the crash
follows in a detached guest worker (`MQ_IS_EMPTY → do_recv → advance_hle`)
reading a message queue in guest memory: a use-after-unmap during teardown.

Fix (small, no runtime redesign): fifth canonical runtime patch
`n64modernruntime-shutdown.patch` keeps RDRAM mapped until process exit, and
`main` flushes stdio and ends with `std::_Exit` after `SDL_Quit`, so detached
workers never see unmapped memory or destroyed static registries. CMake refuses
a runtime without the patch. `.sbks` integrity never depended on this (atomic
rename); the fix matters for a clean exit. **Status: fixed code-side; not
reproducible without the live game, so confirmation is part of the live gate
(close the game, check that no crash backtrace is printed).**

## LIVE-STARTUP-01

Symptom: every launch aborted before gameplay (heartbeat ~720–1080, no guest
owner yet) with `P2 unmatched completion` thrown by
`quiescence::completed(Graphics)` on the gfx thread.

Root cause (bisected live: f91fb36/5102039 start, d6298fe/d177aa7 abort; first
bad commit d6298fe): `driver::init` — which arms P2 — moved after
`select_rom()` for `rom_hash`, and so after `init_frontend_config()`. That
creates the graphics tab, whose `set_graphics_config` queues an
`UpdateConfigAction` through `trigger_config_action`. P2 was still disabled, so
`try_accept_config` did not count it; the gfx thread's first dequeue then
completed it with P2 armed and `pending[Graphics] == 0`. Instrumented run:
1 config admitted while disabled, 0 graphics accepted, first completion at
generation 0 / Idle. Not a race and not a double completion.

Fix: savestates are initialized (P2 armed) before the first device producer —
before theme/controls/frontend config and ROM selection (`rom_hash` is static
game metadata). `enable()` now refuses to arm, with
`P2 enabled after unaccounted device work`, if any graphics/RSP work was
admitted while disabled, so a future ordering regression fails at enable time
instead of at a random completion. `completed()` is unchanged.

Regression coverage: `tests/quiescence/lifecycle.cpp` (in `run.py` and CTest)
— 50 armed startups with config/VI work queued before the graphics worker
starts, all matched; an invalid Graphics/RSP completion after the activation
boundary still throws; `--late-enable` reproduces the old order and checks the
coordinator refuses to arm. Live: 3/3 startups past the old abort point into
gameplay (startup retired, 6 owners, no backtrace). P6/P7 persistent live
remain pending (below).

## P6-XPROC-01

Symptom (human gate, process B): F8 in a fresh process restored the state
(`RESTORE ok`, all domain hashes equal to the capture, "State loaded", audio
back) but the image froze and the window did not close. The frontend loop
kept running (heartbeats to the end of the log); later F8 presses timed out
in `DrainDevices` with every game owner parked.

Stacks from a hung process (reproduced with the preserved `.sbks`): the Gfx
thread spun in `WorkloadQueue::advanceToNextWorkload` (workload ring full);
RT64 Workload waited in `waitForPresentId(913)`; RT64 Present waited in
`waitForWorkloadId(1821)` for a later present, although present 913 (the
restored frame from `present_restored_frame`) had already been consumed.
Present 913 was never published.

Cause: `reset/import_semantic_state` zeroed both RT64 interpolation counter
sets but kept the color images of the last pre-restore workload. When the
restored VI framebuffer was one of them (double-buffer parity, about 1 in 2),
`threadPresent` took the interpolation path with
`framesToPresent = count = 0`, presented nothing, and skipped
`notifyPresentId`. The next workload waited on that id forever, so no DP
completion reached the game (audio kept running), the Graphics participant
never drained, and shutdown blocked joining the Gfx thread.

Not specific to fresh processes: one process with a dev capture followed by
restores hung on its first restore. Process A passed by chance (2/2).

Fix: a restore resets host presentation history to the state RT64 assumes
after one non-interpolated frame: pre-restore color images and interpolated
targets discarded, every counter set `count = 1` (RT64 asserts
`displayFrames > 0`), nothing presented or available. No host object is
serialized and P2 is unchanged. Before the fix the live oracle hung on the
first of 8 same-process restores (the rest timed out behind it) and on about
half of cross-process loads; after it, 10/10 and 6/6 passed.

Coverage: `python3 tests/renderer_lifecycle/run.py` (no GPU; real
`RT64::SharedQueueResources` and the production reset; fails on the old reset)
and `python3 tests/renderer_lifecycle/run_live.py` (ROM + GPU).

### P6-XPROC-02: early quickload rolled back with a depth mismatch

Observed twice before 8312740: a quickload about 2 s after the game threads
started (boot logos) rolled back with `post-install hash mismatch in depth`
("Load failed", previous state intact, no guest side effects).

Status: not reproducible at a1ca18f; open with forensics. There were 0 failures
in 9 live attempts:

- F8 at 2 s, 3 s and 6 s after launch, windowed and with the user's fullscreen
  configuration.
- The exact original save file and timing (2 s after `live_owners=5`).
- Builds with the pre-8312740 presentation reset, and without
  `rt64-aspect-coverage.patch`.

Findings:

- The signature needs the post-install depth plane to differ from the
  GPU-authoritative plane that was just installed. Forcing the restoring
  process to export depth from RDRAM (as when no RT64 depth target exists)
  reproduces it deterministically: rollback, state intact, twice in a row.
- The reverse direction is not the cause. A save whose depth plane came from
  RDRAM (captured before any depth target existed) loads exactly: the
  RDRAM->GPU->readback round trip is byte-exact.
- So a quickload in startup is not inherently unrestorable, and a
  "not ready" gate would reject loads that work today. None was added.
  The remaining candidate is a race that changes or drops the depth target
  between import and post-validation.

Forensics: the rollback error now names every diverging domain with both
hashes (`depth:<snapshot>!=<live>`, `color`, `renderer`). A recurrence shows
whether only the plane content differs (renderer header equal) or the target
layout changed too. Covered by the savestate runtime test (a domain that
installs one byte wrong must roll back exactly and be named).

## Tests (non-live, measured)

`python3 tests/savestate/run.py` (also `--sanitize`, `--tsan`):

1. `persistence.cpp` (no runtime): roundtrip incl. metadata; deterministic
   bytes; encode refuses stale/invalid snapshots; write→read; atomic replace;
   previous file survives injected CreateTemp/Write/Sync/Rename failures with
   no temp leftovers; stale-temp cleanup; permissions; WRONG_ROM, WRONG_GAME,
   INCOMPATIBLE_BUILD (corpus, function count, domain set, older schema),
   UNSUPPORTED_VERSION (format, schema, flags, compression, domain bits,
   section version, unknown required section); CORRUPT_HEADER (checksum,
   magic, endianness, padding, reserved, count, overlaps, duplicates, offsets
   inside the header or overflowing); CORRUPT_SECTION (payload checksum,
   stored≠uncompressed, invalid enum/bool with consistent hashes, huge counts);
   HASH_MISMATCH (resealed payload flip, aggregate); TRUNCATED at 99 cut
   points including every section boundary ±1; 3000 random byte flips and 300
   garbage files rejected; table and physical reordering accepted; unknown
   optional section accepted; missing required section; TOO_LARGE limits;
   20 repeated decodes; audio boundary cases (empty, 1/3/7 buffers, partial
   consumption, 5 repeated restores, pre-mutation rejections, device refusal,
   ledger wraparound).
2. `runtime.cpp` on the actual patched scheduler/queues/timer/owners, now with
   a synthetic audio device behind the real AudioDomain: all P4 checks, plus
   100 restores of the same snapshot (0 hash mismatches, 0 stale owners, lifetime
   counter restored, deferred-event accounting unchanged: 0 duplicated/lost),
   capture A → B → restore B, a failed capture never replaces a snapshot, eight
   rejections without mutation (invalid magic, build, corrupt hash, truncated
   memory, extent, domain set, audio format, frame shape: memory bytes,
   hashes, owners and audio queue unchanged), 100 cycles capture → encode →
   decode → restore, 50 cycles capture → atomic write → read → decode →
   restore, 10 reloads of the same file, deterministic file bytes for the same
   Frozen state.
3. Cross-process: process A runs the fixture, saves `quick.sbks` and exits
   without cleanup; a fresh process B boots its own timeline, advances to a
   different point, loads and restores the file (hashes equal, lifetime
   counter restored, stale host keys invalid) and continues 300 items with no
   sequence error.

| Result | Value |
| --- | --- |
| All three stages | PASS |
| ASan + UBSan (`-fno-sanitize-recover=undefined`) | 0 reports, PASS |
| TSan (`setarch -R`) | PASS; 13 reports, **all** in the test harness (the test thread polls the guest `CONSUMED` word written by the fixture consumer without synchronization, `runtime.cpp` `advance()`/startup wait vs `step_consumer`); 0 in runtime, continuation, savestate, storage or audio code. Not declared "TSan clean". |
| Deadlocks / timeouts | 0 |

Measurements (synthetic — **not** live game measurements):

| Metric | median / p95 / max |
| --- | --- |
| Encode, game-scale synthetic (512 MiB extent, 660 nonzero pages, ~300 KB planes; 3.03 MB file) | 2.4–2.6 / 4.3–6.3 / 8.8–16.4 ms |
| Decode (incl. checksums + canonical hashes), same | 1.6 / 1.9–2.5 / 3.0–5.8 ms |
| Disk save (write + fsync + rename + dir fsync), same | 3.4 / 4.3–5.0 / 9.8–10.2 ms |
| Disk load (read + decode), same | 2.2–2.3 / 3.0–4.5 / 3.6–6.1 ms |
| Fixture file (16 MiB extent) encode / decode | 0.13 / 0.12 ms median |
| Fixture save / load / restore-from-file | 1.5 / 0.2 / 4.8 ms median |

Ranges are across the normal and sanitizer-free runs recorded while
developing. The live quick save adds the measured P4 capture (≈ 173 ms,
reported by the user) while frozen; encoding and writing happen after resume.
Expected `.sbks` size for the reported live capture: ≈ 3.0 MB (payload
3,023,639 bytes plus ≈ 8 bytes per page and table/header overhead).

## Live gate (pending, human)

See [P7](P7-SAVESTATE-UX.md#live-gate-pending-human) for the exact command and
the twelve steps (same-process F5/F8, then close, relaunch and load the same
file in a new process). **P6 LIVE PASS must not be claimed before a human
completes it.**
