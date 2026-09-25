// UI-MOD-01: the Mods tab (the only place a ModMenu is built) is offered only
// when it has a mod_game_id and its mod subsystem has been initialized.
#include "main/config_tabs.hpp"

#include <algorithm>
#include <cstdio>

namespace {
int failures = 0;
void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}
using sbk::frontend::ConfigTab;
bool has(const std::vector<ConfigTab>& tabs, ConfigTab tab) { return std::find(tabs.begin(), tabs.end(), tab) != tabs.end(); }
}

int main() {
    const auto disabled = sbk::frontend::config_tabs("", false);
    check(!sbk::frontend::mods_available("", false), "no mod_game_id: mods unavailable");
    check(!has(disabled, ConfigTab::Mods), "no mod_game_id: Mods tab absent, so no ModMenu can be built");
    check(disabled == std::vector<ConfigTab>{ConfigTab::General, ConfigTab::Graphics, ConfigTab::Controls, ConfigTab::Sound},
          "General, Graphics, Controls and Sound tabs still registered in order");

    check(!has(sbk::frontend::config_tabs("", true), ConfigTab::Mods), "initialized subsystem without ID: tab absent");

    const auto uninitialized = sbk::frontend::config_tabs("fixture-game", false);
    check(!sbk::frontend::mods_available("fixture-game", false), "mod_game_id without initialized mods: unavailable");
    check(!has(uninitialized, ConfigTab::Mods), "mod_game_id without initialized mods: tab absent");

    const auto enabled = sbk::frontend::config_tabs("fixture-game", true);
    check(sbk::frontend::mods_available("fixture-game", true), "fixture mod_game_id and initialized mods: available");
    check(enabled.size() == 5 && enabled.back() == ConfigTab::Mods, "initialized fixture: Mods tab registered after the others");
    std::printf("%s frontend config tabs (%d failure%s)\n", failures ? "FAIL" : "PASS", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
