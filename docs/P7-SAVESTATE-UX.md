# P7 — Quick save / quick load (initial UX)

**STATUS (2026-09-24): P7 CODE-SIDE READY. LIVE VALIDATION PENDING (human) —
not claimed.** Format, storage and compatibility: [P6](P6-PERSISTENT-SAVESTATE.md).

## Controls

| Key | Action |
| --- | --- |
| **F5** | Quick save to the `quick` slot |
| **F8** | Quick load from the `quick` slot |
| Ctrl+F6 / Ctrl+F7 | DEVELOPMENT ONLY in-memory capture / restore (P4), only with `SBK_P4_SAVESTATE_DEV=1` |

F5/F8 were checked against the frontend first: RecompFrontend binds F11
(fullscreen) and Escape (menu); the default N64 keyboard mapping uses neither
F5 nor F8. Keys are read with `SDL_GetKeyboardState` (no SDL event is
consumed), edge-triggered, ignored with Ctrl or Alt held, and ignored while a
frontend menu captures input. A player who manually binds F5 or F8 to an N64
button in the controls menu would trigger both actions.

## Behaviour

- **Quick save:** "Saving state…" → freeze (P2) → capture (P4-B) → resume →
  encode + atomic write on a worker thread → "State saved". A capture refused
  at a transient boundary (an owner inside a native HLE, a signal in flight)
  is retried at a later freeze, up to 8 attempts, before "Save failed".
- **Quick load:** "Loading state…" → read + full validation on a worker thread
  → only then freeze → transactional restore (P4-C) → resume → "State loaded".
- One operation at a time; one more request may wait behind it (so F5 then F8
  loads the state that was just saved). Further presses show "Savestate busy".
- The frontend thread never waits on guest owners; the freeze has a 10 s
  timeout that cancels the barrier; a request that cannot freeze within 5 s
  (still booting) is dropped with "Not ready yet".
- A load in a freshly started process is accepted once the runtime is ready
  (guest memory handed over, startup execution context retired); earlier it is
  rejected before mutation with "Not ready yet".

## Feedback

A small non-modal toast (bottom-left) on the existing RecompFrontend UI: one
context that captures neither input nor mouse, so gameplay input continues
while it is visible (2 s; errors 3.5 s, red border). No new UI subsystem.
`SBK_SAVESTATE_TOAST=0` disables the toast; every notice is also logged as
`SAVESTATE NOTICE <text>`.

| Situation | Message |
| --- | --- |
| save started / done | Saving state... / State saved |
| load started / done | Loading state... / State loaded |
| no `quick.sbks` | No quick save exists |
| wrong game/ROM, other build or schema, adapter/extent/frame-shape/audio-format mismatch | Save incompatible |
| checksum, truncation, malformed table/section, hash mismatch | Save corrupted |
| capture or write failed | Save failed |
| read error, restore rolled back, freeze timeout | Load failed |
| not yet in game | Not ready yet |

Technical details stay in the log (`SAVESTATE QUICKSAVE ...`,
`SAVESTATE QUICKLOAD ...` with status names such as `WRONG_ROM`, per-domain
`HASHES` and `PHASES` lines).

## Environment

| Variable | Effect |
| --- | --- |
| (none) | Savestates enabled in `SBK_CONTINUATIONS` builds; F5/F8 active |
| `SBK_SAVESTATES=0` | Disables savestates (P2 coordinator not enabled) |
| `SBK_P2_CYCLES` | P2 probe owns the barrier; savestates disabled |
| `SBK_P4_SAVESTATE_DEV=1` | Adds Ctrl+F6/Ctrl+F7 and the control file |
| `SBK_P4_SAVESTATE_CONTROL=<file>` | Dev automation: `<seq> capture`, `<seq> restore [fault]`, `<seq> quicksave [slot]`, `<seq> quickload [slot]` |
| `SBK_SAVESTATE_TOAST=0` | Log-only feedback |

## Save location

`<working directory>/runtime-data/savestates/quick.sbks` in this build (the
RecompFrontend config path). Running from `build-renderer-stack/` puts it in
`build-renderer-stack/runtime-data/savestates/`, which is git-ignored;
`*.sbks` is ignored repository-wide as well.

## Live gate (pending, human)

Build: the final tree builds `build-renderer-stack/SnowboardKidsRecompiled`
(`cmake --build build-renderer-stack --target SnowboardKidsRecompiled`).

Process A (exact command):

```sh
cd ~/proyectos/Recomp/SnowboardKids-Recomp/build-renderer-stack
rm -f runtime-data/savestates/quick.sbks
./SnowboardKidsRecompiled runtime-data/snowboardkids.n64.us.z64 2>&1 | tee /tmp/sbk-p6-live-A.log
```

1. Open the game (command above).
2. Enter a race.
3. Press **F5**. Expect the toast "State saved" and in the log
   `SAVESTATE QUICKSAVE ok slot=quick path=.../quick.sbks file_bytes=...`.
4. Play at least 30 s (change position, speed, timer, HUD, item).
5. Press **F8**. Expect "State loaded" and
   `SAVESTATE QUICKLOAD RESTORE ok`; `RESTORED HASHES` equal to the
   `CAPTURE HASHES` of step 3.
6. Verify the race returned to the saved moment (position, speed, timer, HUD,
   item, image) and audio has no duplication or gross corruption.
7. Play at least 30 s.
8. Close the game window. Expect `P4A shutdown ...` and **no**
   `crash backtrace` (SHUTDOWN-01 confirmation).

Process B:

```sh
cd ~/proyectos/Recomp/SnowboardKids-Recomp/build-renderer-stack
./SnowboardKidsRecompiled runtime-data/snowboardkids.n64.us.z64 2>&1 | tee /tmp/sbk-p6-live-B.log
```

9. Open the game again (new process).
10. Once in the game (title, menu or a race), press **F8** to load the same
    `quick.sbks` from disk.
11. Verify the race state of step 3 is restored across processes (log:
    `SAVESTATE QUICKLOAD file ok`, `SAVESTATE QUICKLOAD RESTORE ok`, restored
    hashes equal to process A's capture hashes).
12. Play at least 30 s.

Useful checks afterwards:

```sh
grep -E "SAVESTATE (QUICKSAVE|QUICKLOAD|NOTICE|TIMEOUT)|HASHES|crash backtrace|fallback" /tmp/sbk-p6-live-A.log /tmp/sbk-p6-live-B.log
```

**P6 LIVE PASS and P7 LIVE PASS require this gate performed by a human:
load from disk, in a new process, correct restore, continued play.**
