# Audio capabilities and LIVE gate

Audio exposes one mixed stream and **Master Volume** from 0 to 100 percent.
The gain applies to host output samples; it does not change the guest AI FIFO.
There are no separate Music or SFX controls.

In a race-active Linux run, the UI changed 100% → 50% → 0% → 35%. The opt-in
trace reported host gains 0.50, 0.00, and 0.35, and the race continued with
zero continuation fallbacks. Leaving Audio saved `sound.json` with 35; process B
loaded the same value and host gain. Keyboard and virtual controller navigation
also changed 35 → 36 → 35.

The ROM-free `audio_host_gain`, `compat_audio_progress`, and config tests pass.
`compat_audio_progress` executes its assertions in Release builds by undefining
`NDEBUG`. Malformed `sound.json` values use tested safe defaults. Subjective
loudness and a physical speaker mute measurement were outside this gate.
