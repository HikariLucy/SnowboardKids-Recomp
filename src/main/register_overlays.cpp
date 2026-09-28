#include "module/module_abi.h"
#include "module/module_loader.hpp"

namespace sbk {

extern module::GameModule g_game_module;

void register_overlays() {
    if (g_game_module.is_loaded() && g_game_module.api()->register_overlays) {
        g_game_module.api()->register_overlays();
    }
}

} // namespace sbk
