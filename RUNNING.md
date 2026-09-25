# Running Snowboard Kids Recompiled

This archive never contains a ROM. Supply your own legally obtained
**Snowboard Kids (USA)** ROM. Supported identity: game code `NSKE`, expected
USA dump SHA-1 `1583bacc9046a360df8ea4d536942155247e154c`.

Extract the whole archive, then run `SnowboardKidsRecompiled` (or the Windows
`.exe`). With no argument, select your ROM in the file dialog. You can also
pass its absolute path. `--help` and `--version` work without a ROM or graphics.
The app validates ROM contents and rejects the wrong game or revision.

Configs, savestates and Controller Pak files live in the frontend user-data
folder (`~/.config/snowboardkids-recompiled` on Linux; Local AppData on
Windows). Set `SBK_USER_DATA_DIR` to an absolute path to override it. F5 saves
a quick savestate and F8 loads it. Controller Pak `.mpk` data is independent of
savestates and is not rewound by F8. The Controls page supports assignment and
remapping; a keyboard can control player 1.

If Vulkan startup fails, install a Vulkan-capable driver. On Linux the host
also needs SDL2, GTK3 and their runtime libraries; use `ldd` on the executable
to identify a missing library. If audio cannot open, check the system audio
device. For an invalid ROM, verify that it is the USA game and not a different
region. If user data cannot be written, check permissions or set
`SBK_USER_DATA_DIR` to a writable directory. A corrupt Controller Pak needs
manual backup/repair; do not delete your `.mpk` without preserving a copy.

This is a development candidate. Full gameplay and physical-controller gates
remain pending; see the source repository compatibility documents.
