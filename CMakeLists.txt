cmake_minimum_required(VERSION 3.20)

project(SnowboardKidsRecompiled LANGUAGES C CXX)

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(N64_RUNTIME_DIR "${CMAKE_SOURCE_DIR}/.deps-runtime/N64ModernRuntime")

if(NOT EXISTS "${N64_RUNTIME_DIR}/librecomp/include/librecomp/rsp.hpp")
    message(FATAL_ERROR
        "N64ModernRuntime is missing. Run: bash scripts/bootstrap-native-runtime.sh"
    )
endif()

file(GLOB RECOMPILED_CPU_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/RecompiledFuncs/*.c"
)
file(GLOB RECOMPILED_CPU_CXX_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/RecompiledFuncs/*.cpp"
)

if(NOT RECOMPILED_CPU_SOURCES)
    message(FATAL_ERROR
        "RecompiledFuncs is empty or missing. Run: bash scripts/run-recompiler.sh"
    )
endif()

if(NOT EXISTS "${CMAKE_SOURCE_DIR}/rsp/aspMain.cpp")
    message(FATAL_ERROR
        "rsp/aspMain.cpp is missing. Run: bash scripts/run-rsp-recompiler.sh"
    )
endif()

add_library(SnowboardKidsCpu STATIC
    ${RECOMPILED_CPU_SOURCES}
    ${RECOMPILED_CPU_CXX_SOURCES}
)

target_compile_options(SnowboardKidsCpu PRIVATE
    -fno-strict-aliasing
    -Wno-unused-variable
    -Wno-unused-but-set-variable
    -Wno-implicit-function-declaration
)

target_include_directories(SnowboardKidsCpu PRIVATE
    "${CMAKE_SOURCE_DIR}/RecompiledFuncs"
    "${N64_RUNTIME_DIR}/N64Recomp/include"
)

add_library(SnowboardKidsRsp STATIC
    "${CMAKE_SOURCE_DIR}/rsp/aspMain.cpp"
)

target_compile_options(SnowboardKidsRsp PRIVATE
    -fno-strict-aliasing
    -Wno-unused-variable
    -Wno-unused-but-set-variable
)

# N64ModernRuntime's RSP vector implementation selects its x86 SIMD path on
# x86_64. That implementation uses SSSE3 (_mm_shuffle_epi8) and SSE4.1
# (_mm_blendv_epi8), so the standalone RSP validation target must enable the
# same ISA features explicitly. Keep this scoped to x86/x64; ARM64 uses the
# runtime's sse2neon path instead.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
    target_compile_options(SnowboardKidsRsp PRIVATE
        -mssse3
        -msse4.1
    )
endif()

target_include_directories(SnowboardKidsRsp PRIVATE
    "${N64_RUNTIME_DIR}/librecomp/include"
    "${N64_RUNTIME_DIR}/N64Recomp/include"
    "${N64_RUNTIME_DIR}/ultramodern/include"
)

add_custom_target(SnowboardKidsNativeCore
    DEPENDS SnowboardKidsCpu SnowboardKidsRsp
)

message(STATUS "Configured Snowboard Kids native-core compile validation")


option(SBK_BUILD_RUNTIME_SMOKE "Build the N64ModernRuntime GameEntry/ROM smoke executable" OFF)

if(SBK_BUILD_RUNTIME_SMOKE)
    add_subdirectory(
        "${N64_RUNTIME_DIR}"
        "${CMAKE_BINARY_DIR}/N64ModernRuntime"
    )

    add_executable(SnowboardKidsRuntimeSmoke
        "${CMAKE_SOURCE_DIR}/src/main/runtime_smoke.cpp"
        "${CMAKE_SOURCE_DIR}/src/main/runtime_compat.cpp"
    )

    target_compile_options(SnowboardKidsRuntimeSmoke PRIVATE
        -fno-strict-aliasing
    )

    target_include_directories(SnowboardKidsRuntimeSmoke PRIVATE
        "${N64_RUNTIME_DIR}/librecomp/include"
        "${N64_RUNTIME_DIR}/N64Recomp/include"
        "${N64_RUNTIME_DIR}/ultramodern/include"
    )

    target_link_libraries(SnowboardKidsRuntimeSmoke PRIVATE
        SnowboardKidsCpu
        SnowboardKidsRsp
        librecomp
        ultramodern
    )
endif()


option(SBK_BUILD_RENDERER_STACK "Build RT64 + RecompFrontend integration dependencies" OFF)

if(SBK_BUILD_RENDERER_STACK)
    set(RT64_DIR "${CMAKE_SOURCE_DIR}/.deps-renderer/rt64")
    set(RECOMP_FRONTEND_DIR "${CMAKE_SOURCE_DIR}/.deps-renderer/RecompFrontend")

    if(NOT EXISTS "${RT64_DIR}/CMakeLists.txt")
        message(FATAL_ERROR
            "RT64 is missing. Run: bash scripts/bootstrap-renderer-stack.sh"
        )
    endif()

    if(NOT EXISTS "${RECOMP_FRONTEND_DIR}/CMakeLists.txt")
        message(FATAL_ERROR
            "RecompFrontend is missing. Run: bash scripts/bootstrap-renderer-stack.sh"
        )
    endif()

    set(RT64_STATIC TRUE)
    set(RT64_SDL_WINDOW_VULKAN TRUE)
    add_compile_definitions(HLSL_CPU)

    add_subdirectory(
        "${RT64_DIR}"
        "${CMAKE_BINARY_DIR}/rt64"
    )

    add_subdirectory(
        "${N64_RUNTIME_DIR}"
        "${CMAKE_BINARY_DIR}/N64ModernRuntime-renderer"
    )

    # RecompFrontend's recompinput/recompui CMake files consume the
    # SDL2_INCLUDE_DIRS variable supplied by FindSDL2. The working SBK2 port
    # resolves SDL2 in the parent project before adding RecompFrontend.
    if(CMAKE_SYSTEM_NAME MATCHES "Linux")
        find_package(SDL2 REQUIRED)
        message(STATUS "SDL2_FOUND = ${SDL2_FOUND}")
        message(STATUS "SDL2_INCLUDE_DIRS = ${SDL2_INCLUDE_DIRS}")

        # N64ModernRuntime exposes SDL_Window* as its Linux WindowHandle.
        # Plume must use its SDL/Vulkan RenderWindow typedef in every sibling
        # target that includes RT64 headers, including RecompFrontend.
        add_compile_definitions(
            PLUME_SDL_VULKAN_ENABLED
            RT64_SDL_WINDOW_VULKAN
        )
    endif()

    # RT64 defines its DXC shader compiler variables inside its own
    # subdirectory scope. RecompFrontend calls RT64's shader helper functions
    # from a sibling subdirectory, so the parent project must expose the same
    # DXC command/options before adding RecompFrontend. This mirrors the
    # working Snowboard Kids 2 build.
    set(DXC_COMMON_OPTS "-I${RT64_DIR}/src")
    set(DXC_DXIL_OPTS "-Wno-ignored-attributes")
    set(DXC_SPV_OPTS "-spirv" "-fspv-target-env=vulkan1.0" "-fvk-use-dx-layout")
    set(DXC_PS_OPTS "${DXC_COMMON_OPTS}" "-E" "PSMain" "-T" "ps_6_0" "-D" "DYNAMIC_RENDER_PARAMS")
    set(DXC_VS_OPTS "${DXC_COMMON_OPTS}" "-E" "VSMain" "-T" "vs_6_0" "-D" "DYNAMIC_RENDER_PARAMS" "-fvk-invert-y")
    set(DXC_CS_OPTS "${DXC_COMMON_OPTS}" "-E" "CSMain" "-T" "cs_6_0")
    set(DXC_GS_OPTS "${DXC_COMMON_OPTS}" "-E" "GSMain" "-T" "gs_6_0")

    if(WIN32)
        set(DXC "${RT64_DIR}/src/contrib/dxc/bin/x64/dxc.exe")
    elseif(APPLE)
        if(CMAKE_SIZEOF_VOID_P EQUAL 8 AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|AMD64")
            set(DXC
                "DYLD_LIBRARY_PATH=${RT64_DIR}/src/contrib/dxc/lib/x64"
                "${RT64_DIR}/src/contrib/dxc/bin/x64/dxc-macos"
            )
        else()
            set(DXC
                "DYLD_LIBRARY_PATH=${RT64_DIR}/src/contrib/dxc/lib/arm64"
                "${RT64_DIR}/src/contrib/dxc/bin/arm64/dxc-macos"
            )
        endif()
    else()
        if(CMAKE_SIZEOF_VOID_P EQUAL 8 AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|AMD64")
            set(DXC
                "LD_LIBRARY_PATH=${RT64_DIR}/src/contrib/dxc/lib/x64"
                "${RT64_DIR}/src/contrib/dxc/bin/x64/dxc-linux"
            )
        else()
            set(DXC
                "LD_LIBRARY_PATH=${RT64_DIR}/src/contrib/dxc/lib/arm64"
                "${RT64_DIR}/src/contrib/dxc/bin/arm64/dxc-linux"
            )
        endif()
    endif()

    set(RECOMP_FRONTEND_N64MODERNRUNTIME_PATH "${N64_RUNTIME_DIR}")
    set(RECOMP_FRONTEND_RT64_PATH "${RT64_DIR}")

    add_subdirectory(
        "${RECOMP_FRONTEND_DIR}"
        "${CMAKE_BINARY_DIR}/RecompFrontend"
    )

    add_custom_target(SnowboardKidsRendererStack
        DEPENDS rt64 recompui recompinput
    )
endif()


option(SBK_BUILD_NATIVE_BOOT "Build the first graphical Snowboard Kids native boot executable" OFF)

if(SBK_BUILD_NATIVE_BOOT)
    if(NOT SBK_BUILD_RENDERER_STACK)
        message(FATAL_ERROR "SBK_BUILD_NATIVE_BOOT requires SBK_BUILD_RENDERER_STACK=ON")
    endif()

    find_package(Threads REQUIRED)

    add_executable(SnowboardKidsRecompiled
        "${CMAKE_SOURCE_DIR}/src/main/native_boot.cpp"
        "${CMAKE_SOURCE_DIR}/src/main/register_overlays.cpp"
        "${CMAKE_SOURCE_DIR}/src/main/runtime_compat.cpp"
    )

    target_compile_options(SnowboardKidsRecompiled PRIVATE
        -fno-strict-aliasing
        -fms-extensions
    )

    target_include_directories(SnowboardKidsRecompiled PRIVATE
        "${CMAKE_SOURCE_DIR}"
        "${N64_RUNTIME_DIR}/librecomp/include"
        "${N64_RUNTIME_DIR}/N64Recomp/include"
        "${N64_RUNTIME_DIR}/ultramodern/include"
        "${N64_RUNTIME_DIR}/thirdparty"
        "${RECOMP_FRONTEND_DIR}/recompui/include"
        "${RECOMP_FRONTEND_DIR}/recompinput/include"
        "${RT64_DIR}/src"
        "${RT64_DIR}/src/contrib"
        "${CMAKE_BINARY_DIR}/shaders"
        ${SDL2_INCLUDE_DIRS}
    )

    target_link_libraries(SnowboardKidsRecompiled PRIVATE
        SnowboardKidsCpu
        SnowboardKidsRsp
        recompui
        recompinput
        librecomp
        ultramodern
        rt64
        nfd
        SDL2::SDL2
        Threads::Threads
        ${CMAKE_DL_LIBS}
        atomic
    )
endif()
