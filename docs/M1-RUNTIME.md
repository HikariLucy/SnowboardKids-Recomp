# M1.5 — Native Runtime Integration

Updated: 2026-09-23

## Goal

Move from successfully generated host code to a native executable backed by N64ModernRuntime and RT64.

This milestone is split deliberately so compile-time ABI problems are separated from renderer/frontend/runtime behavior.

## Reference architecture

The working Snowboard Kids 2 recomp at commit:

```text
031ee23a01ec23ef40660bfb659bd8fcb5a3e265
```

pins:

```text
N64ModernRuntime  6ccb2e7c2e7f6708257b461097e0aaf03c445e2a
RT64              6a4166b2cfa952d931a08481d1037da995f28b54
RecompFrontend    e85b912d9df677b04f9358867dd010c8af27ea05
```

We will start by pinning the same N64ModernRuntime revision.

Note that this runtime revision itself pins N64Recomp:

```text
81213c1831fab2521a6a5459c67b63437d67e253
```

while this project currently generates code with the newer:

```text
ffb39cdad1da5de07eaaa48bd1db4a89a7986771
```

The first native-core compile intentionally uses the runtime's own N64Recomp headers. This is an ABI-compatibility test. If the generated CPU code compiles against those headers, we have evidence that the newer recompiler output remains compatible with the working runtime baseline.

## M1.5a — Native-core compile validation

Inputs:

```text
RecompiledFuncs/*.c
rsp/aspMain.cpp
```

Dependencies:

```text
N64ModernRuntime/librecomp
N64ModernRuntime/ultramodern
N64ModernRuntime/N64Recomp headers
```

Commands:

```bash
bash scripts/bootstrap-native-runtime.sh
bash scripts/build-native-core.sh
```

Expected outputs:

```text
build-native-core/libSnowboardKidsCpu.a
build-native-core/libSnowboardKidsRsp.a
```

This does not yet produce a playable executable. It verifies that the two generated translations compile as native host code against the runtime ABI.

## M1.5b — Runtime executable skeleton

After M1.5a passes:

- add RT64 at the pinned working revision;
- add the required frontend/window/input dependencies;
- register Snowboard Kids as a `recomp::GameEntry`;
- register `recomp_entrypoint`;
- route `M_AUDTASK` to `aspMain`;
- validate ROM identity at runtime;
- select the correct save type;
- link the generated CPU and RSP libraries;
- attempt native startup.

## M1.5c — First runtime blockers

The first executable is expected to expose game-specific runtime assumptions. We will resolve these individually and document every patch rather than importing Snowboard Kids 2 patches wholesale.

Likely areas include:

- task scheduler behavior;
- audio buffering;
- controller/PFS behavior;
- graphics task submission;
- VI timing;
- framebuffer assumptions.

## Exit criterion

M1.5 is complete when a native Snowboard Kids executable links and begins executing `recomp_entrypoint` through N64ModernRuntime.

M2 begins with stabilizing that first boot.


### First native-core blocker: N64 `main` collision

The first host compilation reached Clang successfully but failed because the original game exports an N64 function literally named:

```text
main
```

N64Recomp initially emitted:

```c
void main(uint8_t* rdram, recomp_context* ctx);
```

In hosted C, Clang reserves `main` as the program entry function and requires the conventional host signature, so it rejected every translation unit that included `funcs.h`.

This is a naming collision, not a CPU translation or runtime ABI failure.

Resolution:

```toml
[patches]
renamed = [
    "main",
]
```

N64Recomp's rename mechanism consistently emits this function as `main_recomp` and rewrites references/calls to the renamed symbol.

After pulling this fix, regenerate `RecompiledFuncs/` before rebuilding the native core.


### Second native-core blocker: RSP SIMD target features

After resolving the original-game `main` symbol collision, all generated CPU translation units advanced through host compilation. The remaining failure was isolated to:

```text
rsp/aspMain.cpp
```

N64ModernRuntime selects its SIMD RSP implementation automatically on x86-64. That implementation uses:

```text
_mm_shuffle_epi8  -> SSSE3
_mm_blendv_epi8   -> SSE4.1
```

The standalone `SnowboardKidsRsp` validation target had not enabled those target features, so Clang rejected the always-inline intrinsics.

Resolution:

```cmake
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
    target_compile_options(SnowboardKidsRsp PRIVATE
        -mssse3
        -msse4.1
    )
endif()
```

The flags are scoped only to the RSP target and only to x86/x64. ARM64 remains on N64ModernRuntime's sse2neon path.


### M1.5a result

**PASS**

The native-core build produced both archives successfully:

```text
build-native-core/libSnowboardKidsCpu.a
build-native-core/libSnowboardKidsRsp.a
```

This is the first point in the project where the translated Snowboard Kids CPU and audio RSP code have both been compiled into native x86-64 machine-code libraries.

The next step is no longer code-generation validation. It is runtime integration.


### M1.5b.1 — ROM identity

N64ModernRuntime validates supported ROMs with `XXH3_64bits` after normalizing byte order. This is distinct from the SHA-1 used by the matching decomp baseline.

The first-game runtime entry also needs the ROM's internal header name. Obtain both directly from the verified local dump with:

```bash
bash scripts/inspect-runtime-rom.sh
```

The original game uses Controller Pak/PFS flows for its large save and replay data. The decomp exposes a `GameSaveData` slot of `0x78F8` bytes and explicit Controller Pak read/write/menu code, so the runtime's cartridge save medium should start as:

```cpp
.save_type = recomp::SaveType::None
```

Controller Pak support is handled through the runtime's PFS path rather than EEPROM/SRAM/FlashRAM.

The entrypoint remains:

```text
0x80000400
```

Once the XXH3-64 value and internal name are recorded, the first `recomp::GameEntry` can be created without guessed metadata.


### M1.5b.2 — Runtime smoke executable

Verified Snowboard Kids USA runtime metadata:

```text
XXH3-64       = 0xF384619787B78D4B
Internal name = SNOWBOARD KIDS
Game code     = NSKE
Entrypoint    = 0x80000400
```

The first native runtime executable deliberately stops before graphics initialization. It validates the integration boundary by:

1. linking the generated CPU and RSP native libraries;
2. linking N64ModernRuntime;
3. registering the real `recomp::GameEntry`;
4. verifying the generated entrypoint address;
5. passing the verified local ROM through `recomp::select_rom`;
6. loading N64ModernRuntime's stored normalized ROM copy;
7. confirming the runtime sees the expected 8 MiB image.

Run:

```bash
bash scripts/build-runtime-smoke.sh
```

Expected terminal result:

```text
ROM validation  : Good
Runtime ROM load: OK (8388608 bytes)
Result          : PASS
```

This is intentionally before RT64/SDL/frontend so any linker or runtime identity issue remains isolated.


### First runtime linker boundary

The first `SnowboardKidsRuntimeSmoke` build compiled all 149 objects and reached the final executable link. The unresolved symbols were limited to:

```text
__osPfsSelectBank_recomp
__osContRamRead_recomp
__osContRamWrite_recomp
rmonPrintf_recomp
```

These functions are in N64Recomp's built-in ignored-function set. The generated SBK1 translation can still contain references to them because some compiled libultra internals share output translation units with game code.

For the current runtime baseline:

- N64ModernRuntime's public Controller Pak/PFS functions return `PFS_ERR_NOPACK` (`1`);
- the SBK1 matching decomp defines `rmonPrintf` as an empty function.

Therefore `src/main/runtime_compat.cpp` provides narrow ABI shims:

```text
__osPfsSelectBank_recomp -> PFS_ERR_NOPACK
__osContRamRead_recomp   -> PFS_ERR_NOPACK
__osContRamWrite_recomp  -> PFS_ERR_NOPACK
rmonPrintf_recomp        -> no-op
```

These are bootstrap compatibility shims, not the final Controller Pak implementation. Persistent SBK1 save/replay support remains a later PFS task.


### First renderer-stack blocker: SDL2 include propagation

The first RT64 + RecompFrontend build configured the Vulkan renderer successfully and reached RecompFrontend compilation, but `recompinput` failed with:

```text
fatal error: 'SDL.h' file not found
```

SDL2 itself was installed correctly. The issue was CMake ordering: RecompFrontend's `recompinput` and `recompui` projects consume `${SDL2_INCLUDE_DIRS}`, but the parent SnowboardKids-Recomp project had not called `find_package(SDL2 REQUIRED)` before adding RecompFrontend.

The working Snowboard Kids 2 port resolves SDL2 in the parent CMake project before `add_subdirectory(RecompFrontend)`.

Resolution:

```cmake
if(CMAKE_SYSTEM_NAME MATCHES "Linux")
    find_package(SDL2 REQUIRED)
endif()
```

This populates the expected include directory (normally `/usr/include/SDL2`) for RecompFrontend without hard-coding a distro-specific path.


### Second renderer-stack blocker: DXC scope

After SDL2 propagation was fixed, the renderer/frontend build advanced from the first RecompFrontend files to more than 500 of 609 build steps. RT64 itself compiled successfully far into its source tree.

The next failure occurred while generating RecompFrontend SPIR-V shaders:

```text
Generating shaders/InterfaceVS.hlsl.spv
/bin/sh: .../InterfaceVS.hlsl: Permission denied
```

The generated command attempted to execute the HLSL source file directly. This means the RT64 `build_vertex_shader` / `build_pixel_shader` helpers were visible, but their `DXC` variable was empty in RecompFrontend's CMake scope.

RT64 defines `DXC` and shader option variables inside the RT64 subdirectory. CMake directory scopes do not propagate those values back to the parent. The working Snowboard Kids 2 build explicitly re-declares the DXC command/options in the root project before adding RecompFrontend.

SnowboardKids-Recomp now mirrors that arrangement, using the pinned RT64 DXC binaries for each host architecture.

Expected Linux x86-64 shader compiler:

```text
.deps-renderer/rt64/src/contrib/dxc/bin/x64/dxc-linux
```

with its matching `LD_LIBRARY_PATH`.


### Third renderer-stack blocker: game UI ABI header

After DXC scope was fixed, the renderer/frontend build advanced into the final RecompFrontend UI sources (545+/609 steps). It then stopped because RecompFrontend contains a deliberate game-facing include:

```cpp
#include "../../../../../patches/ui_funcs.h"
```

This interface supplies the event data layout used when the native frontend queues callbacks into recompiled game code.

SnowboardKids-Recomp now provides a minimal project-owned interface:

```text
patches/recompui_event_structs.h
patches/ui_funcs.h
```

The event enum ordering matches RecompFrontend's `recompui::EventType`, `DragPhase`, and `MenuAction` ordering. The header also declares the runtime callback:

```cpp
void recomp_run_ui_callbacks(uint8_t* rdram, recomp_context* ctx);
```

No SBK2 game-specific patch behavior is imported. This is only the ABI contract RecompFrontend requires to compile.


### Fourth renderer-stack blocker: SDL/Vulkan window ABI

After the project-owned UI ABI headers were added, the renderer/frontend build reached the final RecompFrontend source file:

```text
[608/609] recompui/src/renderer/rt64_render_context.cpp
```

The compile failed assigning:

```cpp
appCore.window = window_handle;
```

because:

- N64ModernRuntime defines Linux `WindowHandle` as `SDL_Window*`;
- Plume defines `RenderWindow` as `SDL_Window*` only when `PLUME_SDL_VULKAN_ENABLED` is defined;
- otherwise, on Linux, Plume falls back to an X11 `RenderWindow` struct.

RT64's own CMake enables the SDL/Vulkan macro inside RT64's directory scope, but that definition does not automatically propagate to the sibling RecompFrontend directory.

The working Snowboard Kids 2 root build explicitly defines:

```cmake
PLUME_SDL_VULKAN_ENABLED
RT64_SDL_WINDOW_VULKAN
```

on Linux before building RecompFrontend.

SnowboardKids-Recomp now does the same, so both runtime and frontend agree that the window ABI is `SDL_Window*`.


### Renderer/frontend stack result

**PASS**

The isolated graphics/frontend build completed:

```text
[609/609] Linking CXX static library RecompFrontend/recompui/librecompui.a
Renderer/frontend compile validation passed.
```

The project can now compile the complete pinned RT64/RecompFrontend stack on Linux with SDL2 + Vulkan.

### M1.5b.3 — First graphical native boot

The next executable is intentionally minimal and launcher-free:

```text
SnowboardKidsRecompiled
```

It:

1. validates and stores the verified local SBK1 ROM;
2. registers N64Recomp's generated section/overlay tables;
3. creates an SDL2 Vulkan window;
4. wires RT64 through RecompFrontend's renderer context;
5. routes `M_AUDTASK` to the generated `aspMain`;
6. supplies bootstrap input/audio callbacks;
7. calls `recomp::start_game`;
8. enters N64ModernRuntime;
9. wraps the generated entrypoint with a terminal trace.

Critical success marker:

```text
>>> ENTERING SNOWBOARD KIDS RECOMP_ENTRYPOINT
```

Reaching that line completes the M1.5 exit criterion even if the game then exposes a new startup blocker.

Run:

```bash
bash scripts/build-native-boot.sh
```


### First graphical boot linker boundary

The first `SnowboardKidsRecompiled` build compiled all 146 objects and failed only at the final native link.

RecompFrontend expects two globals to be owned by the executable:

```cpp
SDL_Window* window;
std::vector<recomp::GameEntry> supported_games;
```

The initial minimal boot used a private `g_window` and a local `GameEntry`, so those symbols were absent. They are now exported with the expected names.

A third unresolved dependency came from the two static RecompFrontend archives:

```text
recompui -> recompinput
recompinput -> recompui::controls_page
```

GNU ld processes static archives from left to right and does not automatically re-scan an earlier archive. The boot target now links:

```text
recompui
recompinput
recompui
```

so the reverse UI dependency can be resolved without modifying upstream RecompFrontend.
