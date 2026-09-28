# Savestate tests (non-live): P4-B/P4-C, P5 audio, P6 `.sbks`

```sh
python3 tests/savestate/run.py            # -O2
python3 tests/savestate/run.py --sanitize # ASan + UBSan
python3 tests/savestate/run.py --tsan     # ThreadSanitizer (runs under setarch -R)
```

Three stages:

1. `persistence.cpp` — no runtime: `.sbks` roundtrip and deterministic bytes,
   compatibility statuses, parser hardening (truncation at every boundary,
   random byte flips, malformed/duplicate/overlapping/unknown sections,
   oversized lengths and counts), atomic storage with injected failures, and
   the P5 host audio boundary with a synthetic device. Also prints synthetic
   game-scale encode/decode/save/load timings (not live measurements).
2. `runtime.cpp` — builds against the actual patched ultramodern scheduler,
   message queues, timer thread and thread lifetimes (`SBK_CONTINUATIONS`),
   the continuation dispatcher/HLE/owner registry, the P2 coordinator and the
   savestate service, with a synthetic audio device behind the real
   AudioDomain: capture, restore after independent advancement,
   external-arrival accounting, a blocked sender, rollback fault injection,
   100 capture/advance/restore cycles, 100 restores of one snapshot,
   rejections without mutation, 100 capture/encode/decode/restore cycles, 50
   disk cycles, and the sealed unrecoverable path.
3. Cross-process — `runtime --save <file>` writes a `.sbks` and exits; a fresh
   `runtime --load <file>` boots its own timeline, restores the file and
   continues.

It also compile-checks the app-only adapters and the driver (VI/events,
librecomp overlays, renderer/RSP domains).

`hle_stubs.cpp` provides weak stand-ins for the audited HLE allowlist; the
fixture defines the few HLEs its hand-written continuation corpus uses.

Passing does not establish live restore in the game. See
`docs/P6-PERSISTENT-SAVESTATE.md` and `docs/P7-SAVESTATE-UX.md` for the live gate.
