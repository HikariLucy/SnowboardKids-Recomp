# Local multiplayer P1: discovery and gates

Source inventory: `snowboardkids-decomp` commit
`03088d8c5164644c7697846770b091d5265e53de`.
No multiplayer menu or race pass has been recorded on this branch. Isolated
guest-port tests passed with 2, 3 and 4 SDL virtual controllers.

## Guest capability

- The guest allocates four `RacePlayer` records and four camera slots.
  `race/flow/race_flow.c` has explicit `gPlayerCount` cases 1–4. Case 2
  configures two stacked viewports; case 3 uses three quadrants; case 4
  uses four quadrants. Course 6 uses `configureRaceViewport` variants.
- `menu/main_menu/controller_subsystem.c` calls `osContInit`, records a
  controller bitmask and count, and reads four `OSContPad` entries.
  `menu/race_setup/race_setup_menu.c` limits the player-count selection to
  `gConnectedControllerCount`.
- `race/flow/race_flow.c` sends 2–4 players through per-player character and
  course selection. It also branches on split-screen mode and race type.
  `gRaceResultState` and `rankIndex` participate in results logic.
- A four-player demo is not evidence of four independent human ports. The
  demo sets its own player/CPU/replay state; the live 2P/3P/4P paths remain
  untested.

## Host boundary at the starting baseline

`native_boot.cpp` forwards `get_input(port)` to RecompInput for ports 0–3,
but `get_connected_device_info(port)` exposes a controller only for port 0.
RecompInput has a player-assignment model (`recompinput/players.h` and
`profiles.h`), yet defaults to single-player mode. In that mode
`profiles::get_n64_input` reads shared single-player profiles regardless of
the requested player index. Changing only the connected-device callback
would make the guest think extra ports exist while their input could mirror
P1. The port must align guest presence with the existing assignment model.

The P1–P4 acceptance gate requires four distinct virtual SDL controllers,
separate assignments, and exact guest observations: P1 Left, P2 Right, P3 A,
P4 Start. Releasing every action must yield neutral for each port. Detaching
P3 must report P3 absent/neutral while P1, P2 and P4 remain unchanged.
Repeat connect/disconnect and input checks 100 times. No desktop-global input
is needed or permitted.

The 2P/3P/4P isolated gate now passes: assignment happens after deferred
controller profiles are published, and input is sent after assignment. The
four-port case observed Left/Right/A/Start separation, P3 neutral/error on
disconnect and neutral release on the remaining ports. The 100-cycle stress
gate remains open.

## Renderer and runtime gates

The existing `8a7d443` Expand fix must be checked with 2 stacked viewports,
3 quadrants and 4 quadrants. For each layout, verify Original and Expand
rectangles, scissor boundaries, framebuffer color/depth ownership and HUD
aspect per player. A single bounding rectangle over all viewports is not a
valid split-screen result. The geometry and lifecycle suites passing for the
single-viewport baseline are necessary, but do not prove these cases.

Run 100 scene-lifecycle transitions and 100 logical multiplayer-like
savestate cycles. The fixture must capture guest player/camera state,
mutate it, restore it, and compare semantic state. Host controller objects
must remain outside `.sbks`. Quiescence must drain Graphics and RSP without
unmatched completions, stale interpolation history or present deadlocks.

## Human live gates

Single-player candidate: boot the USA ROM with `SBK_COMPAT_TRACE=1`, then
visit title, menu, character/course select, race, one item pickup and use,
AI finish, results, retry/back to menu, and a training or secondary mode.
Record the diagnostic log and compare it with `scripts/compat_coverage.py`.
This is a compact session, not a full-game completion requirement.

Multiplayer candidate (hardware required): attach two controllers, assign
them separately through the existing Controls flow, enter the game's player
count menu, choose 2P, choose characters and course, start split-screen,
move each rider independently, use an item, finish, inspect results and
rematch/exit. Extend the same gate to 3P/4P after code-side and virtual
fixtures pass. No multiplayer LIVE PASS is claimed until a human completes
this gate.
