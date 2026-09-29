cmake_minimum_required(VERSION 3.20)

project(SnowboardKidsRecompiled VERSION 0.9.0 LANGUAGES C CXX)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_custom_target(sbk_version_header ALL
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/generate_version_header.py"
        --out "${CMAKE_BINARY_DIR}/generated/sbk_version.h"
        --version "${PROJECT_VERSION}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Generating dynamic version header"
)
include_directories("${CMAKE_BINARY_DIR}/generated")

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    get_filename_component(RECOMP_PROJ_PARENT "${CMAKE_SOURCE_DIR}/.." ABSOLUTE)
    # clang-cl ignores GCC-style driver options unless forwarded with /clang:.
    set(SBK_PREFIX_MAP_FLAG "-ffile-prefix-map=")
    if(MSVC)
        set(SBK_PREFIX_MAP_FLAG "/clang:-ffile-prefix-map=")
    endif()
    # The last matching map wins: broadest first so the checkout's own
    # directory name never reaches embedded __FILE__ strings.
    add_compile_options(
        "$<$<COMPILE_LANGUAGE:C,CXX>:${SBK_PREFIX_MAP_FLAG}${RECOMP_PROJ_PARENT}=.>"
        "$<$<COMPILE_LANGUAGE:C,CXX>:${SBK_PREFIX_MAP_FLAG}${CMAKE_BINARY_DIR}=.>"
        "$<$<COMPILE_LANGUAGE:C,CXX>:${SBK_PREFIX_MAP_FLAG}${CMAKE_SOURCE_DIR}=.>"
    )
endif()

# Controller Pak suite: the real PFS backend and HLE adapter, compiled with the
# project's compiler (MSVC or clang-cl on Windows) and run directly by CTest.
set(SBK_RECOMP_INCLUDE "${CMAKE_SOURCE_DIR}/.deps-runtime/N64ModernRuntime/N64Recomp/include"
    CACHE PATH "N64Recomp headers (recomp.h) for the Controller Pak HLE test")
set(SBK_PFS_SANITIZER "" CACHE STRING
    "GCC/Clang only: -fsanitize= list for the Controller Pak test, e.g. address,undefined")
function(sbk_add_controller_pak_test)
    if(NOT EXISTS "${SBK_RECOMP_INCLUDE}/recomp.h")
        message(FATAL_ERROR "recomp.h is missing from SBK_RECOMP_INCLUDE (${SBK_RECOMP_INCLUDE}). "
            "Run: python3 scripts/bootstrap.py --only runtime")
    endif()
    add_executable(SnowboardKidsControllerPakTest
        tests/controller_pak/main.cpp
        tests/controller_pak/hle.cpp
        tests/controller_pak/suite.cpp
        src/pfs/controller_pak.cpp
        src/pfs/hle.cpp
    )
    target_include_directories(SnowboardKidsControllerPakTest PRIVATE src "${SBK_RECOMP_INCLUDE}")
    if(MSVC)
        if(SBK_PFS_SANITIZER)
            message(FATAL_ERROR "SBK_PFS_SANITIZER is only supported with GCC/Clang")
        endif()
        target_compile_options(SnowboardKidsControllerPakTest PRIVATE /W4)
    else()
        target_compile_options(SnowboardKidsControllerPakTest PRIVATE -Wall -Wextra -Werror)
        if(SBK_PFS_SANITIZER)
            target_compile_options(SnowboardKidsControllerPakTest PRIVATE
                "-fsanitize=${SBK_PFS_SANITIZER}" -fno-sanitize-recover=all -fno-omit-frame-pointer)
            target_link_options(SnowboardKidsControllerPakTest PRIVATE "-fsanitize=${SBK_PFS_SANITIZER}")
        endif()
    endif()
    message(STATUS "Controller Pak test compiler: ${CMAKE_CXX_COMPILER_ID} "
        "${CMAKE_CXX_COMPILER_VERSION} (${CMAKE_CXX_COMPILER})")
    add_test(NAME controller_pak COMMAND SnowboardKidsControllerPakTest)
endfunction()

option(SBK_ROM_FREE_CI "Configure only ROM-free CI targets" OFF)
if(SBK_ROM_FREE_CI)
    enable_testing()
    add_executable(SnowboardKidsAudioProgressTest tests/compat/audio_progress.cpp)
    target_include_directories(SnowboardKidsAudioProgressTest PRIVATE src)
    add_test(NAME compat_audio_progress COMMAND SnowboardKidsAudioProgressTest)
    add_executable(SnowboardKidsHostGainTest tests/audio/host_gain.cpp)
    target_include_directories(SnowboardKidsHostGainTest PRIVATE src)
    add_test(NAME audio_host_gain COMMAND SnowboardKidsHostGainTest)

    add_library(SnowboardKidsSyntheticModule SHARED
        tests/module/synthetic_module.cpp
    )
    target_include_directories(SnowboardKidsSyntheticModule PRIVATE src)

    add_executable(SnowboardKidsModuleLoaderTest
        tests/module/test_module_loader.cpp
        src/module/module_loader.cpp
    )
    target_include_directories(SnowboardKidsModuleLoaderTest PRIVATE src)
    target_link_libraries(SnowboardKidsModuleLoaderTest PRIVATE ${CMAKE_DL_LIBS})
    if(CMAKE_SYSTEM_NAME MATCHES "Linux")
        target_link_options(SnowboardKidsModuleLoaderTest PRIVATE -rdynamic)
    endif()
    add_test(NAME module_loader_synthetic
        COMMAND SnowboardKidsModuleLoaderTest "$<TARGET_FILE:SnowboardKidsSyntheticModule>")
    add_test(NAME module_builder_unit
        COMMAND "${Python3_EXECUTABLE}" -m unittest discover -s "${CMAKE_SOURCE_DIR}/tests/module" -p "test_builder_unit.py")
    add_executable(SnowboardKidsFirstRunProcessTest tests/module/test_first_run.cpp src/ui/first_run_process.cpp)
    target_include_directories(SnowboardKidsFirstRunProcessTest PRIVATE "${CMAKE_SOURCE_DIR}")
    add_test(NAME ui_first_run_process COMMAND SnowboardKidsFirstRunProcessTest)
    add_executable(SnowboardKidsMenuActionsTest tests/frontend/menu_actions.cpp)
    target_include_directories(SnowboardKidsMenuActionsTest PRIVATE src)
    find_package(Threads REQUIRED)
    target_link_libraries(SnowboardKidsMenuActionsTest PRIVATE Threads::Threads)
    add_test(NAME ui_menu_actions COMMAND SnowboardKidsMenuActionsTest)
    add_test(NAME ui_first_run_protocol
        COMMAND "${Python3_EXECUTABLE}" -m unittest discover -s "${CMAKE_SOURCE_DIR}/tests/module" -p "test_first_run_protocol.py")
    add_test(NAME ui_assets
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/release/test_ui_assets.py")

    sbk_add_controller_pak_test()
    add_test(NAME release_artifact_audit
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/release/test_artifact_audit.py")
    add_test(NAME release_package
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/release/test_package.py")
    add_test(NAME release_bootstrap
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/release/test_bootstrap.py")
    add_test(NAME release_version
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/release/test_version.py")
    add_test(NAME release_readiness
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/release/test_readiness.py")
    add_test(NAME release_windows_package
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/release/test_windows_package.py")
    add_test(NAME release_dxil_hash
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/release/test_dxil_hash.py")
    return()
endif()

set(N64_RUNTIME_DIR "${CMAKE_SOURCE_DIR}/.deps-runtime/N64ModernRuntime")

if(NOT EXISTS "${N64_RUNTIME_DIR}/librecomp/include/librecomp/rsp.hpp")
    message(FATAL_ERROR
        "N64ModernRuntime is missing. Run: bash scripts/bootstrap-native-runtime.sh"
    )
endif()

option(SBK_CONTINUATIONS "Explicit serializable continuation backend (P4-A validated)" OFF)

# ROM-free split runtime: build only SnowboardKidsEngine, which loads the game
# module at run time. No SnowboardKidsCpu/Rsp/Recompiled targets and no
# ROM-derived corpus are required, so hosted CI can build the release engine.
option(SBK_ENGINE_ONLY "Build only the ROM-free SnowboardKidsEngine (no ROM-derived targets)" OFF)
if(SBK_ENGINE_ONLY)
    if(NOT SBK_BUILD_RENDERER_STACK OR NOT SBK_BUILD_NATIVE_BOOT OR SBK_BUILD_RUNTIME_SMOKE)
        message(FATAL_ERROR "SBK_ENGINE_ONLY requires SBK_BUILD_RENDERER_STACK=ON, SBK_BUILD_NATIVE_BOOT=ON and SBK_BUILD_RUNTIME_SMOKE=OFF")
    endif()
endif()

# CI-only: compile the patched renderer/frontend/runtime stack without the
# ROM-derived CPU and RSP sources. Never a production or packaging path.
option(SBK_RENDERER_COMPILE_CI "CI only: build the renderer stack without ROM-derived code" OFF)
if(SBK_RENDERER_COMPILE_CI)
    if(NOT SBK_BUILD_RENDERER_STACK OR SBK_BUILD_RUNTIME_SMOKE OR SBK_BUILD_QUIESCENCE_TESTS OR
       (SBK_BUILD_NATIVE_BOOT AND NOT SBK_ENGINE_ONLY))
        message(FATAL_ERROR "SBK_RENDERER_COMPILE_CI requires SBK_BUILD_RENDERER_STACK=ON and no executables that need ROM-derived code (native boot only with SBK_ENGINE_ONLY=ON)")
    endif()
elseif(SBK_ENGINE_ONLY)
    message(STATUS "Configured ROM-free engine-only build (no SnowboardKidsCpu/Rsp)")
else()
set(SBK_CPU_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/RecompiledFuncs")
if(SBK_CONTINUATIONS)
    set(SBK_CPU_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/build-tools/production-continuation/corpus")
    if(NOT EXISTS "${CMAKE_SOURCE_DIR}/build-tools/production-continuation/manifest.tsv")
        message(FATAL_ERROR "Generate P4-A corpus first: tests/production_continuation/generate_corpus.py")
    endif()
endif()

file(GLOB RECOMPILED_CPU_SOURCES CONFIGURE_DEPENDS
    "${SBK_CPU_OUTPUT_DIR}/*.c"
)
file(GLOB RECOMPILED_CPU_CXX_SOURCES CONFIGURE_DEPENDS
    "${SBK_CPU_OUTPUT_DIR}/*.cpp"
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

if(SBK_CONTINUATIONS)
    set_source_files_properties(${RECOMPILED_CPU_SOURCES} PROPERTIES LANGUAGE CXX)
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
    "${SBK_CPU_OUTPUT_DIR}"
    "${CMAKE_SOURCE_DIR}/src"
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
endif()


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
    set(RECOMP_THEME_DIR "${CMAKE_SOURCE_DIR}/.deps-renderer/recomp-theme")

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

    if(NOT EXISTS "${RECOMP_THEME_DIR}/CMakeLists.txt")
        message(FATAL_ERROR
            "Snowboard Kids recomp theme is missing. Run: bash scripts/bootstrap-renderer-stack.sh"
        )
    endif()

    set(RT64_STATIC TRUE)
    set(RT64_SDL_WINDOW_VULKAN TRUE)
    add_compile_definitions(HLSL_CPU)

    # Windows: every shader is compiled with the pinned official DXC release
    # (scripts/dxc_redist.py) whose dxcompiler.dll ships with the engine. RT64
    # links run-time shaders against build-time library shaders, which
    # IDxcLinker only accepts from the same compiler, and this release signs
    # DXIL internally, without the dxil.dll validator (docs/DXC-PROVENANCE.md).
    if(WIN32)
        set(SBK_DXC_REDIST_DIR "${CMAKE_SOURCE_DIR}/.deps-renderer/dxc-redist/v1.8.2505.1/bin/x64")
        if(NOT EXISTS "${SBK_DXC_REDIST_DIR}/dxc.exe" OR NOT EXISTS "${SBK_DXC_REDIST_DIR}/dxcompiler.dll")
            message(FATAL_ERROR "Pinned DirectX Shader Compiler release missing. Run: python scripts/bootstrap.py --only dxc")
        endif()
        set(RT64_DXC_EXECUTABLE "${SBK_DXC_REDIST_DIR}/dxc.exe")
    endif()

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
        set(DXC "${RT64_DXC_EXECUTABLE}")
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

    # On Windows RecompFrontend reads SDL2 headers from sdl2_SOURCE_DIR; use
    # the SDL2 development tree RT64 already links on Windows.
    if(WIN32 AND NOT sdl2_SOURCE_DIR)
        set(sdl2_SOURCE_DIR "${RT64_DIR}/src/contrib/mupen64plus-win32-deps/SDL2-2.26.3")
    endif()

    add_subdirectory(
        "${RECOMP_FRONTEND_DIR}"
        "${CMAKE_BINARY_DIR}/RecompFrontend"
    )

    # Compile project-owned styling. Upstream theme checkout supplies only
    # explicitly licensed fonts to the staging allowlist.
    set(SNOWBOARDKIDS_THEME_SOURCES "${CMAKE_SOURCE_DIR}/src/ui/recomp_theme.cpp")
    set(SNOWBOARDKIDS_THEME_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/src/ui")

    add_custom_target(SnowboardKidsRendererStack
        DEPENDS rt64 recompui recompinput
    )
endif()


option(SBK_BUILD_NATIVE_BOOT "Build the first graphical Snowboard Kids native boot executable" OFF)

if(SBK_BUILD_NATIVE_BOOT)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
    endif()
    if(NOT SBK_BUILD_RENDERER_STACK)
        message(FATAL_ERROR "SBK_BUILD_NATIVE_BOOT requires SBK_BUILD_RENDERER_STACK=ON")
    endif()

    find_package(Threads REQUIRED)

    # Windows: SDL2 and FreeType come from the prebuilt trees pinned inside
    # RT64/RecompFrontend. SDL2's config package gives SDL2::SDL2 a DLL
    # location, so the runtime DLLs can be staged beside the executable.
    if(WIN32)
        if(NOT TARGET SDL2::SDL2)
            find_package(SDL2 CONFIG REQUIRED
                PATHS "${RT64_DIR}/src/contrib/mupen64plus-win32-deps/SDL2-2.26.3/cmake"
                NO_DEFAULT_PATH)
        endif()
        if(NOT TARGET Freetype::Freetype)
            set(SBK_FREETYPE_WIN_DIR "${RECOMP_FRONTEND_DIR}/recompui/lib/freetype-windows-binaries")
            add_library(Freetype::Freetype STATIC IMPORTED)
            set_target_properties(Freetype::Freetype PROPERTIES
                IMPORTED_LOCATION "${SBK_FREETYPE_WIN_DIR}/release static/vs2015-2022/win64/freetype.lib"
                INTERFACE_INCLUDE_DIRECTORIES "${SBK_FREETYPE_WIN_DIR}/include")
        endif()
    endif()

    set(SBK_APP_SOURCES
        "${CMAKE_SOURCE_DIR}/src/main/native_boot.cpp"
        "${CMAKE_SOURCE_DIR}/src/main/virtual_pad.cpp"
        "${CMAKE_SOURCE_DIR}/src/quiescence/probe.cpp"
        "${CMAKE_SOURCE_DIR}/src/main/register_overlays.cpp"
        "${CMAKE_SOURCE_DIR}/src/main/runtime_compat.cpp"
        "${CMAKE_SOURCE_DIR}/src/pfs/controller_pak.cpp"
        "${CMAKE_SOURCE_DIR}/src/pfs/hle.cpp"
        "${CMAKE_SOURCE_DIR}/src/module/module_loader.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/menu.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/ux_settings.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/first_run_process.cpp"
        "${CMAKE_SOURCE_DIR}/src/ui/first_run_window.cpp"
        ${SNOWBOARDKIDS_THEME_SOURCES}
    )

    # SnowboardKidsEngine is always built. The monolithic executable links the
    # ROM-derived CPU/RSP libraries and is skipped in engine-only mode.
    set(SBK_APP_TARGETS SnowboardKidsEngine)
    if(NOT SBK_ENGINE_ONLY)
        list(APPEND SBK_APP_TARGETS SnowboardKidsRecompiled)
        add_executable(SnowboardKidsRecompiled ${SBK_APP_SOURCES})
    endif()
    add_executable(SnowboardKidsEngine ${SBK_APP_SOURCES})
    target_compile_definitions(SnowboardKidsEngine PRIVATE SBK_ROM_FREE_ENGINE=1)

    enable_testing()
    add_executable(SnowboardKidsAudioProgressTest tests/compat/audio_progress.cpp)
    target_include_directories(SnowboardKidsAudioProgressTest PRIVATE src)
    add_test(NAME compat_audio_progress COMMAND SnowboardKidsAudioProgressTest)
    add_executable(SnowboardKidsHostGainTest tests/audio/host_gain.cpp)
    target_include_directories(SnowboardKidsHostGainTest PRIVATE src)
    add_test(NAME audio_host_gain COMMAND SnowboardKidsHostGainTest)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    sbk_add_controller_pak_test()

    add_library(SnowboardKidsSyntheticModule SHARED
        tests/module/synthetic_module.cpp
    )
    target_include_directories(SnowboardKidsSyntheticModule PRIVATE src)

    add_executable(SnowboardKidsModuleLoaderTest
        tests/module/test_module_loader.cpp
        src/module/module_loader.cpp
    )
    target_include_directories(SnowboardKidsModuleLoaderTest PRIVATE src)
    target_link_libraries(SnowboardKidsModuleLoaderTest PRIVATE ${CMAKE_DL_LIBS})
    if(CMAKE_SYSTEM_NAME MATCHES "Linux")
        target_link_options(SnowboardKidsModuleLoaderTest PRIVATE -rdynamic)
    endif()
    add_test(NAME module_loader_synthetic
        COMMAND SnowboardKidsModuleLoaderTest "$<TARGET_FILE:SnowboardKidsSyntheticModule>")
    add_test(NAME module_builder_unit
        COMMAND "${Python3_EXECUTABLE}" -m unittest discover -s "${CMAKE_SOURCE_DIR}/tests/module" -p "test_builder_unit.py")
    add_executable(SnowboardKidsFirstRunProcessTest tests/module/test_first_run.cpp src/ui/first_run_process.cpp)
    target_include_directories(SnowboardKidsFirstRunProcessTest PRIVATE "${CMAKE_SOURCE_DIR}")
    add_test(NAME ui_first_run_process COMMAND SnowboardKidsFirstRunProcessTest)
    add_executable(SnowboardKidsMenuActionsTest tests/frontend/menu_actions.cpp)
    target_include_directories(SnowboardKidsMenuActionsTest PRIVATE src)
    find_package(Threads REQUIRED)
    target_link_libraries(SnowboardKidsMenuActionsTest PRIVATE Threads::Threads)
    add_test(NAME ui_menu_actions COMMAND SnowboardKidsMenuActionsTest)
    add_test(NAME ui_first_run_protocol
        COMMAND "${Python3_EXECUTABLE}" -m unittest discover -s "${CMAKE_SOURCE_DIR}/tests/module" -p "test_first_run_protocol.py")
    add_test(NAME ui_assets
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/release/test_ui_assets.py")

    foreach(SBK_EXE ${SBK_APP_TARGETS})
        add_dependencies(${SBK_EXE} sbk_version_header)
        if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" AND NOT MSVC)
            target_compile_options(${SBK_EXE} PRIVATE
                -fno-strict-aliasing
                -fms-extensions
                -fno-omit-frame-pointer
            )
        elseif(MSVC)
            target_compile_options(${SBK_EXE} PRIVATE /permissive-)
        endif()
        if(WIN32)
            # windows.h (via ultramodern) must not define min/max macros that
            # break std::min/std::max in RecompFrontend and project headers.
            target_compile_definitions(${SBK_EXE} PRIVATE NOMINMAX)
            # rt64 links "SDL2"/"SDL2main" by name with a directory-scoped
            # link_directories(); the executables need the same search path.
            target_link_directories(${SBK_EXE} PRIVATE
                "${RT64_DIR}/src/contrib/mupen64plus-win32-deps/SDL2-2.26.3/lib/x64")
        endif()
        if(CMAKE_SYSTEM_NAME MATCHES "Linux")
            target_link_options(${SBK_EXE} PRIVATE -rdynamic)
        endif()
    endforeach()

    foreach(SBK_EXE ${SBK_APP_TARGETS})
        target_include_directories(${SBK_EXE} PRIVATE
            "${CMAKE_SOURCE_DIR}"
            "${CMAKE_BINARY_DIR}/generated"
            "${N64_RUNTIME_DIR}/librecomp/include"
            "${N64_RUNTIME_DIR}/N64Recomp/include"
            "${N64_RUNTIME_DIR}/ultramodern/include"
            "${N64_RUNTIME_DIR}/thirdparty"
            "${RECOMP_FRONTEND_DIR}/recompui/include"
            "${RECOMP_FRONTEND_DIR}/recompui/src"
            "${RECOMP_FRONTEND_DIR}/recompinput/include"
            "${SNOWBOARDKIDS_THEME_INCLUDE_DIR}"
            "${RT64_DIR}/src"
            "${RT64_DIR}/src/contrib"
            "${RT64_DIR}/src/contrib/nativefiledialog-extended/src/include"
            "${CMAKE_BINARY_DIR}/shaders"
            ${SDL2_INCLUDE_DIRS}
        )
    endforeach()

    if(TARGET SnowboardKidsRecompiled)
        target_link_libraries(SnowboardKidsRecompiled PRIVATE SnowboardKidsCpu SnowboardKidsRsp)
    endif()
    # First-run setup draws with the FreeType build RmlUi already requires.
    if(NOT WIN32)
        find_package(Freetype REQUIRED)
    endif()
    foreach(SBK_EXE ${SBK_APP_TARGETS})
        target_link_libraries(${SBK_EXE} PRIVATE
            recompui
            recompinput
            recompui
            librecomp
            ultramodern
            rt64
            nfd
            SDL2::SDL2
            Threads::Threads
            Freetype::Freetype
            ${CMAKE_DL_LIBS}
        )
        if(CMAKE_SYSTEM_NAME MATCHES "Linux")
            target_link_libraries(${SBK_EXE} PRIVATE atomic)
        endif()
    endforeach()

    # PE/COFF has no -rdynamic: the game module DLL imports the runtime entry
    # points it needs from SnowboardKidsEngine.exe through an import library.
    # One reviewed list (src/module/engine_exports.inc) feeds this .def and the
    # module builder, so both sides agree on the exact exported surface.
    if(WIN32)
        file(STRINGS "${CMAKE_SOURCE_DIR}/src/module/engine_exports.inc" SBK_ENGINE_EXPORT_LINES
            REGEX "^SBK_ENGINE_EXPORT(_C)?\\(")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
            "${CMAKE_SOURCE_DIR}/src/module/engine_exports.inc")
        set(SBK_ENGINE_DEF_TEXT "EXPORTS\n")
        foreach(SBK_LINE ${SBK_ENGINE_EXPORT_LINES})
            string(REGEX REPLACE "^SBK_ENGINE_EXPORT(_C)?\\(([A-Za-z0-9_]+)\\).*" "\\2" SBK_SYMBOL "${SBK_LINE}")
            string(APPEND SBK_ENGINE_DEF_TEXT "    ${SBK_SYMBOL}\n")
        endforeach()
        set(SBK_ENGINE_DEF "${CMAKE_BINARY_DIR}/generated/SnowboardKidsEngine.def")
        file(CONFIGURE OUTPUT "${SBK_ENGINE_DEF}" CONTENT "${SBK_ENGINE_DEF_TEXT}")
        set_target_properties(SnowboardKidsEngine PROPERTIES ENABLE_EXPORTS ON)
        target_link_options(SnowboardKidsEngine PRIVATE "/DEF:${SBK_ENGINE_DEF}")
        set_property(TARGET SnowboardKidsEngine APPEND PROPERTY LINK_DEPENDS "${SBK_ENGINE_DEF}")

        # Runtime DLLs the executables load at startup: SDL2 (imported target)
        # and dxcompiler.dll from the pinned DXC release (SBK_DXC_REDIST_DIR,
        # above), which rt64 links by import library. No dxil.dll: this release
        # never loads it. Release staging re-derives the set from the PE
        # imports and checks the DXC hash.
        foreach(SBK_EXE ${SBK_APP_TARGETS})
            add_custom_command(TARGET ${SBK_EXE} POST_BUILD
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    $<TARGET_RUNTIME_DLLS:${SBK_EXE}>
                    "${SBK_DXC_REDIST_DIR}/dxcompiler.dll"
                    "$<TARGET_FILE_DIR:${SBK_EXE}>"
                COMMAND_EXPAND_LISTS)
        endforeach()

        # ROM-free gate on RT64's run-time DXIL compile+link path: from its own
        # directory (dxcompiler.dll, no dxil.dll) it links RT64's shaders like
        # RasterShader does, verifies they are hashed, that D3D12 (WARP) accepts
        # them and that dxil.dll never loads (tests/renderer/dxc_runtime_probe.cpp).
        add_executable(SnowboardKidsDxcRuntimeProbe tests/renderer/dxc_runtime_probe.cpp)
        set_target_properties(SnowboardKidsDxcRuntimeProbe PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/dxc-probe")
        target_compile_definitions(SnowboardKidsDxcRuntimeProbe PRIVATE NOMINMAX)
        target_include_directories(SnowboardKidsDxcRuntimeProbe PRIVATE
            "${RT64_DIR}/src" "${RT64_DIR}/src/contrib" "${RT64_DIR}/src/contrib/plume"
            "${RT64_DIR}/src/contrib/hlslpp/include" "${RT64_DIR}/src/contrib/dxc/inc"
            "${CMAKE_BINARY_DIR}/src")
        target_link_directories(SnowboardKidsDxcRuntimeProbe PRIVATE
            "${RT64_DIR}/src/contrib/mupen64plus-win32-deps/SDL2-2.26.3/lib/x64")
        target_link_libraries(SnowboardKidsDxcRuntimeProbe PRIVATE rt64 d3d12 dxgi)
        add_custom_command(TARGET SnowboardKidsDxcRuntimeProbe POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                $<TARGET_RUNTIME_DLLS:SnowboardKidsDxcRuntimeProbe>
                "${SBK_DXC_REDIST_DIR}/dxcompiler.dll"
                "$<TARGET_FILE_DIR:SnowboardKidsDxcRuntimeProbe>"
            COMMAND_EXPAND_LISTS)
        add_test(NAME renderer_dxc_runtime
            COMMAND SnowboardKidsDxcRuntimeProbe "${CMAKE_BINARY_DIR}/dxc-probe/out")
    endif()

    add_executable(SnowboardKidsFirstRunUITest
        tests/frontend/first_run_ui.cpp
        src/ui/first_run_process.cpp
        src/ui/first_run_window.cpp
    )
    target_include_directories(SnowboardKidsFirstRunUITest PRIVATE src ${SDL2_INCLUDE_DIRS})
    target_link_libraries(SnowboardKidsFirstRunUITest PRIVATE SDL2::SDL2 Freetype::Freetype)
    # The test owns main(); on Windows SDL.h would otherwise rename it SDL_main.
    target_compile_definitions(SnowboardKidsFirstRunUITest PRIVATE SDL_MAIN_HANDLED)
    add_test(NAME ui_first_run_layout
        COMMAND SnowboardKidsFirstRunUITest "$<TARGET_FILE_DIR:SnowboardKidsEngine>/assets")
    set_tests_properties(ui_first_run_layout PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy")
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        # Host harnesses that compile with clang++/ASan against system SDL2.
        add_test(NAME ui_graphics_config
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/graphics_config/run.py")
        add_test(NAME ui_ux_config
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/ux_config/run.py")
        add_test(NAME input_capture_events
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/controls/run_capture_events.py")
    endif()
    # ROM-free: a synthetic module built by the module builder toolchain must
    # load, validate and initialize in the real engine (PE imports on Windows).
    add_test(NAME module_engine_probe
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/module/run_engine_probe.py"
            --engine "$<TARGET_FILE:SnowboardKidsEngine>")
    set_tests_properties(module_engine_probe PROPERTIES TIMEOUT 600)

    # Both executables share a build directory. Stage once so parallel links
    # cannot replace the same assets directory at the same time.
    add_custom_target(SnowboardKidsAssets
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/stage_ui_assets.py"
            --out "${CMAKE_BINARY_DIR}/assets"
        COMMENT "Staging reviewed fonts and original SBK UI assets"
    )
    foreach(SBK_EXE ${SBK_APP_TARGETS})
        add_dependencies(${SBK_EXE} SnowboardKidsAssets)
    endforeach()
endif()

# P2 coordinator remains inert unless SBK_P2_CYCLES is set at runtime.
if(TARGET ultramodern)
    file(STRINGS "${N64_RUNTIME_DIR}/ultramodern/src/events.cpp" P2_RUNTIME_HOOK
        REGEX "quiescence::vi_boundary")
    if(NOT P2_RUNTIME_HOOK)
        message(FATAL_ERROR "P2 runtime patch missing. Run: python3 scripts/apply-quiescence-patches.py")
    endif()
    target_sources(ultramodern PRIVATE "${CMAKE_SOURCE_DIR}/src/quiescence/quiescence.cpp")
    target_include_directories(ultramodern PUBLIC "${CMAKE_SOURCE_DIR}/src")
endif()

option(SBK_BUILD_QUIESCENCE_TESTS "Build the standalone P2 barrier stress tests" OFF)
if(SBK_BUILD_QUIESCENCE_TESTS)
    find_package(Threads REQUIRED)
    enable_testing()
    add_executable(SnowboardKidsQuiescenceTest
        tests/quiescence/main.cpp src/quiescence/quiescence.cpp)
    target_link_libraries(SnowboardKidsQuiescenceTest PRIVATE Threads::Threads)
    target_include_directories(SnowboardKidsQuiescenceTest PRIVATE src)
    add_test(NAME runtime_quiescence COMMAND SnowboardKidsQuiescenceTest)
    set_tests_properties(runtime_quiescence PROPERTIES TIMEOUT 60)
    add_executable(SnowboardKidsQuiescenceLifecycleTest
        tests/quiescence/lifecycle.cpp src/quiescence/quiescence.cpp)
    target_link_libraries(SnowboardKidsQuiescenceLifecycleTest PRIVATE Threads::Threads)
    target_include_directories(SnowboardKidsQuiescenceLifecycleTest PRIVATE src)
    add_test(NAME runtime_quiescence_lifecycle COMMAND SnowboardKidsQuiescenceLifecycleTest)
    add_test(NAME runtime_quiescence_lifecycle_late COMMAND SnowboardKidsQuiescenceLifecycleTest --late-enable)
    set_tests_properties(runtime_quiescence_lifecycle runtime_quiescence_lifecycle_late PROPERTIES TIMEOUT 60)
    add_executable(SnowboardKidsQuiescenceKernelTest
        tests/quiescence/kernel.cpp src/quiescence/quiescence.cpp
        "${N64_RUNTIME_DIR}/ultramodern/src/threads.cpp"
        "${N64_RUNTIME_DIR}/ultramodern/src/threadqueue.cpp"
        "${N64_RUNTIME_DIR}/ultramodern/src/scheduling.cpp"
        "${N64_RUNTIME_DIR}/ultramodern/src/mesgqueue.cpp"
        "${N64_RUNTIME_DIR}/ultramodern/src/timer.cpp")
    target_include_directories(SnowboardKidsQuiescenceKernelTest PRIVATE src
        "${N64_RUNTIME_DIR}/ultramodern/include" "${N64_RUNTIME_DIR}/thirdparty"
        "${N64_RUNTIME_DIR}/thirdparty/concurrentqueue")
    target_link_libraries(SnowboardKidsQuiescenceKernelTest PRIVATE Threads::Threads)
    add_test(NAME runtime_quiescence_kernel COMMAND SnowboardKidsQuiescenceKernelTest)
    add_test(NAME runtime_quiescence_kernel_baseline COMMAND SnowboardKidsQuiescenceKernelTest --baseline)
    set_tests_properties(runtime_quiescence_kernel runtime_quiescence_kernel_baseline PROPERTIES TIMEOUT 60)
endif()
if(TARGET recompui)
    target_include_directories(recompui PRIVATE "${CMAKE_SOURCE_DIR}/src")
endif()

# Development A/B selection; production acceptance must explicitly select ON.
if(SBK_CONTINUATIONS)
    if(NOT TARGET SnowboardKidsEngine)
        message(FATAL_ERROR "P4-A backend requires the native-boot application targets")
    endif()
    foreach(SBK_TARGET ${SBK_APP_TARGETS} librecomp ultramodern)
        target_compile_definitions(${SBK_TARGET} PRIVATE SBK_CONTINUATIONS=1)
        target_include_directories(${SBK_TARGET} PRIVATE
            "${CMAKE_SOURCE_DIR}/src" "${N64_RUNTIME_DIR}/N64Recomp/include")
    endforeach()
    foreach(SBK_EXE ${SBK_APP_TARGETS})
        target_sources(${SBK_EXE} PRIVATE
            src/continuation/dispatch.cpp src/continuation/execution.cpp
            src/continuation/hle.cpp src/continuation/runtime_owner.cpp)
    endforeach()
    # P4 in-memory savestates, P5 audio boundary, P6 .sbks persistence and
    # P7 quick save/load (F5/F8); development trigger behind SBK_P4_SAVESTATE_DEV.
    file(STRINGS "${N64_RUNTIME_DIR}/ultramodern/src/threads.cpp" P4_SAVESTATE_HOOK
        REGEX "spawn_restored_thread")
    if(NOT P4_SAVESTATE_HOOK)
        message(FATAL_ERROR "P4 savestate runtime patch missing. Run: python3 scripts/apply-continuation-patches.py")
    endif()
    file(STRINGS "${N64_RUNTIME_DIR}/librecomp/src/recomp.cpp" SBK_SHUTDOWN_PATCH REGEX "RDRAM stays mapped")
    if(NOT SBK_SHUTDOWN_PATCH)
        message(FATAL_ERROR "Runtime shutdown patch missing. Run: python3 scripts/apply-continuation-patches.py")
    endif()
    foreach(SBK_EXE ${SBK_APP_TARGETS})
        target_sources(${SBK_EXE} PRIVATE
            src/savestate/snapshot.cpp src/savestate/service.cpp
            src/savestate/runtime_domains.cpp src/savestate/app_domains.cpp
            src/savestate/host_audio.cpp src/savestate/sbks.cpp src/savestate/storage.cpp
            src/savestate/driver.cpp src/savestate/toast.cpp src/savestate/dev_trigger.cpp)
    endforeach()
    if(TARGET SnowboardKidsRecompiled)
        target_include_directories(SnowboardKidsRecompiled PRIVATE "${SBK_CPU_OUTPUT_DIR}")
    endif()
endif()
