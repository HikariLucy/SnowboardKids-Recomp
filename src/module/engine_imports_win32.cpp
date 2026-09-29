// Windows game module only: engine runtime addresses seen through dllimport.
//
// The generated corpus declares runtime functions without dllimport, so taking
// their address inside the DLL yields a local import thunk rather than the
// engine's function. Continuation dispatch compares those addresses with the
// engine's HLE table, so game_module_entry.cpp rebinds its function tables to
// the addresses collected here. This file must not include funcs.h: a later
// declaration without dllimport would silently drop the attribute.
#if defined(_WIN32)

#include "recomp.h"
#include "engine_imports_win32.hpp"

extern "C" {
#define SBK_ENGINE_EXPORT(name) __declspec(dllimport) void name(uint8_t* rdram, recomp_context* ctx);
#define SBK_ENGINE_EXPORT_C(name)
#include "engine_exports.inc"
#undef SBK_ENGINE_EXPORT
#undef SBK_ENGINE_EXPORT_C
}

namespace sbk::module_win32 {

const EngineImport engine_imports[] = {
#define SBK_ENGINE_EXPORT(name) {#name, &name},
#define SBK_ENGINE_EXPORT_C(name)
#include "engine_exports.inc"
#undef SBK_ENGINE_EXPORT
#undef SBK_ENGINE_EXPORT_C
};

const size_t engine_import_count = sizeof(engine_imports) / sizeof(engine_imports[0]);

} // namespace sbk::module_win32

#endif
