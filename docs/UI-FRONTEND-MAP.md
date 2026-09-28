# Frontend inventory — UI-P1

Source audit: base `4ce33da18bdf9f95392cdf1015c9978371123c34`, 2026-09-25.
Status: inventory complete; replacement and visual validation pending.
Paths beginning `FE/` refer to `.deps-renderer/RecompFrontend/recompui/src/`;
`THEME/` refers to `.deps-renderer/recomp-theme/`. Dependencies were inspected
read-only in the split worktree. This is not a claim that every screen was
rendered or exercised.

## Screens and bindings

| Screen | Source file | Current controls | Assets used | Config bindings | Replacement status |
|---|---|---|---|---|---|
| Launcher / game selection | FE/base/ui_launcher.cpp, ui_game_option.cpp | Game selection, launch, configuration; shared launcher supports mods | Registered fonts, dynamic game/mod thumbnails | Registered GameEntry and program configuration | Pending; no game-derived thumbnail to be added |
| In-game configuration overlay | FE/config/ui_config.cpp | Tabbed modal, quit, close/resume | Quit.svg, X.svg, fonts | Registered config tabs | Pending |
| Graphics | FE/config/ui_config_tab_graphics.cpp | Resolution, downsampling, aspect, window mode, framerate, MSAA, HUD placement; hidden developer/API/framebuffer controls | Shared radio/slider widgets and fonts | graphics config → GraphicsConfig → RT64Context | Pending; remove unsafe inherited framerate UI |
| General | FE/config/ui_config_tab_general.cpp; src/main/native_boot.cpp | Rumble, joystick deadzone, background input; debug hidden | Shared widgets/fonts | general config; input consumers | Pending; gyro and mouse sensitivity disabled by project registration |
| Audio (currently Sound) | FE/config/ui_config_tab_sound.cpp | Main volume | Shared slider/fonts | sound.main_volume → host audio | Pending |
| Controls | FE/config/ui_config_tab_controls.cpp, ui_config_page_controls.cpp | Profiles, bindings, record/reset/delete, keyboard/controller selection | Cont.svg, Keyboard.svg, Trash.svg, Reset.svg, Question.svg, RecordBorder.svg, Caret.svg; PromptFont | Input profile/binding APIs | Pending; retain existing navigation semantics |
| Player/controller assignment | FE/composites/ui_assign_players_modal.cpp, ui_player_card.cpp | Join, assign, shared keyboard, confirm/back | Cont.svg, Keyboard.svg, RecordBorder.svg, RecordSpinner.svg, PlusKeyboard.svg | Player/device assignment APIs | Pending |
| Confirmation dialogs | FE/composites/ui_prompt.cpp; FE/elements/ui_modal.cpp | Confirm/cancel, quit, changes pending | Fonts and programmatic theme | Callback/context stack | Pending |
| Options Apply/Revert | FE/config/ui_config_page_options_menu.cpp | Apply, discard temporary changes, close handling | Shared buttons/fonts | Config temporary/permanent storage | Existing; not a timed display rollback |
| Mods | FE/config/ui_config_tab_mods.cpp; FE/composites/ui_mod_menu.cpp | Generic installation/list/details/config, refresh | Reset.svg and dynamic mod assets | Mod subsystem and game mod ID | Intentionally absent for SBK; preserve UI-MOD-01 |
| Native file picker | FE/util/file.cpp; src/main/native_boot.cpp | Select local ROM; cancel | OS-native dialog | Selected filesystem path → ROM validation | Preserve OS dialog; theme the surrounding workflow |
| Model D missing module / build / ready / error | src/main/native_boot.cpp: first-run block and invoke_local_module_builder | Automatic discovery, native picker, blocking builder invocation; console status/errors | No themed first-run page | GameModule + module_builder; user-data modules directory | New frontend state machine needed |
| Savestate notifications | src/savestate/toast.cpp; service.cpp, dev_trigger.cpp | F5/F8; noninteractive timed toast | Shared fonts/colors | Existing save/load service and .sbks storage | Theme pending; dedicated savestate page absent |
| Controller Pak | src/pfs/controller_pak.cpp, hle.cpp | No permanent Pak UI | None | Existing user-data persistence | Optional read-only status; no delete control |

## RML, RCSS, theme, fonts

The active screens are primarily C++ element trees, not a set of editable RML
page files. Base styling is in FE/data/base_rcss.cpp and FE/elements/ui_theme.*,
ui_frontend_theme.*. THEME/src/recomp_theme.cpp registers a FrontendTheme and
fonts. THEME/assets/recomp.rcss only declares Fredoka on body. A CSS-only skin
would miss programmatic component styles.

Existing theme: blue surfaces, white text, purple accents, very rounded thick
modal frames, Fredoka throughout much of the UI. Lato is registered but is not
the default body family. PromptFont provides input glyphs; NotoEmoji is loaded
by the frontend. Existing font license texts are accounted for separately.
Use Fredoka for headings, Lato for body, PromptFont for input; add no fonts.

## Unknown-license SVG consumers

Eleven file names are consumed by the frontend: Caret.svg, Cont.svg,
Keyboard.svg, PlusKeyboard.svg, Question.svg, Quit.svg, RecordBorder.svg,
RecordSpinner.svg, Reset.svg, Trash.svg, X.svg. Source references are in the
screen table and FE/elements/ui_select.cpp, ui_binding_button.cpp.

Create new drawings from basic geometry, with a common 32×32 viewBox, in
assets/sbk-ui/icons/. Retaining compatible runtime filenames is acceptable;
retaining upstream SVG content is not. Preserve upstream checkout read-only.
Document provenance in assets/sbk-ui/README.md.

CMake currently copies the complete theme asset directory to runtime assets.
The packager walks that directory; it excludes board.svg, board-selected.svg
and rock.png but includes the navigation SVGs. Replacing only UI references
would therefore not remove their distribution. Both staging and packaging
must use a reviewed allowlist, reject unexpected SVGs and compare staged icon
bytes to project-owned sources. Stale build assets need removal from the
staging directory, without deleting upstream files.

## Navigation and scaling

FE/base/ui_state.cpp maps keyboard/controller actions into the RmlUi input
path and transforms mouse coordinates for drawable dimensions. Widgets
implement focus and tab navigation. Keep these controls and context capture
rules rather than creating mouse-only click targets. Exercise focus return,
back, confirm, radio left/right, sliders, dropdowns and tabs after styling.

The frontend sets density-independent pixel ratio to drawable height / 1080
on each rendered UI frame. This is automatic viewport scaling, not a persisted
user UI-scale option. Verify all target sizes and 4:3 before claiming coverage.
Existing focus effects include pulses; proposed theme uses steady focus.

No UI sound asset was identified in the theme asset inventory. No new sounds
are planned. The inventory does not constitute an audit of every dependency's
optional audio features.

## Baseline evidence

- Source branch: feat/split-game-module; clean at expected HEAD.
- Isolated worktree: SnowboardKids-Recomp-ui, branch feat/sbk-frontend-theme.
- CTest: 9/9 using build-split test registrations copied to /tmp, retaining
  absolute executable paths and keeping CTest logs out of the split worktree.
- Release Python tests: 15/15. Module builder unit tests: 11/11.
- Frontend: policy/structure tests pass with ASAN_OPTIONS=detect_leaks=0;
  default invocation fails in LeakSanitizer due to ptrace in this environment.
  ASan/UBSan remain enabled. Leak checking remains unverified.
- Existing dist/SnowboardKidsEngine-linux-x86_64.zip: existing artifact audit PASS.
  That scanner does not establish absence of unknown-license theme icons.
- git diff --check: PASS. No live game or visual baseline captured yet.
