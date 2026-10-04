# Public beta distribution policy

Updated: 2026-10-03

Snowboard Kids Recompiled `v0.9.0-beta.1` is an unofficial, noncommercial public
beta intended for preservation, interoperability and testing.

## What the release contains

The downloadable package contains the native PC engine, reviewed UI assets,
license/notice files and a precompiled Snowboard Kids game module for the
supported USA revision. The module contains statically recompiled game logic
and generated RSP code, but the package does **not** contain a ROM, extracted
textures, audio, music, levels or other game assets.

The user must supply their own supported Snowboard Kids (USA) ROM. The engine
validates the ROM before starting the game.

## Project license

Project-authored source is released under GNU GPL version 3 as provided in the
repository `LICENSE` file, except where a file or third-party component states
different terms.

N64ModernRuntime is GPL-3.0 licensed. The release page must link to the exact
SnowboardKids-Recomp source tag/commit and the repository retains the build
scripts, dependency pins and runtime patch series used to construct the engine.

## RecompFrontend status

The pinned RecompFrontend repository does not currently contain a top-level
license grant. This has been reported upstream:

https://github.com/N64Recomp/RecompFrontend/issues/44

The project is proceeding with this beta while that clarification remains
pending, following the practical distribution model used by other public
N64: Recompiled projects. This notice is deliberately retained in the
repository, downloadable archive and release notes.

This policy is a disclosure of the known uncertainty, not a statement that an
unpublished RecompFrontend license has been inferred or granted.

## Game-module status

The precompiled game module is distributed separately from the GPL engine at
runtime through the documented module ABI. It is provided only to make the
port usable without requiring end users to install a compiler. It still
requires the user's original ROM and does not replace or include that ROM.

The project does not claim ownership of Snowboard Kids game code, trademarks
or original content. Rights in the original game remain with their respective
rights holders.

## Release requirements

A public beta archive must:

- contain no ROM or extracted commercial game assets;
- contain no user saves, Controller Pak files, savestates or local config;
- contain `LICENSE`, `THIRD_PARTY_NOTICES.md` and `SOURCE-COMPLIANCE.md`;
- contain `ROM-IDENTITY.md` with the exact canonical source hashes and `LAUNCHER-INTEGRATION.md` with the package/user-data ownership boundary;
- identify the exact project commit and dependency-lock digest;
- include the reviewed engine and game module only;
- pass `scripts/audit_release_artifact.py`;
- publish a SHA-256 checksum;
- be marked as a prerelease/beta on GitHub.

The unresolved RecompFrontend license status must remain visible until upstream
provides an explicit grant.

The launcher integration contract does not grant new redistribution rights. A
launcher may use it to implement a non-owning registration/launch route that
leaves the user's prepared package, ROM and mutable data under user ownership.
