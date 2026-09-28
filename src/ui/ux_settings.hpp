#pragma once

#include <string>

namespace recomp::config {
class Config;
}

// Audio and accessibility settings that are wired end to end:
// options menu -> Config (user-data JSON) -> host effect.
namespace sbk::ux {

inline const std::string accessibility_id = "accessibility";
inline const std::string reduced_motion_id = "reduced_motion";

// Master Volume (Audio tab, sound.json "main_volume") -> host output gain.
void bind_master_volume(recomp::config::Config& sound);
float master_gain();

// Accessibility tab (accessibility.json). Reduced Motion drives the
// frontend's focus pulses, spinners and smooth scrolling.
recomp::config::Config& create_accessibility_tab();

} // namespace sbk::ux
