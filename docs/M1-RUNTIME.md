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
