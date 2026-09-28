#pragma once

#include <algorithm>
#include <cstdint>

namespace sbk::input_ports {

// During single-player setup, report attached controller capacity so the
// guest can offer 2–4 players. After RecompInput assignment, port presence
// follows the assigned/attached devices exactly. P1 remains the keyboard
// capable port during setup.
inline uint8_t presence_mask(bool single_player, int controller_count, uint8_t assigned_attached_mask) {
    if (!single_player) return assigned_attached_mask & 0x0f;
    const int count = std::clamp(controller_count, 1, 4);
    return uint8_t((1u << count) - 1u);
}

} // namespace sbk::input_ports
