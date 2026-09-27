#pragma once

#include <string>
#include <vector>

namespace sbk::frontend {

enum class ConfigTab { General, Graphics, Controls, Sound, Accessibility, Mods };

// A game id alone does not mean the mod subsystem has been initialized.
// ModMenu is available only after both conditions hold (UI-MOD-01).
inline bool mods_available(const std::string& mod_game_id, bool mods_initialized) {
    return mods_initialized && !mod_game_id.empty();
}

// Order of the options menu tabs.
inline std::vector<ConfigTab> config_tabs(const std::string& mod_game_id, bool mods_initialized) {
    std::vector<ConfigTab> tabs{ConfigTab::General, ConfigTab::Graphics, ConfigTab::Controls, ConfigTab::Sound,
                                ConfigTab::Accessibility};
    if (mods_available(mod_game_id, mods_initialized)) tabs.push_back(ConfigTab::Mods);
    return tabs;
}

}
