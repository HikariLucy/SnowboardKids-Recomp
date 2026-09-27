# Input capabilities and LIVE gate

The Controls tab edits the assigned profile. In the tested four-player layout,
Player 1 showed **Keyboard (SP)**; Edit Profile opened that keyboard profile.
The `SBK_UX_TRACE=1` run selected **D-Pad Right**, second slot, captured `P`
(scancode 19), and showed `P` in that slot. Capture committed without changing
tabs, resuming, or closing the overlay. Leaving Controls saved the second
`DPAD_RIGHT` entry to `controls.json` in user data. Process B loaded the same
profile and displayed `P` in that slot. The UI reads the backend profile, so
this also confirms backend profile reload. The guest action for `P` was not
measured in that run: its race-navigation probe overrides port 1 input.

Reset opens a confirmation focused on **Cancel**, including when opened with
a mouse. Immediate Enter cancels and preserves the binding. After explicitly
moving focus to **Reset**, Enter restores the default empty second slot; leaving
Controls writes that result to `controls.json`.

Keyboard navigation selected tabs, moved through binding rows one focus event
at a time, edited Audio and Accessibility, and resumed the race. A virtual SDL
controller opened and navigated the overlay, changed and restored a volume
value, returned focus with B, and closed the overlay with Back. Physical
controller behavior and rumble have no LIVE pass from this gate.

`SBK_UX_TRACE=1` writes state transitions to stderr for local diagnosis.
It is silent by default and does not log per frame. Screenshots and local ROM
or user data remain outside the repository.
