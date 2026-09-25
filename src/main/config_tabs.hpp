#pragma once

#include <string>
#include <vector>

namespace sbk::frontend {

enum class ConfigTab { General, Graphics, Controls, Sound, Mods };

// librecomp loads mods only for a game registered with a mod_game_id. Until
// this game has one, the Mods tab would build a ModMenu without a game mod id
// (UI-MOD-01): it is not offered at all.
inline bool mods_available(const std::string& mod_game_id) {
    return !mod_game_id.empty();
}

// Order of the options menu tabs.
inline std::vector<ConfigTab> config_tabs(const std::string& mod_game_id) {
    std::vector<ConfigTab> tabs{ConfigTab::General, ConfigTab::Graphics, ConfigTab::Controls, ConfigTab::Sound};
    if (mods_available(mod_game_id)) tabs.push_back(ConfigTab::Mods);
    return tabs;
}

}
