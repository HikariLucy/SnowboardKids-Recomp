#ifndef SBK_MODULE_ABI_H
#define SBK_MODULE_ABI_H

#include <stddef.h>
#include <stdint.h>

#if defined(__has_include)
#if __has_include("recomp.h")
#include "recomp.h"
#endif
#if __has_include("ultramodern/ultra64.h")
#include "ultramodern/ultra64.h"
#endif
#endif

#ifndef __RECOMP_H__
typedef struct recomp_context recomp_context;
#endif

#ifndef __ULTRA64_ultramodern_H__
typedef union OSTask {
    struct {
        uint32_t type;
        uint32_t flags;
    } t;
    int64_t force_structure_alignment;
} OSTask;
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define SBK_MODULE_MAGIC 0x3130444F4D4B4253ULL /* "SBKMOD01" in LE */
#define SBK_MODULE_ABI_VERSION 1
#define SBK_MODULE_EXPORT_SYMBOL "sbk_game_module_get_api"

/* Action kinds for continuation step results */
typedef enum SbkActionKind {
    SBK_ACTION_RETURN = 0,
    SBK_ACTION_CALL   = 1,
    SBK_ACTION_LOOKUP = 2,
    SBK_ACTION_HLE    = 3,
    SBK_ACTION_YIELD  = 4,
    SBK_ACTION_PAUSE  = 5
} SbkActionKind;

/* Explicit C-ABI continuation action */
typedef struct SbkAction {
    uint32_t kind;       /* SbkActionKind */
    uint32_t tail;       /* 1 if tail call, 0 otherwise */
    uint64_t target;
} SbkAction;

/* Explicit C-ABI continuation frame */
typedef struct SbkFrame {
    uint64_t function;
    uint64_t continuation;
    uint64_t hi;
    uint64_t lo;
    uint64_t result;
    int32_t  c1cs;
    uint32_t indirect_target;
    uint32_t scratch_count;
    uint32_t reserved;
    uint64_t scratch[8];
} SbkFrame;

/* C step function pointer */
typedef SbkAction (*SbkStepFn)(uint8_t* rdram, recomp_context* ctx, SbkFrame* frame);

/* Continuation descriptor in C */
typedef struct SbkContinuationDescriptor {
    uint64_t id;
    uint32_t guest_address;
    uint32_t scratch_count;
    SbkStepFn step;
    void (*token)(uint8_t*, recomp_context*);
    const char* name;
} SbkContinuationDescriptor;

/* Engine callbacks provided to module during init */
typedef struct SbkEngineApiV1 {
    uint32_t abi_version;
    uint32_t struct_size;
    void (*switch_error)(const char* section, uint32_t jtbl_addr, uint32_t target);
    uint8_t* dmem; /* Pointer to 4KB RSP DMEM buffer */
    void (*continuation_enter)(uint64_t id, uint8_t* rdram, recomp_context* ctx);
} SbkEngineApiV1;

/* RSP ucode function signature matching librecomp::RspUcodeFunc */
typedef uint32_t (*SbkRspUcodeFunc)(uint8_t* rdram, uint32_t ucode_addr);

/* Module API exported by the game module */
typedef struct SbkGameModuleApiV1 {
    uint64_t magic;              /* SBK_MODULE_MAGIC */
    uint32_t abi_version;        /* SBK_MODULE_ABI_VERSION */
    uint32_t struct_size;        /* sizeof(SbkGameModuleApiV1) */

    /* Identification metadata */
    const char* game_id;         /* "snowboardkids.n64.us" */
    const char* internal_name;   /* "SNOWBOARD KIDS" */
    uint64_t    rom_hash;        /* 0xF384619787B78D4BULL */
    uint64_t    corpus_digest;   /* XXH3-64 of continuation manifest */
    uint32_t    function_count;  /* 1981 */
    uint32_t    hle_count;       /* 56 */
    uint32_t    entrypoint_address; /* 0x80000400 */

    /* Module lifecycle */
    int  (*init)(const SbkEngineApiV1* engine);
    void (*shutdown)(void);

    /* Game Execution */
    void (*entrypoint)(uint8_t* rdram, recomp_context* ctx);

    /* RSP microcode resolver */
    SbkRspUcodeFunc (*get_rsp_microcode)(const OSTask* task);

    /* Overlays */
    void (*register_overlays)(void);

    /* Continuation dispatch table */
    size_t continuation_count;
    const SbkContinuationDescriptor* continuations;
    SbkAction (*step)(uint8_t* rdram, recomp_context* ctx, SbkFrame* frame);
} SbkGameModuleApiV1;

typedef const SbkGameModuleApiV1* (*SbkGetGameModuleApiFn)(void);

#ifdef __cplusplus
}
#endif

#endif /* SBK_MODULE_ABI_H */
