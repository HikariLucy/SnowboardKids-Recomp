#include "toast.hpp"

#include "recompui/recompui.h"
#include "core/ui_context.h"
#include "elements/ui_element.h"
#include "elements/ui_label.h"
#include "ultramodern/ultramodern.hpp"

#include <chrono>
#include <cstdlib>
#include <cstring>

namespace sbk::savestate::toast {
namespace {
using Clock = std::chrono::steady_clock;
recompui::ContextId context = recompui::ContextId::null();
recompui::Element* box = nullptr;
recompui::Label* label = nullptr;
Clock::time_point hide_at;
bool visible = false;

bool disabled() {
    static const bool value = [] {
        const char* flag = std::getenv("SBK_SAVESTATE_TOAST");
        return flag && std::strcmp(flag, "0") == 0;
    }();
    return value;
}

void build() {
    context = recompui::create_context();
    context.open();
    context.set_captures_input(false);
    context.set_captures_mouse(false);
    auto* root = context.create_element<recompui::Element>(context.get_root_element());
    root->set_position(recompui::Position::Absolute);
    root->set_left(32);
    root->set_bottom(32);
    box = context.create_element<recompui::Element>(root);
    box->set_padding(16);
    box->set_border_width(recompui::theme::border::width);
    box->set_border_radius(recompui::theme::border::radius_lg);
    box->set_border_color(recompui::theme::color::WhiteA20);
    box->set_background_color(recompui::theme::color::ModalOverlay);
    label = context.create_element<recompui::Label>(box, "", recompui::LabelStyle::Normal);
    context.close();
}
}

void show(const std::string& text, bool error) {
    // The frontend UI exists once the renderer started the game.
    if (disabled() || !ultramodern::is_game_started()) return;
    if (context == recompui::ContextId::null()) build();
    context.open();
    label->set_text(text);
    box->set_border_color(error ? recompui::theme::color::Danger : recompui::theme::color::WhiteA20);
    context.close();
    if (!recompui::is_context_shown(context)) recompui::show_context(context, "");
    visible = true;
    hide_at = Clock::now() + std::chrono::milliseconds(error ? 3500 : 2000);
}

void update() {
    if (!visible || Clock::now() < hide_at) return;
    visible = false;
    // Another UI path may already have hidden every context.
    if (recompui::is_context_shown(context)) recompui::hide_context(context);
}
}
