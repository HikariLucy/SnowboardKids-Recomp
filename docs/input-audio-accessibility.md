# Controls, audio, and accessibility behavior

## Controls

The Controls tab initially shows the single-player **controller** profile. Use the controller/keyboard toggle in its footer before selecting a binding when remapping a keyboard key. To bind `P` to the second D-Pad Right slot, select keyboard, focus that slot, activate capture, then press `P`. The capture consumes the key. A controller press or axis motion from another device does not navigate the frontend while capture is active; release events still pass through to clear held navigation state. Escape cancels capture.

Bindings are saved to `controls.json` in user data when the Controls tab closes or changes. The persisted enum keys and guest button semantics are unchanged. Reset to defaults opens a confirmation with **Cancel** focused. Enter immediately cancels; reset requires an explicit selection of Reset.

The footer may show the virtual controller as the last active input source when a test pad sends events. That label affects menu hints; it does not change which profile is being edited. The footer toggle selects the profile.

## Audio

The Audio tab exposes only the actual mixed output stream. Master Volume is a host-side linear gain from 0 to 100 percent on samples queued to SDL. Zero percent is mute. The guest AI FIFO byte counts, resampler history, and savestate audio mirror use the original samples. Music and SFX are not separate streams and are not exposed separately. `sound.json` lives in user data; invalid values fall back to defaults.

## Accessibility

The approved theme is retained. Focus has a visible border and shape or size cue on interactive options. Selected tabs and options use a separate underline or weight cue. Device labels spell out keyboard, controller name, disconnected, and unassigned states. Reduced Motion stops menu pulses, decorative spins, and smooth scrolling without changing gameplay; it persists in `accessibility.json` in user data. UI scale is not exposed because the frontend has no validated scale backend.

## Verification

The Linux CTest suite includes `input_capture_events`, `audio_host_gain`, `compat_audio_progress`, `ui_ux_config`, graphics configuration, first-run layout, PFS, and quiescence tests. `ui_ux_config` verifies process A save to process B reload and malformed-file fallback under ASan/UBSan. Physical controller behavior and the Model D race overlay flow remain separate live gates.
