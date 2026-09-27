// Real Audio/General schemas (RecompFrontend), the real Accessibility tab and
// Master Volume binding (src/ui/ux_settings.cpp) and librecomp's Config/JSON
// persistence. Only the host UI seams are stubbed.
#include "recompui/config.h"
#include "recompinput/device_label.h"
#include "librecomp/game.hpp"
#include "ui/ux_settings.hpp"
#include "ui/input_names.hpp"
#include "recompinput/recompinput.h"

#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>

static std::map<std::string, std::unique_ptr<recomp::config::Config>> configs;
static bool reduced_motion = false;
static int reduced_motion_calls = 0;

namespace recomp {
std::filesystem::path get_config_path() { return std::getenv("SBK_CONFIG_TEST_DIR"); }
const Version& get_project_version() { static const Version version{1, 0, 0}; return version; }
}
namespace recompui {
void set_reduced_motion(bool enabled) { reduced_motion = enabled; ++reduced_motion_calls; }
bool get_reduced_motion() { return reduced_motion; }
bool is_steam_deck() { return false; }
}
namespace recompui::config {
recomp::config::Config& create_config_tab(const std::string& name, const std::string& id, bool confirm) {
    auto& slot = configs[id];
    slot = std::make_unique<recomp::config::Config>(name, id, confirm);
    return *slot;
}
recomp::config::Config& get_config(const std::string& id) { return *configs.at(id); }
}

int main(int argc, char** argv) {
    auto& sound = recompui::config::create_sound_tab("Audio");
    sbk::ux::bind_master_volume(sound);
    recompui::config::GeneralTabOptions general_options{};
    general_options.has_rumble_strength = true;
    auto& general = recompui::config::create_general_tab(general_options);
    auto& accessibility = sbk::ux::create_accessibility_tab();
    for (auto* config : {&sound, &general, &accessibility}) config->load_config();

    const std::string mode = argc > 1 ? argv[1] : "";
    if (mode == "save" && argc == 6) {
        sound.set_option_value("main_volume", std::atof(argv[2]));
        accessibility.set_option_value(sbk::ux::reduced_motion_id, static_cast<uint32_t>(std::atoi(argv[3])));
        general.set_option_value("joystick_deadzone", std::atof(argv[4]));
        general.set_option_value("rumble_strength", std::atof(argv[5]));
        for (auto* config : {&sound, &general, &accessibility})
            if (!config->save_config()) return 2;
    } else if (mode == "labels") {
        using recompinput::DeviceLabelState;
        nlohmann::json j;
        j["not_assigned"] = recompinput::device_label(DeviceLabelState::NotAssigned);
        j["keyboard"] = recompinput::device_label(DeviceLabelState::Keyboard);
        j["named"] = recompinput::device_label(DeviceLabelState::Controller, "Xbox Wireless Controller");
        j["unnamed"] = recompinput::device_label(DeviceLabelState::Controller, nullptr);
        j["empty"] = recompinput::device_label(DeviceLabelState::Controller, "");
        j["disconnected"] = recompinput::device_label(DeviceLabelState::Disconnected, "Xbox Wireless Controller");
        j["none"] = recompinput::device_label(DeviceLabelState::NoController);
        std::cout << j.dump() << '\n';
        return 0;
    } else if (mode == "names") {
        // Display names change; the persisted enum names must not.
        nlohmann::json j = nlohmann::json::object();
        sbk::input_names::apply([](recompinput::GameInput input, const char* name) {
            recompinput::set_game_input_name(input, name);
        });
        for (size_t i = 0; i < recompinput::num_game_inputs; ++i) {
            const auto input = static_cast<recompinput::GameInput>(i);
            j[recompinput::get_game_input_enum_name(input)] = recompinput::get_game_input_name(input);
        }
        std::cout << j.dump() << '\n';
        return 0;
    } else if (!mode.empty()) {
        return 3;
    }

    nlohmann::json j;
    j["sound"] = sound.get_json_config();
    j["general"] = general.get_json_config();
    j["accessibility"] = accessibility.get_json_config();
    j["gain"] = sbk::ux::master_gain();
    j["reduced_motion"] = reduced_motion;
    j["reduced_motion_calls"] = reduced_motion_calls;
    j["recompui_volume"] = recompui::config::sound::get_main_volume();
    j["recompui_deadzone"] = recompui::config::general::get_joystick_deadzone();
    std::cout << j.dump() << '\n';
}
