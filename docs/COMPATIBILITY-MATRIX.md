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
| Boot, Pak warning, title/demo, main menu | `race/flow/race_flow.c` startup route; `demo/title_demo_race_intro.c`; `menu/main_menu/controller_main_menu_flow.c` | Five consecutive live navigation runs reached `controller_pak`, `title_demo_entered`, `menu_navigation` | Dynamic entry path pass |
| Race setup and character selection | `menu/race_setup/race_setup_menu.c`; `menu/character_select/character_select_menu.c` | Five runs reached `character_select`; default one-player route only | Dynamic default route pass |
| Course selection and race start | `menu/course_select/multiplayer_course_select_menu.c`; `race/flow/race_flow.c` | Five runs reached `course_select` and `race_active`; default one-player route only | Dynamic default route pass |
| Ten race courses | `include/generated/course_ids.inc` defines IDs 0–9: Big Snowman, Sunset Rock, Night Highway, Grass Valley, Dizzy Land, Quicksand Valley, Silver Mountain, Animal Land, Ninja Land, Rookie Mountain | Default course route only; no course-by-course traversal | Open |
| Character roster | `menu/character_select/character_select_menu.c` has six roster IDs (`gCharacterSelectIdOrder`), including the unlock-dependent Shinobin slot | Entry to roster observed; no selection-by-character matrix | Open |
| One to four human players | `menu/race_setup/race_setup_menu.c` bounds `gPlayerCount` by `gConnectedControllerCount`; `menu/main_menu/controller_subsystem.c` counts connected controllers | `src/main/native_boot.cpp` reports only port 0 connected; no P2–P4 live run | Blocked at host port discovery |
| Split-screen and race-type choices | `menu/splitscreen_select/race_splitscreen_select_menu.c` supports selections 0–4; `menu/race_type_select/race_type_select_menu.c` routes race types; `race/flow/race_flow.c` branches on `gRaceSplitscreenMode` and player count | Renderer geometry/lifecycle unit suites pass; no split-screen live fixture | Open |
| Per-player input and rumble | `race/player/race_player_input.c`; game loops over players in setup/course select | `docs/CONTROL-P1.md` records one-controller virtual-pad coverage; controls analog suite passes; P2–P4 not exercised | P1 dynamic, P2–P4 open |
| Items, CPU racers, race completion | `race/items/`; `race/player/`; `race/flow/race_flow.c` | P4-A manual race and `race_finish` recorded in `docs/P4-A-PRODUCTION-VALIDATION.md`; current five-run gate stops at `race_active` | Historical dynamic; full matrix open |
| Training | `menu/main_menu/training_course_race_flow.c` | No current live training fixture | Open |
| Replay and ghost flow | `race/flow/race_flow.c` has `initRaceGhostReplayFlow`; `menu/main_menu/main_menu_scene_model.c` serializes race records | No current live replay fixture | Open |
| Controller Pak persistence | `menu/controller_pak/` and `menu/race_setup/race_setup_menu.c` | Port reports no Controller Pak storage (`docs/P6-PERSISTENT-SAVESTATE.md`); warning route passes | Compatibility gap |
| Ending and credits | `ending/`; `race/flow/race_flow.c` routes `initEndingCreditsFlow` | No current live completion fixture | Open |

## Baseline gate and limitations

`tests/production_continuation/run_live_test.py` now starts a finite timeout
at each semantic milestone. Five runs from the same baseline reached boot,
title, main menu, character select, course select and active race with zero
fallbacks, timeouts and residual processes. Character selection occurred at
20.508–20.877 seconds from launch (nearest-rank p95: 20.877 seconds).

The gate pulses logical N64 START through `SBK_P2_CONTROL_FILE`, with a
release between presses. It confirms the default one-player route. It does
not establish that every course, character, mode, player count, item, save
flow or renderer viewport works. Each such claim needs a state-based fixture
or a recorded manual validation on this branch.

## Multiplayer capability boundary

`src/main/native_boot.cpp` already forwards `get_input(controller_num)` to
RecompInput for ports 0–3, but `get_connected_device_info(controller_num)`
returns `Device::None` for ports 1–3. The guest's player-count menu caps its
choice at the connected-controller count. RecompInput also defaults to
single-player mode; in that mode `profiles::get_n64_input` uses its shared
single-player profiles for any queried player index. Reporting more guest
ports alone would therefore expose duplicated inputs. The first multiplayer
production gate is coherent host port discovery and assignment mode, followed
by independent press/release/neutral evidence for each guest player and a
live split-screen renderer fixture. The present one-player navigation gate
cannot certify those behaviors.
