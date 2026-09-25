#include "module/module_abi.h"

#include <stdio.h>
#include <string>
#include <vector>

#if defined(_WIN32)
#define SBK_EXPORT __declspec(dllexport)
#else
#define SBK_EXPORT __attribute__((visibility("default")))
#endif

static int g_init_called = 0;
static int g_shutdown_called = 0;
static const SbkEngineApiV1* g_engine = NULL;

static void synthetic_entrypoint(uint8_t* rdram, struct recomp_context* ctx) {
    if (rdram) {
        rdram[0] = 0xAA;
        rdram[1] = 0xBB;
        rdram[2] = 0xCC;
        rdram[3] = 0xDD;
    }
}

static uint32_t synthetic_rsp_microcode(uint8_t* rdram, uint32_t ucode_addr) {
    (void)rdram;
    (void)ucode_addr;
    return 0;
}

static SbkRspUcodeFunc synthetic_get_rsp_microcode(const OSTask* task) {
    (void)task;
    return synthetic_rsp_microcode;
}

static void synthetic_register_overlays(void) {
    /* No overlays in synthetic module */
}

static int synthetic_init(const SbkEngineApiV1* engine) {
    g_init_called = 1;
    g_engine = engine;
    return 0;
}

static void synthetic_shutdown(void) {
    g_shutdown_called = 1;
}

static SbkAction synthetic_step_1(uint8_t* rdram, struct recomp_context* ctx, SbkFrame* frame) {
    (void)rdram;
    (void)ctx;
    if (frame->continuation == 0) {
        frame->continuation = 101;
        frame->scratch[0] = 42;
        SbkAction act;
        act.kind = SBK_ACTION_YIELD;
        act.tail = 0;
        act.target = 0;
        return act;
    } else if (frame->continuation == 101) {
        frame->continuation = 0;
        frame->result = frame->scratch[0] * 2;
        SbkAction act;
        act.kind = SBK_ACTION_RETURN;
        act.tail = 0;
        act.target = 0;
        return act;
    }
    SbkAction act;
    act.kind = SBK_ACTION_RETURN;
    act.tail = 0;
    act.target = 0;
    return act;
}

static void dummy_token_1(uint8_t* rdram, struct recomp_context* ctx) {
    (void)rdram;
    (void)ctx;
}

static const SbkContinuationDescriptor g_synthetic_continuations[] = {
    {
        .id = 1001ull,
        .guest_address = 0x80001000u,
        .scratch_count = 1,
        .step = synthetic_step_1,
        .token = dummy_token_1,
        .name = "synthetic_func_1"
    }
};

static SbkGameModuleApiV1 g_api = {
    .magic = SBK_MODULE_MAGIC,
    .abi_version = SBK_MODULE_ABI_VERSION,
    .struct_size = sizeof(SbkGameModuleApiV1),
    .game_id = "snowboardkids.n64.us",
    .internal_name = "SNOWBOARD KIDS (SYNTHETIC FIXTURE)",
    .rom_hash = 0xF384619787B78D4BULL,
    .corpus_digest = 0x1234567890ABCDEFULL,
    .function_count = 1,
    .hle_count = 0,
    .entrypoint_address = 0x80000400u,
    .init = synthetic_init,
    .shutdown = synthetic_shutdown,
    .entrypoint = synthetic_entrypoint,
    .get_rsp_microcode = synthetic_get_rsp_microcode,
    .register_overlays = synthetic_register_overlays,
    .continuation_count = 1,
    .continuations = g_synthetic_continuations,
    .step = NULL
};

extern "C" SBK_EXPORT const SbkGameModuleApiV1* sbk_game_module_get_api(void) {
    return &g_api;
}
