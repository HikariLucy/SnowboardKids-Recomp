<div align="center">

# Snowboard Kids Recompiled

**Native PC recompilation of Snowboard Kids (Nintendo 64)**<br>
Powered by **N64Recomp**, **RT64**, **RecompFrontend**, and **N64ModernRuntime**.

[![Release](https://img.shields.io/github/v/release/HikariLucy/SnowboardKids-Recomp?include_prereleases&label=release)](https://github.com/HikariLucy/SnowboardKids-Recomp/releases)
[![ROM-free CI](https://github.com/HikariLucy/SnowboardKids-Recomp/actions/workflows/ci.yml/badge.svg)](https://github.com/HikariLucy/SnowboardKids-Recomp/actions/workflows/ci.yml)
[![License](https://img.shields.io/badge/license-GPL--3.0-blue.svg)](LICENSE)
![Linux](https://img.shields.io/badge/Linux-beta-success)
![Windows](https://img.shields.io/badge/Windows-beta-success)

**[Download v0.9.0-beta.1](https://github.com/HikariLucy/SnowboardKids-Recomp/releases/tag/v0.9.0-beta.1)** ·
[Quick Start](#quick-start) ·
[Roadmap](docs/ROADMAP.md) ·
[Running Guide](RUNNING.md) ·
[Report an Issue](https://github.com/HikariLucy/SnowboardKids-Recomp/issues)

</div>

> [!IMPORTANT]
> **No ROM is included or downloaded by this project.** You must provide your own supported Snowboard Kids (USA) ROM dump.

## What is Snowboard Kids Recompiled?

Snowboard Kids Recompiled is a native PC recompilation project for **Snowboard Kids (Nintendo 64, USA)**.

The goal is to make the original game run as a modern native PC application while preserving the original game logic and adding a cleaner desktop experience around it: modern rendering, configurable input, Controller Pak persistence, savestates, frontend settings, and reproducible release packaging.

This is **not an emulator distribution** and the repository does not provide the commercial game ROM or extracted commercial assets.

## Current status

| Platform | Status | Notes |
| --- | --- | --- |
| **Linux x86_64** | ✅ Public beta | Live gameplay validated; refreshed package included in <code>v0.9.0-beta.1</code> |
| **Windows x86_64** | ✅ Public beta | Real game module, packaging and physical gameplay validated; included in <code>v0.9.0-beta.1</code> — see [docs/WINDOWS.md](docs/WINDOWS.md) |

The Linux beta has been validated in real play sessions, including:

- Full race start → finish → results → next race
- Official in-game save → close → reopen → progress restored
- Quick savestates with **F5 / F8**
- Keyboard gameplay
- Physical controller gameplay
- Physical rumble
- Controller remapping and persistence
- Master Volume and accessibility settings
- Controller Pak persistence

Windows x86_64 has completed the same release path: the ROM-free engine and renderer stack build in CI, a real <code>SnowboardKidsGame.dll</code> validates against the engine ABI, the audited package runs from a clean extracted directory, and physical gameplay has been verified with D3D12/RT64, audio, keyboard, controller, rumble, official save persistence and F5/F8 savestates. Engineering evidence and build notes: [docs/WINDOWS.md](docs/WINDOWS.md).

## Quick start

### Public beta

1. Download **[v0.9.0-beta.1](https://github.com/HikariLucy/SnowboardKids-Recomp/releases/tag/v0.9.0-beta.1)**.
2. Extract the package for your platform:
   - <code>SnowboardKidsRecompiled-0.9.0-beta.1-Linux-x86_64.zip</code>
   - <code>SnowboardKidsRecompiled-0.9.0-beta.1-Windows-x86_64.zip</code>
3. Run <code>./SnowboardKidsEngine</code> on Linux or <code>SnowboardKidsEngine.exe</code> on Windows.
4. Select your own supported **Snowboard Kids (USA)** ROM when prompted.
5. The engine validates the ROM and launches the bundled reviewed game module.

The public beta does **not** require a compiler, Python setup, ROM extraction, or a separate build step.

## Highlights

- Native executable runtime
- RT64-powered rendering
- Split **engine + game module** architecture
- Local ROM validation
- Controller Pak persistence
- Cross-session saves
- Quick save / quick load
- Keyboard and controller input
- Controller remapping
- Physical rumble
- Windowed and borderless modes
- Original 4:3 and expanded aspect modes
- Internal resolution scaling
- MSAA
- HUD placement controls
- VSync where supported
- Master Volume
- Reduced Motion accessibility setting
- ROM-free Linux/Windows CI
- Deterministic public-beta packaging and artifact auditing

## First-run architecture

The host engine and recompiled game logic are intentionally kept separate:

~~~text
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
~~~

The public beta contains a reviewed precompiled game module so normal players do not need to compile anything.

The release contains **no ROM and no extracted commercial game assets**.

Developers can still rebuild the module locally. See [RUNNING.md](RUNNING.md).

## Supported ROM

The initial beta intentionally supports one verified **Snowboard Kids (USA)** corpus.

Normalized SHA-256:

~~~text
58870ea67d49f778e7a7607eb270ad1d3a081a4733b337b2d607de2606dcfb3c
~~~

Normalized SHA-1:

~~~text
1583bacc9046a360df8ea4d536942155247e154c
~~~

Game code:

~~~text
NSKE
~~~

Supported input formats:

- <code>.z64</code>
- <code>.v64</code>
- <code>.n64</code>

Byte-order variants are normalized before validation. A matching filename or game ID alone is not enough; unsupported revisions and regions are rejected. See [ROM-IDENTITY.md](ROM-IDENTITY.md) for the complete canonical identity and a local verification tool.

## Playing

Open the PC settings overlay with:

~~~text
Escape
~~~

Savestates:

~~~text
F5  Quick Save
F8  Quick Load
~~~

Controls can be remapped from the **Controls** page and persist between sessions.

For complete controls, user-data locations and troubleshooting, see [RUNNING.md](RUNNING.md).

## Saves and user data

PC configuration, Controller Pak data, savestates and locally generated modules live outside the installation directory.

Typical locations:

- Linux: <code>~/.local/share/SnowboardKids</code>
- Windows: <code>%APPDATA%\SnowboardKids</code>

Formats:

- Savestates: <code>.sbks</code>
- Controller Pak persistence: <code>.mpk</code>

Savestates intentionally do **not** rewind Controller Pak storage. The original persistent save medium remains independent from temporary execution-state snapshots.

## Roadmap

### Completed

- [x] Native playable Linux runtime
- [x] RT64 renderer integration
- [x] Modern frontend/settings foundation
- [x] Controller Pak persistence
- [x] Savestates
- [x] Physical controller and rumble support
- [x] Linux public beta packaging
- [x] First public release: <code>v0.9.0-beta</code>
- [x] Windows x86_64 real game-module (<code>SnowboardKidsGame.dll</code>) build and validation
- [x] Windows x86_64 distributable packaging
- [x] Live Windows gameplay validation
- [x] Cross-platform beta refresh: <code>v0.9.0-beta.1</code>

### In progress

- [ ] Broader full-game QA

### Later

- [ ] Additional graphics and UX enhancements
- [ ] Widescreen / ultrawide feasibility
- [ ] Higher-framerate feasibility
- [ ] Mod support and mod templates
- [ ] Additional ROM revisions / regions
- [ ] Performance profiling and optimization
- [ ] Further accessibility improvements
- [ ] Android ARM64 / Vulkan research

See the full [roadmap](docs/ROADMAP.md).

## Build from source

Normal players should use a release build. Building from source is mainly for contributors and development.

~~~bash
git clone https://github.com/HikariLucy/SnowboardKids-Recomp.git
cd SnowboardKids-Recomp
bash scripts/bootstrap.sh
~~~

For detailed build requirements and developer workflows:

- [Running and building](RUNNING.md)
- [Release engineering](docs/RELEASE-ENGINEERING.md)
- [Roadmap](docs/ROADMAP.md)
- [Current status](docs/STATUS.md)
- [Windows development status](docs/WINDOWS.md)

Public CI is ROM-free and never uploads or stores a commercial ROM.

## Development and CI

The project has ROM-free automated coverage for major host/runtime systems including:

- Controller Pak persistence
- cross-process persistence
- savestate codec and restore
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
- [RT64](https://github.com/rt64/rt64)
- [N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime)
- [RecompFrontend](https://github.com/N64Recomp/RecompFrontend)
- [Snowboard Kids matching decompilation](https://github.com/cdlewis/snowboardkids-decomp)

Exact revisions are pinned in <code>scripts/dependency_lock.py</code>.

## License and redistribution

Project-authored source is released under **GNU GPL version 3**. See [LICENSE](LICENSE).

N64ModernRuntime is GPL-3.0 licensed. Corresponding-source and build directions for the engine are documented in [SOURCE-COMPLIANCE.md](SOURCE-COMPLIANCE.md).

The pinned RecompFrontend repository currently has **no top-level license grant**. Clarification is pending in [N64Recomp/RecompFrontend#44](https://github.com/N64Recomp/RecompFrontend/issues/44).

The public beta proceeds with that uncertainty explicitly disclosed rather than claiming a license upstream has not stated.

Before redistributing a binary build, see:

- [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)
- [Public beta distribution policy](docs/BETA-DISTRIBUTION-POLICY.md)
- [Corresponding source](SOURCE-COMPLIANCE.md)

## Documentation

- [Running the game](RUNNING.md)
- [Supported ROM identity](ROM-IDENTITY.md)
- [Launcher integration contract](LAUNCHER-INTEGRATION.md)
- [Roadmap](docs/ROADMAP.md)
- [Current status](docs/STATUS.md)
- [Windows development status](docs/WINDOWS.md)
- [Release engineering](docs/RELEASE-ENGINEERING.md)
- [Controller Pak persistence](docs/CONTROLLER-PAK-PERSISTENCE.md)
- [Original save-data map](docs/SAVE-DATA-MAP.md)
- [License audit](docs/LICENSE-AUDIT.md)
- [Public beta distribution policy](docs/BETA-DISTRIBUTION-POLICY.md)
- [Corresponding source](SOURCE-COMPLIANCE.md)

## Disclaimer

Snowboard Kids Recompiled is an unofficial preservation and reverse-engineering project.

Snowboard Kids and related trademarks, game code, artwork, audio and other original game content belong to their respective rights holders. This repository does not provide the game ROM and is not affiliated with or endorsed by the original developers, publishers, Nintendo, Atlus or Racdym.
