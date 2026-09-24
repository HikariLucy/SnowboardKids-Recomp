# P4-B/P4-C savestate tests (non-live)

```sh
python3 tests/savestate/run.py            # -O2
python3 tests/savestate/run.py --sanitize # ASan + UBSan
```

Builds `runtime.cpp` against the actual patched ultramodern scheduler, message
queues, timer thread and thread lifetimes (`SBK_CONTINUATIONS`), the
continuation dispatcher/HLE/owner registry, the P2 coordinator and the
savestate service, then runs capture, restore after independent advancement,
external-arrival accounting, a deterministic blocked-sender case, rollback
fault injection, 100 capture/advance/restore cycles and the sealed
unrecoverable path. It also compile-checks the app-only adapters (VI/events,
librecomp overlays, renderer/RSP domains).

`hle_stubs.cpp` provides weak stand-ins for the audited HLE allowlist; the
fixture defines the few HLEs its hand-written continuation corpus uses.

Passing does not establish live restore in the game. See
`docs/P4-BC-SAVESTATE-VALIDATION.md` for the live gate.
