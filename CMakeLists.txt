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
