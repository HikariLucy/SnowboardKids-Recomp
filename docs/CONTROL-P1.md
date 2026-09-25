# CONTROL-P1: modern gamepad input

Status: code-side PASS. Live (human, physical controller) gate pending.

## Pipeline

```text
SDL2 GameController (any SDL-mapped pad: Xbox, DualShock/DualSense, Switch Pro)
  -> RecompInput bindings (runtime-data/controls.json, profiles per device/player)
  -> profiles::get_n64_input: buttons + stick in [-1, 1], radial deadzone
  -> native_boot get_input -> ultramodern osContGetReadData:
     convert_to_n64_range (radial magnitude onto the N64 octagon, max ~82)
  -> librecomp osContGetReadData_recomp: OSContPad[4] in guest RAM
  -> game: updateControllerInputState -> updateGameTaskScheduler:
     clamp raw stick to +-45, gAnalogStickResponseCurve (0..7 dead, max 31),
     direction flags at +-27 -> race player stickX/stickY
```

No vendor or product is hardcoded anywhere on this path. Devices are SDL game
controllers, and per-device profiles are keyed by SDL GUID.

## Analog audit

A virtual SDL game controller (`SBK_TEST_PAD_FILE`, `src/main/virtual_pad.cpp`)
drives the real path. The guest's pad bytes and the game's mapped stick were
read from RDRAM in a race and at the title:

| Host | Guest stick | Game stickX/Y | Flags |
| --- | --- | --- | --- |
| neutral | 0, 0 | 0, 0 | none |
| full right / left | 81 / -82 | 31 / -31 | right / left |
| full up / down | 82 / -81 (Y) | 31 / -31 | up / down |
| half right | 38 | 25 | none |
| diagonal | 69, 69 | 31, 31 | right + up |

The analog path has no functional break. With a standard SDL controller, the
rider steers, including from a race started by navigating the menus with the
virtual controller.

What was wrong or questionable:

- The frontend deadzone was per axis (square). It snapped or bent small
  diagonals toward an axis and let direction change with magnitude. It is now
  radial (`recompinput/deadzone.h`, `patches/recompfrontend-input.patch`):
  neutral inside the deadzone, direction kept, magnitude rescaled, never
  outside the unit circle.
- The game saturates at raw 45, about 55% of a full N64 throw, and treats raw
  < 8 as neutral. This is the original game's response curve, left as is.
  If the physical stick still feels too twitchy, that is a sensitivity choice
  (for example an outer scale). It is a separate decision, not a bug.

The exact symptom seen on the user's Xbox controller was not reproduced
without the physical device. The human gate below checks it.

## Remapping

RecompFrontend's remapper is present and works (verified live with the
virtual controller):

1. Options (Esc / controller Back) -> Controls. Player 1 defaults to the
   keyboard, so "Edit Profile" edits `Keyboard (SP)`.
2. "Re-assign Players" -> press any button on the controller -> Confirm.
   Player 1 becomes the controller with profile `Controller (SP)`, the
   profile single-player input reads.
3. "Edit Profile" -> choose a slot -> press the new button.
4. The change applies immediately. Closing the Controls tab saves
   `runtime-data/controls.json` (`recomp::get_config_path()`), and a new
   process loads it. Verified: Y added to N64 A produced A in the guest,
   before and after a restart.

The only usability gap is discoverability: controller bindings are editable
only after Re-assign Players. No new UX was added.

## Rumble

The game initializes rumble with `osMotorInit` at startup and race start, and
pulses it through `osMotorStart/Stop` (race events). N64ModernRuntime forwards
motor access to `recompinput::set_rumble`, which ramps `SDL_JoystickRumble`.
The chain was cut at the first link: this program reported no pak, so
`osMotorInit` returned `PFS_ERR_NOPACK`. It now reports a Rumble Pak on
controller 1 (librecomp still answers every Controller Pak call with NOPACK,
so saves are unaffected). Boot and menus are unchanged. In a fresh race,
driven by the virtual controller, the game's rumble reached SDL as 100
ramped `SDL_JoystickRumble` requests. Strength follows General -> Rumble
Strength.

A race restored from a save made without a Rumble Pak keeps its saved motor
status until the next race start.

## Hotplug

Disconnecting the controller while holding right + A releases every button
and centers the stick in the guest. Reconnecting resumes input. SDL device
add/remove is handled by RecompInput; no new framework was added.

## Multi-controller foundation (audit only)

RecompInput tracks each player's device (`players`), per-device profiles by
GUID, and a player-assignment flow (the remapper above). There is no global
single-controller singleton. One blocker for future local multiplayer is in
this program: `native_boot` reports only port 1 as connected
(`get_connected_device_info`), so the game sees one controller. Enabling P2-P4
means reporting the assigned players' ports. That changes what the guest sees
and belongs to the multiplayer phase.

## Tests

- `python3 tests/controls/run.py` (no device, ASan + UBSan): the real radial
  deadzone and the runtime's real `convert_to_n64_range` object. Covers
  neutral, noise, cardinals, diagonal on the octagon, no sqrt(2) overflow,
  partial magnitude, small-diagonal direction, bounds and NaN.
- `python3 tests/controls/run_live.py [--workdir DIR]` (ROM + window, virtual
  controller at the title): cardinals, partial, diagonal, release, A / B / D-pad /
  Start, disconnect-while-held neutralization and reconnect.

## Human gate (physical controller)

```sh
cd build-renderer-stack && ./SnowboardKidsRecompiled runtime-data/snowboardkids.n64.us.z64
```

1. Connect the Xbox controller. Use it on the title and menus.
2. Start a race. Steer with the left stick: full, gentle partial, diagonals.
   Release: the rider goes straight.
3. A / B / Start / D-pad behave as before.
4. Options -> Controls -> Re-assign Players -> press A on the controller ->
   Confirm -> Edit Profile -> remap one action -> close the menu -> verify.
5. Quit, start again, verify the remap persists.
6. In a race, hit obstacles or other riders and feel for rumble. Rumble
   Strength is in General.
