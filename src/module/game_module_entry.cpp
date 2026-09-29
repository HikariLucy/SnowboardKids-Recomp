#include "module_abi.h"

#include "librecomp/rsp.hpp"
#include "librecomp/overlays.hpp"
#include "continuation/dispatch.hpp"

#if defined(__has_include)
#if __has_include("recomp_overlays.inl")
#include "recomp_overlays.inl"
#define SBK_HAS_RECOMP_OVERLAYS 1
#elif __has_include("../../RecompiledFuncs/recomp_overlays.inl")
#include "../../RecompiledFuncs/recomp_overlays.inl"
#define SBK_HAS_RECOMP_OVERLAYS 1
#elif __has_include("../build-tools/production-continuation/corpus/recomp_overlays.inl")
#include "../build-tools/production-continuation/corpus/recomp_overlays.inl"
#define SBK_HAS_RECOMP_OVERLAYS 1
#endif
#endif

#include <cstdio>
#include <cstdlib>
#include <vector>

#if defined(_WIN32)
#define SBK_EXPORT __declspec(dllexport)
#else
#define SBK_EXPORT __attribute__((visibility("default")))
#endif

extern "C" void recomp_entrypoint(uint8_t* rdram, recomp_context* ctx);
gpr get_entrypoint_address();
extern RspExitReason aspMain(uint8_t* rdram, uint32_t ucode_addr);

// Weak fallback definition of RSP dmem so the module can load in test fixtures
// and environments without hard runtime dependency, while binding to engine dmem when present.
#if !defined(_WIN32)
__attribute__((weak)) uint8_t dmem[0x1000];
#endif

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "engine_imports_win32.hpp"

// A DLL cannot interpose the engine's RSP DMEM the way an ELF module does.
// The RSP microcode translation unit is compiled with dmem=(*sbk_module_dmem),
// so librecomp's RSP accessors read the engine buffer handed over in init().
uint8_t (*sbk_module_dmem)[] = nullptr;

extern "C" void sbk_engine_register_overlays(
    const recomp::overlays::overlay_section_table_data_t* sections,
    const recomp::overlays::overlays_by_index_t* overlays);

extern "C" {
#define SBK_ENGINE_EXPORT(name) void name(uint8_t* rdram, recomp_context* ctx);
#define SBK_ENGINE_EXPORT_C(name)
#include "engine_exports.inc"
#undef SBK_ENGINE_EXPORT
#undef SBK_ENGINE_EXPORT_C
}

// Addresses as this DLL sees them without dllimport: its own import thunks.
static recomp_func_t* const module_import_thunks[] = {
#define SBK_ENGINE_EXPORT(name) &name,
#define SBK_ENGINE_EXPORT_C(name)
#include "engine_exports.inc"
#undef SBK_ENGINE_EXPORT
#undef SBK_ENGINE_EXPORT_C
};
#endif

namespace sbk::continuation {

static void (*g_engine_enter)(uint64_t, uint8_t*, recomp_context*) = nullptr;

void enter(uint64_t id, uint8_t* rdram, recomp_context* context) {
    if (g_engine_enter) {
        g_engine_enter(id, rdram, context);
    } else {
        std::fprintf(stderr, "Fatal: Engine continuation_enter callback uninitialized! ID: 0x%llX\n",
                     static_cast<unsigned long long>(id));
        std::abort();
    }
}

static std::vector<SbkContinuationDescriptor>& get_module_continuations() {
    static std::vector<SbkContinuationDescriptor> s_table;
    return s_table;
}

bool register_function(Descriptor d) {
    SbkContinuationDescriptor raw{};
    raw.id = d.id;
    raw.guest_address = d.guest_address;
    raw.scratch_count = static_cast<uint32_t>(d.scratch_count);
    raw.step = reinterpret_cast<SbkStepFn>(d.step);
    raw.token = reinterpret_cast<void(*)(uint8_t*, recomp_context*)>(d.token);
    raw.name = d.name;
    get_module_continuations().push_back(raw);
    return true;
}

} // namespace sbk::continuation

namespace {

static const SbkEngineApiV1* g_engine = nullptr;

static void (*g_engine_switch_error)(const char*, uint32_t, uint32_t) = nullptr;

extern "C" void switch_error(const char* section, uint32_t jtbl_addr, uint32_t target) {
    if (g_engine_switch_error) {
        g_engine_switch_error(section, jtbl_addr, target);
    } else {
        std::fprintf(stderr, "Jump table error in %s: jtbl 0x%08X, target 0x%08X\n",
                     section ? section : "unknown", jtbl_addr, target);
        std::abort();
    }
}

template <typename T, std::size_t N>
constexpr std::size_t array_count(const T (&)[N]) {
    return N;
}

static void module_register_overlays(void) {
#if defined(SBK_HAS_RECOMP_OVERLAYS)
    recomp::overlays::overlay_section_table_data_t sections{
        .code_sections = section_table,
        .num_code_sections = array_count(section_table),
        .total_num_sections = num_sections,
    };

    recomp::overlays::overlays_by_index_t overlays{
        .table = overlay_sections_by_index,
        .len = array_count(overlay_sections_by_index),
    };

#if defined(_WIN32)
    sbk_engine_register_overlays(&sections, &overlays);
#else
    recomp::overlays::register_overlays(sections, overlays);
#endif
#endif
}

#if defined(_WIN32)
// Verifies the engine export surface and points the function tables at the
// engine's runtime functions, so HLE token identity matches the ELF build.
static bool bind_engine_imports(const SbkEngineApiV1* engine) {
    if (!engine || !engine->dmem) {
        std::fprintf(stderr, "Engine did not provide RSP DMEM to the game module\n");
        return false;
    }
    sbk_module_dmem = reinterpret_cast<uint8_t (*)[]>(engine->dmem);

    using sbk::module_win32::engine_imports;
    if (array_count(module_import_thunks) != sbk::module_win32::engine_import_count) {
        std::fprintf(stderr, "Game module import tables disagree\n");
        return false;
    }
    HMODULE engine_exe = GetModuleHandleW(nullptr);
    for (size_t i = 0; i < sbk::module_win32::engine_import_count; ++i) {
        const auto exported = reinterpret_cast<recomp_func_t*>(GetProcAddress(engine_exe, engine_imports[i].name));
        if (!exported || exported != engine_imports[i].engine_address) {
            std::fprintf(stderr, "Engine executable does not export %s to the game module\n",
                         engine_imports[i].name);
            return false;
        }
    }

#if defined(SBK_HAS_RECOMP_OVERLAYS)
    for (auto& section : section_table) {
        for (size_t f = 0; f < section.num_funcs; ++f) {
            for (size_t i = 0; i < sbk::module_win32::engine_import_count; ++i) {
                if (section.funcs[f].func == module_import_thunks[i]) {
                    section.funcs[f].func = engine_imports[i].engine_address;
                    break;
                }
            }
        }
    }
#endif
    return true;
}
#endif

static SbkRspUcodeFunc module_get_rsp_microcode(const OSTask* task) {
    if (task && task->t.type == M_AUDTASK) {
        return reinterpret_cast<SbkRspUcodeFunc>(aspMain);
    }
    return nullptr;
}

static int module_init(const SbkEngineApiV1* engine) {
#if defined(_WIN32)
    if (!bind_engine_imports(engine)) {
        return 1;
    }
#endif
    g_engine = engine;
    if (engine) {
        if (engine->switch_error) {
            g_engine_switch_error = engine->switch_error;
        }
        if (engine->continuation_enter) {
            sbk::continuation::g_engine_enter = engine->continuation_enter;
        }
    }
    return 0;
}

static void module_shutdown(void) {
    g_engine = nullptr;
    g_engine_switch_error = nullptr;
    sbk::continuation::g_engine_enter = nullptr;
}

static void module_entrypoint(uint8_t* rdram, recomp_context* ctx) {
    recomp_entrypoint(rdram, ctx);
}

} // namespace

extern "C" SBK_EXPORT const SbkGameModuleApiV1* sbk_game_module_get_api(void) {
    static const uint64_t kCorpusDigest = 0x76260cb8f0e080d7ULL; // Manifest hash

    const auto& continuations = sbk::continuation::get_module_continuations();

    static SbkGameModuleApiV1 api{};
    api.magic = SBK_MODULE_MAGIC;
    api.abi_version = SBK_MODULE_ABI_VERSION;
    api.struct_size = sizeof(SbkGameModuleApiV1);
    api.game_id = "snowboardkids.n64.us";
    api.internal_name = "SNOWBOARD KIDS";
    api.rom_hash = 0xF384619787B78D4BULL;
    api.corpus_digest = kCorpusDigest;
    api.function_count = static_cast<uint32_t>(continuations.size() > 0 ? continuations.size() : 1981);
    api.hle_count = 56;
    api.entrypoint_address = static_cast<uint32_t>(get_entrypoint_address());
    api.init = module_init;
    api.shutdown = module_shutdown;
    api.entrypoint = module_entrypoint;
    api.get_rsp_microcode = module_get_rsp_microcode;
    api.register_overlays = module_register_overlays;
    api.continuation_count = continuations.size();
    api.continuations = continuations.data();
    api.step = nullptr;

    return &api;
}
