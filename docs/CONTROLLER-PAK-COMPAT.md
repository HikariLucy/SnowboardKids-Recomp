# Controller Pak compatibility (USA ROM)

Source inventory: `snowboardkids-decomp` commit
`03088d8c5164644c7697846770b091d5265e53de`.
This records the original-game PFS surface and the port's current NOPACK
policy. Native `.sbks` savestates are host snapshots, not original Controller
Pak files.

| Dependency | Classification | Evidence | Current effect |
| --- | --- | --- | --- |
| Enter a one-player race | OPTIONAL FEATURE | Five default navigation runs reached `race_active` after the Pak warning | Gameplay can start without a Pak |
| Load/save `GameSaveData` for each player | PERSISTENCE ONLY | `controller_main_menu_flow.c` probes, finds, reads and writes `gGameSaveDataBuffer[controllerIndex]` | No durable original-game save |
| Course/character progression across launches | PERSISTENCE ONLY | `race_setup_menu.c` initializes `courseUnlockStates`, `characterFlags`, `progressionLevel` on the no-save path | Unlocks and records do not survive a fresh process without a Pak |
| Race records and ghost replay files | PERSISTENCE ONLY | `main_menu_scene_model.c` stores replay data in `GameSaveData`; `race_flow.c` has ghost replay route | Durable replay data unavailable |
| Optional per-player Rumble Pak | OPTIONAL FEATURE | `controller_subsystem.c` uses `osMotorInit/Start/Stop`; `native_boot.cpp` reports Rumble Pak for connected ports | P1 rumble was previously observed; P2–P4 physical rumble remains untested |
| Alternate Pak management screens | UNKNOWN | `menu/controller_pak/` and save/repair/delete call sites exist | End-to-end screen behavior untested |

## PFS call map

`menu/main_menu/controller_main_menu_flow.c` calls `osPfsInitPak`,
`osPfsFindFile`, `osPfsNumFiles`, `osPfsFreeBlocks`,
`osPfsReadWriteFile`, `osPfsAllocateFile`, `osPfsRepairId`,
`osPfsFileState` and `osPfsDeleteFile`. The host implementations in
`N64ModernRuntime/librecomp/src/pak.cpp` return `PFS_ERR_NOPACK` (1) for
the PFS operations. The guest's Rumble Pak path is separate:
`ultramodern/src/input.cpp` handles `osMotorInit` from the reported device
and Pak type.

The diagnostic navigation saw `controller_pak`, then `save_select` and
`rumble_prompt`, then `character_select`. This establishes that the default
no-Pak path advances. It does not validate every error/repair screen or
the long-term progression loop. PFS persistence is outside this mission;
changing the Pak result to fake success would create false save guarantees.
