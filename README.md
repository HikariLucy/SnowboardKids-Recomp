# SnowboardKids-Recomp

Native PC recompilation/port of **Snowboard Kids (Nintendo 64, USA)** built on the modern N64 recompilation ecosystem.

> **No ROM is included or downloaded by this project.** You must provide your own supported Snowboard Kids (USA) ROM dump.

## Status

**Core PC port: complete and playable — public beta candidate.**

The current build has been validated in real play sessions, not only automated tests:

- Full race start → finish → results → next race: **PASS**
- Official in-game save → close game → reopen → saved progress restored: **PASS**
- Savestates with **F5 / F8**, including restore after closing and reopening the game: **PASS**
- Keyboard play: **PASS**
- Physical controller play: **PASS**
- Physical rumble: **PASS**
- Controller remapping and persistence: **PASS**
- Master Volume and accessibility settings: **PASS**
- Controller Pak persistence: **PASS**
- Linux live runtime: **PASS**
- Linux and Windows ROM-free CI: **GREEN**
- Windows D3D12/VSync renderer stack: **COMPILE PASS**; live Windows validation is still pending

The remaining roadmap work is primarily release/legal packaging and post-launch enhancements, not basic playability.

**First public release target: `v0.9.0-beta`.**

## Quick start

### Recommended: release build

Once `v0.9.0-beta` is published:

1. Download the release archive for your platform from **GitHub Releases**.
2. Extract it to any normal writable folder.
3. Launch **SnowboardKidsEngine**.
   - Linux: double-click the executable after marking it executable if your desktop asks.
   - Windows: double-click `SnowboardKidsEngine.exe` once the Windows release build is published.
4. On first launch, the setup window asks you to choose your own supported Snowboard Kids (USA) ROM.
5. The engine validates the ROM and builds the local game module on your computer.
6. When generation finishes, the game starts automatically.
7. Future launches reuse that local module; you do not need to rebuild it every time.

You do **not** need to pass the ROM on the command line for normal use.

### Supported ROM

The initial public beta intentionally supports one verified Snowboard Kids (USA) corpus.

Expected normalized SHA-1:

```text
1583bacc9046a360df8ea4d536942155247e154c
```

Game code:

```text
NSKE
```

The first-run builder accepts `.z64`, `.v64`, and `.n64` byte-order variants when they normalize to the supported dump.

A filename or matching game ID alone is **not** sufficient. Unsupported revisions/regions are rejected instead of being run with incorrect addresses.

## First-run architecture

SnowboardKids-Recomp uses a split **Model D** architecture:

```text
SnowboardKidsEngine
        |
        |  first launch
        v
Choose your own ROM
        |
        v
Validate + locally generate module
        |
        v
SnowboardKidsGame.so / SnowboardKidsGame.dll
        |
        v
Play
```

The distributed engine contains:

- no ROM
- no extracted commercial game assets
- no pre-generated game module
- no generated `RecompiledFuncs`

The local module is generated only on the user's machine from the user's supported ROM.

## Playing

Open the in-game PC settings overlay with:

```text
Escape
```

Savestates:

```text
F5  Quick Save
F8  Quick Load
```

Controls can be remapped from the **Controls** page. Keyboard and controller profiles persist between sessions.

The PC frontend currently includes:

- Windowed and Borderless modes
- Original 4:3 and Expand aspect modes
- Internal resolution scaling
- MSAA
- HUD placement
- VSync On/Off where supported
- Keyboard/controller remapping
- Multiplayer input foundation
- Master Volume
- Reduced Motion
- Savestates
- Original Controller Pak persistence
- Physical controller rumble

See [RUNNING.md](RUNNING.md) for complete controls, user-data locations and troubleshooting.

## Saves and user data

The port keeps PC configuration, Controller Pak data, savestates and locally generated modules outside the installation directory.

Typical locations:

- Linux: `~/.local/share/SnowboardKids`
- Windows: `%APPDATA%\SnowboardKids`

Savestates use `.sbks`.

Controller Pak persistence uses `.mpk`.

Savestates intentionally do **not** rewind Controller Pak storage. This keeps the original persistent save medium independent from temporary execution-state snapshots.

## Build from source

Normal players should use a release build. Building from source is mainly for contributors and development.

Clone the repository and bootstrap the exact pinned dependencies:

```bash
git clone https://github.com/HikariLucy/SnowboardKids-Recomp.git
cd SnowboardKids-Recomp
bash scripts/bootstrap.sh
```

For detailed Linux build requirements, ROM/module generation, development options and troubleshooting, see:

- [RUNNING.md](RUNNING.md)
- [Release engineering](docs/RELEASE-ENGINEERING.md)
- [Roadmap](docs/ROADMAP.md)

Public CI is ROM-free and never uploads or stores a commercial ROM.

## Platform status

### Linux

**Primary validated platform.**

The game has been played through full races with keyboard and a physical controller, with saving, rumble and savestates working in live use.

### Windows

The Windows ROM-free suite, MSVC/clang-cl Controller Pak tests, RT64, RecompFrontend and D3D12/VSync code all compile and pass in GitHub Actions.

A live Windows gameplay validation is still required before a Windows binary is described as fully verified.

## Project scope

The PC port is considered playable for the first public beta. Features such as the following are **post-release roadmap work**, not blockers for opening the repository:

- true widescreen / ultrawide
- higher-framerate feasibility
- mod support and mod templates
- formal performance profiling and later optimization
- additional ROM revisions/regions
- further accessibility polish
- Android ARM64/Vulkan
- mobile/touch controls

See [docs/ROADMAP.md](docs/ROADMAP.md).

## Development and CI

The project has ROM-free automated coverage for major host/runtime systems including:

- Controller Pak persistence
- cross-process persistence
- savestate codec/restore
- input/deadzone behavior
- audio progress and host gain
- configuration persistence
- frontend/navigation
- renderer geometry/lifecycle
- release artifact auditing
- continuation/runtime infrastructure

Generated game code, ROMs, saves and local user configuration must never be committed.

## Upstream projects

This project builds on work from the N64 recompilation community, including:

- [N64Recomp](https://github.com/N64Recomp/N64Recomp)
- [RecompFrontend](https://github.com/N64Recomp/RecompFrontend)
- [Snowboard Kids matching decompilation](https://github.com/cdlewis/snowboardkids-decomp)
- RT64 / N64ModernRuntime and their transitive dependencies

Exact revisions are pinned in `scripts/dependency_lock.py`.

## License and redistribution status

The source repository is being prepared for public release.

N64ModernRuntime is GPL-3.0 licensed, and binary distribution must satisfy its corresponding-source obligations. Most other dependencies are already inventoried in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

An explicit license clarification for RecompFrontend is still pending. Until that is resolved, this project should **not publish a public binary release that redistributes RecompFrontend**.

This licensing work does not affect local development or gameplay testing.

## Documentation

- [Running the game](RUNNING.md)
- [Roadmap](docs/ROADMAP.md)
- [Current status](docs/STATUS.md)
- [Release engineering](docs/RELEASE-ENGINEERING.md)
- [Controller Pak persistence](docs/CONTROLLER-PAK-PERSISTENCE.md)
- [Original save-data map](docs/SAVE-DATA-MAP.md)
- [License audit](docs/LICENSE-AUDIT.md)

## Disclaimer

SnowboardKids-Recomp is an unofficial preservation and reverse-engineering project.

Snowboard Kids and related trademarks, game code, artwork, audio and other original game content belong to their respective rights holders. This repository does not provide the game ROM and is not affiliated with or endorsed by the original developers, publishers, Nintendo, Atlus or Racdym.
