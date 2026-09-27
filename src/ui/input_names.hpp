#pragma once

#include <array>
#include <utility>

#include "recompinput/input_types.h"

// Names shown on the Controls page. Display only: controls.json stores the
// enum names (get_game_input_enum_name), and guest mappings are unchanged.
namespace sbk::input_names {

using recompinput::GameInput;

inline constexpr std::array<std::pair<GameInput, const char*>, 24> names{{
    {GameInput::X_AXIS_NEG, "Analog Stick Left"},
    {GameInput::X_AXIS_POS, "Analog Stick Right"},
    {GameInput::Y_AXIS_NEG, "Analog Stick Down"},
    {GameInput::Y_AXIS_POS, "Analog Stick Up"},
    {GameInput::A, "N64 A"},
    {GameInput::B, "N64 B"},
    {GameInput::Z, "Z Trigger"},
    {GameInput::L, "L Button"},
    {GameInput::R, "R Button"},
    {GameInput::START, "Start"},
    {GameInput::C_UP, "C Up"},
    {GameInput::C_DOWN, "C Down"},
    {GameInput::C_LEFT, "C Left"},
    {GameInput::C_RIGHT, "C Right"},
    {GameInput::DPAD_UP, "D-Pad Up"},
    {GameInput::DPAD_DOWN, "D-Pad Down"},
    {GameInput::DPAD_LEFT, "D-Pad Left"},
    {GameInput::DPAD_RIGHT, "D-Pad Right"},
    {GameInput::TOGGLE_MENU, "Open / Close Menu"},
    {GameInput::ACCEPT_MENU, "Menu: Confirm"},
    {GameInput::BACK_MENU, "Menu: Back"},
    {GameInput::APPLY_MENU, "Menu: Apply"},
    {GameInput::TAB_LEFT_MENU, "Menu: Previous Tab"},
    {GameInput::TAB_RIGHT_MENU, "Menu: Next Tab"},
}};

template <typename SetName>
void apply(SetName&& set_name) {
    for (const auto& [input, name] : names) set_name(input, name);
}

} // namespace sbk::input_names
