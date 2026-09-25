# SnowboardKids-Recomp

Experimental native recompilation project for **Snowboard Kids (Nintendo 64, USA)**.

The goal is to study the original game, reuse public reverse-engineering knowledge from the matching decompilation, and build a native PC runtime using the modern N64 recompilation ecosystem.

> This repository does **not** distribute the original ROM, copyrighted game assets, or proprietary Nintendo/Atlus/Racdym code.

## Current status

**Phase 0 — reproducible decomp environment: COMPLETE**

Validated locally on Linux:

- Original dump normalized to big-endian `.z64`
- Expected SHA-1 verified
- Matching decompilation cloned and dependencies installed
- Assets extracted successfully
- Upstream project rebuilt successfully
- Rebuilt ROM verified byte-for-byte against the expected SHA-1

Expected USA ROM SHA-1:

```text
1583bacc9046a360df8ea4d536942155247e154c
```

Successful baseline:

```text
[ verify ]  Checking snowboardkids.sha1
build/snowboardkids.z64: OK
```

## Project direction

The intended pipeline is:

```text
Legally obtained Snowboard Kids ROM
        |
        v
Matching decompilation / symbols / asset maps
        |
        v
N64Recomp analysis and configuration
        |
        v
Native recompiled game code
        |
        v
Modern N64 runtime + renderer
        |
        v
Linux / Windows native executable
```

## Upstream research

- Snowboard Kids matching decompilation:
  https://github.com/cdlewis/snowboardkids-decomp
- N64Recomp:
  https://github.com/N64Recomp/N64Recomp

The decompilation project is a research reference and is **not** itself a PC port.

## Development rules

1. Never commit ROM files.
2. Never commit extracted copyrighted assets unless their redistribution is explicitly permitted.
3. Keep generated build outputs out of Git.
4. Document every baseline hash and upstream revision used.
5. Prefer reproducible scripts over manual binary edits.
6. Keep upstream decomp modifications separate from recomp-specific code where possible.

## Documentation

- [Current status](docs/STATUS.md)
- [Roadmap](docs/ROADMAP.md)
- [M1 feasibility](docs/M1-FEASIBILITY.md)
- [Controller Pak persistence and original save location](docs/CONTROLLER-PAK-PERSISTENCE.md)
- [Original save data map](docs/SAVE-DATA-MAP.md)

## Disclaimer

This is an unofficial preservation and reverse-engineering research project. Snowboard Kids and related trademarks/assets belong to their respective rights holders.

## Native port development build

The current port is code-side validated through boot, input, savestates,
audio and Controller Pak tests; full compatibility and several physical/live
gates remain open. ROMs and proprietary game data are not distributed. The
supported game is **Snowboard Kids (USA)**, game code `NSKE`; the supported
ROM identity is documented in [release engineering](docs/RELEASE-ENGINEERING.md).

On Linux, install Git, CMake >= 3.20, Ninja, Clang, Python 3, pkg-config and
development packages for SDL2, FreeType and GTK3. Build the matching USA ELF
locally from your legally obtained ROM, then set `SBK_ROM` and `SBK_ELF` to
absolute paths and run `bash scripts/build-release.sh`. This bootstraps exact
pinned dependencies and creates `build-release/SnowboardKidsRecompiled`.
Run it with your ROM path or with no argument to open the ROM selector.
`--help` and `--version` need no ROM. See [RUNNING.md](RUNNING.md) for controls,
savestates, Pak data locations and troubleshooting.

A ROM-free subset can run with `cmake -S . -B build-ci -G Ninja
-DSBK_ROM_FREE_CI=ON`, `cmake --build build-ci`, then
`ctest --test-dir build-ci --output-on-failure` after bootstrapping the pinned
runtime headers. Public CI does not use a ROM. Binary artifacts are blocked
until generated-input and distribution-rights issues in
[release engineering](docs/RELEASE-ENGINEERING.md) are resolved. Contributions
should preserve the pin/patch lock and keep user data out of commits. The
project license remains to be selected by the owner before public binary
release.
