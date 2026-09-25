# Running Snowboard Kids Recompiled (Model D Architecture)

This distribution contains the standalone, ROM-free game engine (`SnowboardKidsEngine`).
It contains **no ROM data**, **no commercial game assets**, and **no precompiled game modules**.

You supply your own legally dumped **Snowboard Kids (USA)** ROM image (`.z64`, `.v64`, `.n64`).
Supported ROM SHA-1: `1583bacc9046a360df8ea4d536942155247e154c`.

---

## 1. First-Run & Local Module Generation

On first startup, the engine detects that no game module is installed:
1. Run `./SnowboardKidsEngine`.
2. A file selection dialog will prompt you to select your legal Snowboard Kids (USA) ROM. (Alternatively, pass the ROM directly on the command line: `./SnowboardKidsEngine /path/to/snowboardkids.z64`).
3. The engine validates the ROM integrity and compiles the local dynamic game module (`SnowboardKidsGame.so` on Linux, `SnowboardKidsGame.dll` on Windows) into your user data directory (`~/.local/share/SnowboardKids/modules/snowboardkids-us/`).
4. Once compiled and validated, the game starts automatically!

### CLI / Offline Builder
You can also generate the game module ahead of time:
```bash
python3 scripts/build-game-module.py /path/to/snowboardkids.z64
# or:
./SnowboardKidsEngine --build-module /path/to/snowboardkids.z64
```

### Subsequent Launches
On subsequent launches, the engine detects the installed game module in user data and loads it immediately **without rebuilding**.

---

## 2. User Data, Savestates, and Controller Pak

- **User Data Directory**: Configurations, controller profiles, game modules, and savestates are stored in:
  - Linux: `~/.local/share/SnowboardKids` (or `$XDG_DATA_HOME/SnowboardKids`)
  - Windows: `%APPDATA%\SnowboardKids`
  - Override path by setting the `SBK_USER_DATA_DIR` environment variable.
- **Savestates (`.sbks`)**: Press **F5** to quick-save, **F8** to quick-load. Savestate files store guest execution state, CPU registers, RAM, and continuation frames. Rebuilding the game module from the same ROM and compatible corpus preserves full savestate compatibility.
- **Controller Pak (`.mpk`)**: Controller Pak persistence files live in `<user-data>/` and represent external physical memory cards. **Savestates do NOT serialize or rewind external Controller Pak media.** When an `.sbks` state is restored, the Controller Pak file on disk remains intact and is never rewound.

---

## 3. Host Requirements

- **Graphics**: Vulkan 1.2+ capable GPU and drivers.
- **Audio / Input**: SDL2 runtime libraries.
- **Local Module Builder**:
  - Python 3.8+
  - C++20 compiler (`clang++` or `g++` on Linux; Visual Studio C++ / `clang-cl` on Windows).
