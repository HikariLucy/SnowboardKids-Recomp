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

    recomp::overlays::register_overlays(sections, overlays);
#endif
}

static SbkRspUcodeFunc module_get_rsp_microcode(const OSTask* task) {
    if (task && task->t.type == M_AUDTASK) {
        return reinterpret_cast<SbkRspUcodeFunc>(aspMain);
    }
    return nullptr;
}

static int module_init(const SbkEngineApiV1* engine) {
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
