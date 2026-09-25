# Compatibility matrix (USA ROM)

Baseline: port commit `20dca7a` on `feat/compat-multiplayer`; guest source
inventory: `snowboardkids-decomp` commit
`03088d8c5164644c7697846770b091d5265e53de`. This matrix separates
code that exists in the original game from behavior exercised in the port.
An untested row is not a pass.

## Evidence key

- **Static**: the decomp has a reachable flow or implementation. This proves
  the game contains the feature, not that the port handles it correctly.
- **Dynamic**: a named milestone or a focused live test exercised the port.
- **Open**: no live evidence for the complete flow on this baseline.

## Original-game inventory and port coverage

| Feature | Original-game evidence | Port evidence at this baseline | Status |
| --- | --- | --- | --- |
| Boot, Pak warning, title/demo, main menu | `race/flow/race_flow.c` startup route; `demo/title_demo_race_intro.c`; `menu/main_menu/controller_main_menu_flow.c` | Five live runs reached `controller_pak`, `title_demo_entered`, `menu_navigation` | PASS |
| Race setup and character selection | `menu/race_setup/race_setup_menu.c`; `menu/character_select/character_select_menu.c` | Five runs reached `character_select`; default one-player route | PASS |
| Course selection and race start | `menu/course_select/multiplayer_course_select_menu.c`; `race/flow/race_flow.c` | Five runs reached `course_select` and `race_active`; default one-player route | PASS |
| Ten race courses | `include/generated/course_ids.inc` defines IDs 0–9 | No course-by-course traversal | DISCOVERED |
| Character roster | `menu/character_select/character_select_menu.c` has six roster IDs (`gCharacterSelectIdOrder`) | Entry observed; no selection-by-character matrix | DISCOVERED |
| One to four human players | `menu/race_setup/race_setup_menu.c` bounds `gPlayerCount` by `gConnectedControllerCount` | Four isolated virtual ports reached guest; multiplayer menu/race untested | CODE-SIDE COVERED |
| Split-screen and race-type choices | `menu/splitscreen_select/race_splitscreen_select_menu.c`; `menu/race_type_select/race_type_select_menu.c` | Renderer unit suites pass; no split-screen live fixture | DISCOVERED |
| Per-player input and rumble | `race/player/race_player_input.c` | P1 virtual pad in `docs/CONTROL-P1.md`; P2–P4 untested | LIVE OBSERVED |
| Items, CPU racers, race completion | `race/items/`; `race/player/`; `race/flow/race_flow.c` | Historical P4-A manual `race_finish`; current gate stops at `race_active` | LIVE OBSERVED |
| Training | `menu/main_menu/training_course_race_flow.c` | No current live training fixture | DISCOVERED |
| Replay and ghost flow | `race/flow/race_flow.c` has `initRaceGhostReplayFlow` | No current live replay fixture | DISCOVERED |
| Controller Pak persistence | `menu/controller_pak/` | Port uses NOPACK; no original-game Pak persistence | BLOCKED |
| Ending and credits | `ending/`; `race/flow/race_flow.c` routes `initEndingCreditsFlow` | No current live completion fixture | DISCOVERED |

## Detailed checklist

Status applies to the named scope only. `PASS` on default navigation says
nothing about alternate choices on the same screen.

| Surface | State | Evidence or required gate |
| --- | --- | --- |
| Boot ROM validation | PASS | Five runs, `ROM validation: Good` |
| Company logos | UNKNOWN | No semantic milestone or manual record |
| Title | LIVE OBSERVED | `title_demo_entered`; visual/logo sequence still needs human check |
| Attract/demo | LIVE OBSERVED | `title_demo_entered`, `demo_race_players` |
| Main menu, default choice | PASS | `menu_navigation` in five runs |
| Options | DISCOVERED | `initMainMenuSettings`; navigate and inspect live |
| Character select, default route | PASS | `character_select` in five runs |
| Course select, default route | PASS | `course_select` in five runs |
| Save/Controller Pak prompts | LIVE OBSERVED | `controller_pak`, `save_select`, `rumble_prompt` in diagnostic run |
| Secondary menus | DISCOVERED | Mode select, race type, split-screen menus in decomp |
| Single-player race start | PASS | `race_active` in five runs |
| Training | DISCOVERED | `training_course_race_flow.c`; live gate pending |
| Alternative race/challenge choices | DISCOVERED | Split-screen and race-type dispatch; live gate pending |
| Results and placement | LIVE OBSERVED | Historical P4-A `race_finish`; current-branch results gate pending |
| Retry/rematch | DISCOVERED | Post-race route in `race_flow.c`; live gate pending |
| Quit/back to menu | DISCOVERED | `exitRaceFlowToMainMenu`; live gate pending |
| Item spawn/pickup | DISCOVERED | Course pickups and player pickup code; targeted fixture pending |
| Item activation/target/expiration | DISCOVERED | Player update and projectile/effect code; targeted fixture pending |
| AI racers, collision/recovery | DISCOVERED | `race/player/` and `race/items/`; targeted fixture pending |
| Race finish with no hang | LIVE OBSERVED | Historical manual P4-A; repeat on current branch pending |
| Progression flags and unlocks | DISCOVERED | Save data/course unlock code; transition fixture pending |
| Reward screens, secret unlocks | DISCOVERED | Ending/award flow and character flag; live gate pending |
| Ending and credits | DISCOVERED | `ending/`; live gate pending |
| Replay and exit replay | DISCOVERED | `initRaceGhostReplayFlow`; live gate pending |
| 2P/3P/4P setup | CODE-SIDE COVERED | Ports 0–3 isolated with virtual SDL pads; menu navigation per count pending |
| Multiplayer character/course select | DISCOVERED | Per-player menus present; no live fixture |
| Multiplayer race/results/rematch | DISCOVERED | Player-count branches and result flow; no live fixture |
| 2P/3P/4P split-screen | DISCOVERED | `race_flow.c` configures 2, 3 and 4 viewports; renderer fixture pending |
| Controller Pak/PFS calls | CODE-SIDE COVERED | Call sites mapped in decomp; port's NOPACK behavior known |
| Gameplay without Pak | LIVE OBSERVED | Default one-player race reaches `race_active` |
| Original-game Pak persistence | BLOCKED | PFS storage absent by current policy |

### Characters (selection, not just menu entry)

The roster array in `menu/character_select/character_select_menu.c` is
`{5, 0, 1, 2, 3, 4}`. The named slots are Slash (0), Nancy (1), Jam (2),
Linda (3), Tommy (4), and unlock-dependent Shinobin (5). Each row remains
`DISCOVERED` until that character is selected and a race is entered.

| ID | Character | State |
| --- | --- | --- |
| 0 | Slash | DISCOVERED |
| 1 | Nancy | DISCOVERED |
| 2 | Jam | DISCOVERED |
| 3 | Linda | DISCOVERED |
| 4 | Tommy | DISCOVERED |
| 5 | Shinobin | DISCOVERED |

### Courses (individual race traversal)

IDs come from `include/generated/course_ids.inc`. Every row is
`DISCOVERED`; the default course navigation does not identify which track
was selected.

| ID | Course | State |
| --- | --- | --- |
| 0 | Big Snowman | DISCOVERED |
| 1 | Sunset Rock | DISCOVERED |
| 2 | Night Highway | DISCOVERED |
| 3 | Grass Valley | DISCOVERED |
| 4 | Dizzy Land | DISCOVERED |
| 5 | Quicksand Valley | DISCOVERED |
| 6 | Silver Mountain | DISCOVERED |
| 7 | Animal Land | DISCOVERED |
| 8 | Ninja Land | DISCOVERED |
| 9 | Rookie Mountain | DISCOVERED |

### Items (pickup through cleanup)

`include/game/race/items/race_items.h` defines the five item effect IDs
below, with 0 meaning no item. `race/player/race_player_pickup_effects.c`
dispatches those IDs. Spawn, pickup, held state, activation, target selection,
effect lifetime and cleanup need separate evidence for each item.

| ID | Item | State |
| --- | --- | --- |
| 1 | Slapstick | DISCOVERED |
| 2 | Parachute | DISCOVERED |
| 3 | Freeze Shot | DISCOVERED |
| 4 | Snowman | DISCOVERED |
| 5 | Bomb | DISCOVERED |

The ID dispatch in `race/player/race_player_pickup_effects.c` has separate
branches for IDs 1–5 and clears `itemEffectType` to `RACE_ITEM_NONE` after
activation. `race/items/race_item_effects.c` and
`race/items/race_item_projectiles.c` implement effects and projectiles.
Pickup spawn, held state, target selection, effect timer and cleanup remain
distinct live gates; observing an ID alone does not prove these stages.

### AI and progression

`race/player/race_player_progress.c` chooses CPU pace from rank and opponent
distance. `race_player_update.c` applies CPU pace modes and runs player state
updates; `race_player_movement.c` updates `rankIndex`; collision handling is
in `race_player_collision.c`. The default race start observes CPU racers but
does not yet certify recovery, item use, finish ordering or result placement.

`GameSaveData.courseUnlockStates`, `characterFlags`, `progressionLevel` and
`cupPlacements` carry unlock state. The no-Pak initialization in
`race_setup_menu.c` resets progress. `race/effects/race_start_transition.c`
advances progression from cup placements and queues award/credits transitions;
`race/flow/race_flow.c` enters `initEndingCreditsFlow`. The complete progression
loop and credits are static discoveries only.

## Baseline gate and limitations

`tests/production_continuation/run_live_test.py` now starts a finite timeout
at each semantic milestone. Five runs from the same baseline reached boot,
title, main menu, character select, course select and active race with zero
fallbacks, timeouts and residual processes. Character selection occurred at
20.508–20.877 seconds from launch (nearest-rank p95: 20.877 seconds).

COMPAT-BASELINE-01 is classified **B: harness timeout**. The former global
18.5-second observation deadline (within a 20-second cleanup budget) expired
after main-menu entry and during the save/rumble prompt route. The five-run
gate used milestone-relative limits of 10/25/15/15/15 seconds. A later run
recorded `controller_pak` at 2.190 s, `menu_navigation` at 14.592 s,
`character_select` at 21.882 s, `course_select` at 30.529 s and `race_active`
at 45.227 s. Thus character selection arrived 7.290 s after main menu and
3.382 s beyond the old deadline. The control file pulses N64 START (`0x1000`)
on input-frame phase 0 and writes neutral at phase 2. The guest accepts
A/START in the player-count prompt, then traverses save selection and rumble
acknowledgements before `initCharacterSelectMenu`.

After the current input changes, the narrower COMPAT-BASELINE-01 gate
(`--through character_select`) passed five consecutive runs. Character select
arrived at 21.178, 20.760, 20.593, 20.459 and 20.508 s; each run had zero
fallbacks, timeouts, crashes and residual processes. A separate extended
navigation gate beyond character selection has shown intermittent progress
stalls or a race-start timeout; that gate is not part of this baseline claim.

The gate pulses logical N64 START through `SBK_P2_CONTROL_FILE`, with a
release between presses. It confirms the default one-player route. It does
not establish that every course, character, mode, player count, item, save
flow or renderer viewport works. Each such claim needs a state-based fixture
or a recorded manual validation on this branch.

## Multiplayer capability boundary

`src/main/native_boot.cpp` now publishes attached capacity during setup and
assigned, attached ports after RecompInput's assignment flow. The isolated SDL
fixture observed P1 Left, P2 Right, P3 A, P4 Start, then P3 neutral/error on
disconnect while the others stayed independent. The pinned runtime patch copies
the no-response status into guest memory so stale buttons cannot survive a
disconnect. Physical assignment, multiplayer menus and split-screen rendering
still need their own gates; one-player navigation does not certify them.
