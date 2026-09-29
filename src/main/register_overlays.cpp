#include "module/module_abi.h"
#include "module/module_loader.hpp"

#include "librecomp/overlays.hpp"

namespace sbk {

extern module::GameModule g_game_module;

void register_overlays() {
    if (g_game_module.is_loaded() && g_game_module.api()->register_overlays) {
        g_game_module.api()->register_overlays();
    }
}

} // namespace sbk

// C-linkage entry for PE/COFF game modules, which cannot bind the C++ symbol
// through the engine's .def export table. See src/module/engine_exports.inc.
extern "C" void sbk_engine_register_overlays(
    const recomp::overlays::overlay_section_table_data_t* sections,
    const recomp::overlays::overlays_by_index_t* overlays) {
    recomp::overlays::register_overlays(*sections, *overlays);
}
