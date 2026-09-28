# Snowboard Kids PC frontend — implemented state

Branch `feat/sbk-frontend-theme`. Implements the approved design in
`UI-DESIGN-SYSTEM.md` within the limits of `GRAPHICS-CAPABILITIES.md`.
Visual sign-off is **pending human review**.

## Overlay (RecompFrontend)

| Area | Implementation |
|---|---|
| Theme | `src/ui/recomp_theme.cpp`: snow/ink/sun/ice/slope palette, Fredoka headings, LatoLatin body, 3–6 px ink borders, sun focus background, slope-blue focus accent. Replaces the upstream theme library (no longer compiled). |
| Menu tab | `src/ui/menu.cpp`: Resume, Settings, Controller, Quit, Quick Save (F5) / Quick Load (F8). UI callbacks only queue a `MenuAction`; the frontend polling thread calls the savestate driver. Save buttons are disabled when savestates are off. |
| Tabs | Menu, General, Graphics, Controls, Audio. Mods only with a valid mod game ID **and** an initialized mod subsystem (UI-MOD-01, `tests/frontend`). |
| Focus | Tab focus text is themeable (`recompfrontend-theme-focus.patch`); unset keeps upstream behaviour. |
| Preview | `SnowboardKidsEngine --frontend-preview` opens the overlay without ROM or module (savestates off, Resume disabled). |

Font family names must match the TTF name tables (`LatoLatin`, `Fredoka`,
`PromptFont`): RmlUi draws nothing for an unknown family. `tests/release/test_ui_assets.py`
enforces this.

## Graphics

| Option | State |
|---|---|
| Internal Resolution | Exposed. Existing keys preserved (`Original` … `2160p`, `Auto`). |
| Aspect Ratio | Exposed: Original 4:3 / Expand to window. |
| Window Mode | Exposed: Windowed / Borderless Fullscreen (Linux, Windows; SDL desktop fullscreen). macOS keeps the label “Fullscreen”. Never called exclusive. |
| MSAA | Exposed, hardware gated (unchanged). |
| HUD Placement | Exposed, bound to RT64 (unchanged). |
| Downsampling | Exposed; JSON accepts only 0/2/4 and the renderer scale ignores other factors. |
| Framerate / high FPS | Hidden. Output forced to Original regardless of saved config. Guest timing untouched. |
| VSync | Exposed: On / Off (`graphics.json` `vsync`, default On). Applied live on Apply, no restart. Off is disabled when the display/backend cannot present unsynchronized. Details and validation: `PRESENTATION-SYNC.md`. |
| Display resolution, monitor, exclusive fullscreen, HDR, ultrawide presets, DLSS/FSR/XeSS | Not exposed (not supported end to end). |

Config lives in user data (`graphics.json` under the user data folder or
`SBK_USER_DATA_DIR`). A config file of the wrong JSON shape loads defaults
(`n64modernruntime-config-shape.patch`). `tests/graphics_config/run.py`
compiles the real schema with ASan/UBSan and checks process A → process B
persistence for 42 combinations plus malformed values, and VSync default,
persistence across three processes, window-mode toggles, invalid values and
capability gating.

## Model D first run

`src/ui/first_run_window.cpp` runs **before** the renderer when no valid
module loads; native boot order is otherwise unchanged. It is an isolated
SDL_Renderer window with FreeType text (the FreeType build RmlUi already needs).

Phases: Welcome (no module) → Choose ROM → Check ROM + tools → Prepare CPU
code → Generate audio code → Compile module [x/N] → Validate module →
Install → Ready, or Error with Retry / Choose ROM / Quit. Cancel is offered
while building. Only compile carries a count (`SBK_PROGRESS` lines from
`--progress-protocol`); other phases show no percentage. Ready is granted
only by the host after ABI validation, never by builder output.

- Input: arrow keys / Tab / Enter / Space / Esc, mouse hover and click,
  controller D-pad / left stick / A / B. One focus model for all three.
- Layout: a 1280×720 design uniformly scaled and centered; minimum window
  960×540. `tests/frontend/first_run_ui.cpp` renders every phase at 720p,
  1080p, 1440p, 4K, 4:3, 960×540 and 21:9 and checks bounds, overlap and
  labels ≥ 14 px.
- ROM from the command line or `SBK_ROM_PATH` starts immediately and
  continues to the game at Ready (automation path). A remembered ROM is
  offered as “Use saved ROM”.
- No display, or `SBK_FIRST_RUN_CONSOLE=1`: the same state machine runs in
  the console.
- The builder runs from a literal argv in its own process group (job
  object on Windows); no shell parses ROM paths. Cancel/exit reaps children.
- User data only: `last_rom_path.txt`, `logs/first-run.log`,
  `logs/build-module.log`, `modules/snowboardkids-us/`. The install is read-only.

## Visual review

```sh
# Overlay, ROM-free
build-renderer-stack/SnowboardKidsEngine --frontend-preview
# First run with isolated user data (uses your ROM locally)
SBK_USER_DATA_DIR=$(mktemp -d) build-renderer-stack/SnowboardKidsEngine
# First-run snapshots (BMP) for every phase and size, no ROM needed
SDL_VIDEODRIVER=dummy SBK_FIRST_RUN_SNAPSHOTS=/tmp/sbk-shots \
  build-renderer-stack/SnowboardKidsFirstRunUITest build-renderer-stack/assets
```

Screenshots are not committed.

## Known limitations

- Focus now uses a thick border plus filled highlight, while selected tabs and
  options have an underline or weight cue. LIVE screenshots were captured for
  Controls, Audio, Accessibility, Reset, and binding capture. Aesthetic judgment
  remains pending human review.
- In `--frontend-preview` only, the launcher's version label sits under the
  overlay's “Close” prompt at the bottom-left. The launcher is not shown in game.
- Fredoka renders at the variable font's default (light) instance inside
  RmlUi; the first-run window selects weight 600 through FreeType.
- `tests/frontend/run.py` and `tests/graphics_config/run.py` pass under
  ASan/UBSan with LeakSanitizer on in this branch's environment. Where ptrace
  is restricted, LSan must be disabled (`ASAN_OPTIONS=detect_leaks=0`) and
  reported as not run.

## UX LIVE validation

With `SBK_UX_TRACE=1`, the Model D race overlay logged tab, focus, profile,
binding capture, reset, host gain, reduced-motion, and overlay transitions.
The flag is silent by default and does not log each frame. The reset prompt
explicitly focuses its requested default even when opened by mouse; this was
verified with immediate Enter on Cancel and deliberate focus on Reset. See the
three capability documents for run evidence and limits.
