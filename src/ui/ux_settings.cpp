#include "ux_settings.hpp"

#include "main/host_gain.hpp"
#include "librecomp/config.hpp"
#include "recompinput/ux_trace.h"
#include "recompui/config.h"
#include "recompui/recompui.h"

#include <atomic>
#include <variant>

namespace sbk::ux {
namespace {
// Read by the audio thread for every queued block.
std::atomic<float> gain{1.0f};

enum class ReducedMotion : uint32_t { Off, On };
}

void bind_master_volume(recomp::config::Config& sound) {
    sound.add_option_change_callback(recompui::config::sound::options::main_volume,
        [](recomp::config::ConfigValueVariant value, recomp::config::ConfigValueVariant,
           recomp::config::OptionChangeContext) {
            if (const double* percent = std::get_if<double>(&value)) {
                gain.store(sbk::host_gain::from_percent(*percent), std::memory_order_relaxed);
                recompinput::ux_trace::log("AUDIO GAIN percent=%.0f gain=%.2f", *percent, master_gain());
            }
        });
}

float master_gain() {
    return gain.load(std::memory_order_relaxed);
}

recomp::config::Config& create_accessibility_tab() {
    auto& config = recompui::config::create_config_tab("Accessibility", accessibility_id, false);
    config.add_enum_option(
        reduced_motion_id,
        "Reduced Motion",
        "Stops pulsing highlights, spinning icons and smooth scrolling in these menus. "
        "The game itself is not changed.",
        {{ReducedMotion::Off, "Off"}, {ReducedMotion::On, "On"}},
        ReducedMotion::Off);
    config.add_option_change_callback(reduced_motion_id,
        [](recomp::config::ConfigValueVariant value, recomp::config::ConfigValueVariant,
           recomp::config::OptionChangeContext) {
            if (const uint32_t* mode = std::get_if<uint32_t>(&value)) {
                recompui::set_reduced_motion(*mode == static_cast<uint32_t>(ReducedMotion::On));
                recompinput::ux_trace::log("REDUCED_MOTION %s", *mode == static_cast<uint32_t>(ReducedMotion::On) ? "On" : "Off");
            }
        });
    return config;
}

} // namespace sbk::ux
