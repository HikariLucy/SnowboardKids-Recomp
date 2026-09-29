# SnowboardKids-Recomp

Native PC recompilation/port of **Snowboard Kids (Nintendo 64, USA)** built on the modern N64 recompilation ecosystem.

> **No ROM is included or downloaded by this project.** You must provide your own supported Snowboard Kids (USA) ROM dump.

## Status

**Core PC port: complete and playable — `v0.9.0-beta` release candidate.**

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

The remaining roadmap work is primarily release packaging, Windows live validation and post-launch enhancements, not basic playability.

**First public release: `v0.9.0-beta` (Linux x86-64 first).**

## Quick start

### Recommended: release build

For the public beta:

1. Download the Linux x86-64 archive from **GitHub Releases**.
2. Extract it to a normal writable folder.
3. Launch **SnowboardKidsEngine**.
4. Select your own supported Snowboard Kids (USA) ROM when prompted.
5. The engine validates the ROM and starts the bundled reviewed game module.
6. Future launches remember the selected ROM path.

The public beta does **not** require a compiler, Python setup, ROM extraction or
a separate build step. It still requires the user's original supported ROM.

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

SnowboardKids-Recomp keeps the host engine and recompiled game module separate:

```text
SnowboardKidsEngine
        |
        +--> bundled SnowboardKidsGame module
        |
        v
Choose your own ROM
        |
        v
Validate supported ROM
        |
        v
Play
```

The `v0.9.0-beta` archive contains a reviewed precompiled game module so normal
players do not need a compiler. The module contains recompiled game logic but
the release contains **no ROM and no extracted commercial game assets**.

The original local module builder remains available for development and recovery
workflows. See [RUNNING.md](RUNNING.md).

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

Windows is **in development**: CI also builds the ROM-free `SnowboardKidsEngine.exe` and exercises the Windows game-module binding with a synthetic module. No Windows release exists yet; a live Windows gameplay validation is still required before Windows is described as supported. Status and build notes: [`docs/WINDOWS.md`](docs/WINDOWS.md).

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

Project-authored source is released under **GNU GPL version 3**; see [LICENSE](LICENSE).

N64ModernRuntime is GPL-3.0 licensed. Corresponding-source/build directions for
the engine are documented in [SOURCE-COMPLIANCE.md](SOURCE-COMPLIANCE.md).

The pinned RecompFrontend repository currently has **no top-level license grant**.
Clarification is pending in
[N64Recomp/RecompFrontend#44](https://github.com/N64Recomp/RecompFrontend/issues/44).
The public beta proceeds with that uncertainty explicitly disclosed rather than
claiming a license that upstream has not stated.

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and
[docs/BETA-DISTRIBUTION-POLICY.md](docs/BETA-DISTRIBUTION-POLICY.md) before
redistributing a binary build.

## Documentation

- [Running the game](RUNNING.md)
- [Roadmap](docs/ROADMAP.md)
- [Current status](docs/STATUS.md)
- [Release engineering](docs/RELEASE-ENGINEERING.md)
- [Controller Pak persistence](docs/CONTROLLER-PAK-PERSISTENCE.md)
- [Original save-data map](docs/SAVE-DATA-MAP.md)
- [License audit](docs/LICENSE-AUDIT.md)
- [Public beta distribution policy](docs/BETA-DISTRIBUTION-POLICY.md)
- [Corresponding source](SOURCE-COMPLIANCE.md)

## Disclaimer

SnowboardKids-Recomp is an unofficial preservation and reverse-engineering project.

Snowboard Kids and related trademarks, game code, artwork, audio and other original game content belong to their respective rights holders. This repository does not provide the game ROM and is not affiliated with or endorsed by the original developers, publishers, Nintendo, Atlus or Racdym.
