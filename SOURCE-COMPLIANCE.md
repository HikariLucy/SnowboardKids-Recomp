# Corresponding source and build information

Snowboard Kids Recompiled public binaries are distributed from this repository:

https://github.com/HikariLucy/SnowboardKids-Recomp

For `v0.9.0-beta.1`, the GitHub Release must point to the exact tag
`v0.9.0-beta.1`. GitHub's source archive for that tag and the repository history
provide the project-authored source, build scripts, dependency lock and patch
series used by the engine.

## GPL-covered engine

The engine links N64ModernRuntime, which is GPL-3.0 licensed. The repository
pins the exact N64ModernRuntime revision and retains every project patch applied
to that checkout. The same applies to the other pinned build dependencies.

To reconstruct the dependency checkouts and patches, use the repository's
bootstrap scripts and dependency lock. No ROM is fetched by those scripts.

## Game module

The downloadable beta may also contain a separate dynamically loaded
`SnowboardKidsGame` module. The module is not required to obtain or inspect
the GPL-covered engine source. It contains statically recompiled game logic and
generated RSP code and is disclosed separately in
`docs/BETA-DISTRIBUTION-POLICY.md`.

## Launcher and source-identity metadata

`ROM-IDENTITY.md` records hashes and structural metadata for the exact supported
user-provided source without containing any ROM bytes. `LAUNCHER-INTEGRATION.md`
documents executable layout, persistence boundaries and safe managed/non-owning
launcher behavior. These files are interoperability metadata and do not alter
third-party license terms.

## No game ROM in source or binary distribution

The repository and release archives intentionally exclude the Snowboard Kids
ROM and extracted commercial game assets. A user-owned supported ROM is
validated at runtime.
