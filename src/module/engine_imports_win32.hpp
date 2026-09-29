#pragma once

#include <stddef.h>

#include "recomp.h"

namespace sbk::module_win32 {

struct EngineImport {
    const char* name;
    recomp_func_t* engine_address; // resolved through the module's import table
};

// Same order as the SBK_ENGINE_EXPORT entries in engine_exports.inc.
extern const EngineImport engine_imports[];
extern const size_t engine_import_count;

} // namespace sbk::module_win32
