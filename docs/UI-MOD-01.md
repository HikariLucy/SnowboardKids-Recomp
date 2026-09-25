# UI-MOD-01: Mods tab throws "ModMenu created before game mod ID was set"

Status: open. Root cause known. The fix needs a product decision.

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
together with option 3. A test needs a RmlUi context (TabContext -> ModMenu),
or a seam that asserts the id is set before the first draw.

Not related to GRAPHICS-ASPECT-01 or P6-XPROC-02.
