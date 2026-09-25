# UI-MOD-01: Mods tab throws "ModMenu created before game mod ID was set"

Status: fixed. The Mods tab is hidden until mod support is enabled (option 1).

## Failure

```text
std::runtime_error: ModMenu created before game mod ID was set. call update_game_mod_id() first.
recompui::ModMenu -> TabContext -> TabbedModal -> UIState::update_contexts -> draw_hook
```

## Root cause

- `src/main/native_boot.cpp` registers the game with `.mod_game_id = ""` but
  still calls `recompui::config::create_mods_tab()`.
- The Mods tab builds `ModMenu` lazily, the first time the tab is shown.
  `ModMenu::ModMenu` (RecompFrontend `ui_mod_menu.cpp`) throws while
  `current_game_mod_id` is empty.
- `recompui::update_game_mod_id()` is called only from the RecompFrontend
  launcher (`ui_launcher.cpp`). This program never shows the launcher: it
  starts the game on the first VI. So the id is never set, and opening the
  Mods tab always throws, independent of savestates or graphics.

## Options

1. **Hide the tab**: drop `create_mods_tab()` while mods are unsupported.
   Honest and smallest, but removes a UI surface.
2. **Empty tab**: call `recompui::update_game_mod_id("<id>")` before
   `recomp::start`, and leave `GameEntry::mod_game_id` empty. The menu builds
   and lists no mods (librecomp returns nothing for an unregistered id). Mods
   placed there never load, which is misleading.
3. **Real mod support**: set `GameEntry::mod_game_id`. librecomp then registers
   the game and runs `load_mods()` into guest RAM at start. This changes the boot
   path and interacts with savestate memory and compatibility. It needs its own
   validation.

The chosen id becomes a public mod-manifest contract, so it should be decided
together with option 3.

## Decision and implementation

Option 1, tied to the same authority librecomp uses. `src/main/config_tabs.hpp`
registers the Mods tab only when the game has a `mod_game_id`, which is exactly
when librecomp loads mods. In that case boot calls `update_game_mod_id()` before
the tab exists. Snowboard Kids registers none, so the options menu has General,
Graphics, Controls and Sound, and no ModMenu can be built.

Enabling mods later (option 3) sets `mod_game_id` in `native_boot.cpp`. The tab
and the frontend id then follow automatically.

A file dropped on the window before the game starts still selects the `mods`
tab id. With the tab absent this is a no-op (`TabbedModal::set_selected_tab`
ignores unknown ids). This game starts on the first VI, so the window is
practically closed.

Coverage: `python3 tests/frontend/run.py`. It runs the tab policy (absent,
other tabs in order, present with a fixture id) and pins the structural facts:
ModMenu is built only by the Mods tab, and the id is set before it.

Not related to GRAPHICS-ASPECT-01 or P6-XPROC-02.
