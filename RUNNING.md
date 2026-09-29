# Running Snowboard Kids Recompiled

The public beta archive contains `SnowboardKidsEngine`, reviewed frontend assets
and a precompiled `SnowboardKidsGame` module. It contains **no ROM** and **no
extracted commercial game assets**.

You supply your own legally dumped **Snowboard Kids (USA)** ROM image
(`.z64`, `.v64`, `.n64`). Supported normalized ROM SHA-1:
`1583bacc9046a360df8ea4d536942155247e154c`.

---

## 1. First run

1. Extract the release archive.
2. Run `./SnowboardKidsEngine` (Linux) or `SnowboardKidsEngine.exe` (Windows
   development builds only — Windows is not a supported release platform yet).
3. Select your own supported Snowboard Kids (USA) ROM when prompted.
4. The engine validates the ROM and the bundled game module, then starts the game.
5. The selected ROM path is remembered in your user-data directory.

The beta package does **not** require a compiler or a local recompilation step.

### Module override / developer builder

A module installed in user data takes precedence over the release-bundled
module. Developers can rebuild one with:

```bash
python3 scripts/build-game-module.py /path/to/snowboardkids.z64
# or:
./SnowboardKidsEngine --build-module /path/to/snowboardkids.z64
```

That development path requires the local builder inputs/toolchain documented in
the repository. Normal beta users do not need it.

---

## 2. User Data, Savestates, and Controller Pak

- **User Data Directory**: Configurations, controller profiles, game modules, and savestates are stored in:
  - Linux: `~/.local/share/SnowboardKids` (or `$XDG_DATA_HOME/SnowboardKids`)
  - Windows: `%APPDATA%\SnowboardKids`
  - Override path by setting the `SBK_USER_DATA_DIR` environment variable.
- **Savestates (`.sbks`)**: Press **F5** to quick-save, **F8** to quick-load. Savestate files store guest execution state, CPU registers, RAM, and continuation frames. Rebuilding the game module from the same ROM and compatible corpus preserves full savestate compatibility.
- **Controller Pak (`.mpk`)**: Controller Pak persistence files live in `<user-data>/` and represent external physical memory cards. **Savestates do NOT serialize or rewind external Controller Pak media.** When an `.sbks` state is restored, the Controller Pak file on disk remains intact and is never rewound.


### Graphics: VSync

Settings → Graphics → **VSync** synchronizes frame presentation with the
display. **On** is the default. **Off** is only offered when your display and
driver support presenting without waiting for the display; otherwise it is
greyed out. Changes apply immediately with **Apply**. VSync does not change
the game's speed or timing.

### Controls, Audio, and Accessibility

Open the overlay with **Escape** or a controller's Back button. Controls edits
an assigned input profile; bindings and player assignments are saved to
`controls.json` in user data when leaving Controls. Reset asks for confirmation
with **Cancel** focused by default.

Audio exposes mixed-stream **Master Volume**. Zero percent mutes host output;
the game's own audio processing continues. Accessibility offers **Reduced
Motion** for menu decoration and smooth scrolling. The settings persist in
`sound.json` and `accessibility.json` in user data. Both tabs can be operated
with keyboard or controller navigation. UI scale and separate Music/SFX controls
are not offered.

For local diagnosis, `SBK_UX_TRACE=1` prints UI state changes to stderr. It is
silent by default. Physical-controller behavior depends on the attached device
and has not been validated by the virtual-controller tests.

---

## 3. Host Requirements

- **Graphics**: Vulkan 1.2+ capable GPU and drivers (Linux); Direct3D 12 by default on Windows.
- **Audio / Input**: SDL2 runtime libraries.
- **Normal public-beta playback**: no compiler is required.
- **Optional local module rebuilding**:
  - Python 3.8+
  - C++20 compiler (`clang++` or `g++` on Linux; Visual Studio C++ / `clang-cl` on Windows).
