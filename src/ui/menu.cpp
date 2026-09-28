#include "menu.hpp"
#include "menu_actions.hpp"
#include "recompui/config.h"
#include "recompui/recompui.h"
#include "elements/ui_label.h"
#include "elements/ui_svg.h"
#include "ultramodern/ultramodern.hpp"
#ifdef SBK_CONTINUATIONS
#include "savestate/driver.hpp"
#endif

namespace sbk::ui {
namespace {
MenuActions actions;
void build_menu(recompui::ContextId context, recompui::Element* parent, bool preview) {
    using namespace recompui;
    auto* page = context.create_element<Element>(parent);
    page->set_display(Display::Flex);
    page->set_flex_direction(FlexDirection::Column);
    page->set_width(100, Unit::Percent);
    page->set_height(100, Unit::Percent);
    page->set_padding(24);
    page->set_gap(16);
    page->set_overflow_y(Overflow::Auto);

    auto* title = context.create_element<Label>(page, "SNOWBOARD KIDS", LabelStyle::Large);
    title->set_font_family("Fredoka");
    title->set_font_size(48);
    context.create_element<Label>(page, preview ? "PC PORT / FRONTEND PREVIEW" : "TAKE A BREATHER", LabelStyle::Small);

    auto* columns = context.create_element<Element>(page);
    columns->set_display(Display::Flex);
    columns->set_flex_direction(FlexDirection::Row);
    columns->set_flex_wrap(FlexWrap::Wrap);
    columns->set_gap(32);
    auto* navigation = context.create_element<Element>(columns);
    navigation->set_display(Display::Flex);
    navigation->set_flex_direction(FlexDirection::Column);
    navigation->set_width(420);
    navigation->set_max_width(100, Unit::Percent);
    navigation->set_gap(12);
    auto add_button = [&](const char* text, auto callback) {
        auto* button = context.create_element<Button>(navigation, text, ButtonStyle::Primary);
        button->set_width(100, Unit::Percent);
        button->add_pressed_callback(callback);
        return button;
    };
    auto* resume = add_button("Resume", [] { config::close(); });
    resume->set_enabled(!preview);
    add_button("Settings", [] { config::set_tab(config::graphics::id); });
    add_button("Controller", [] { config::set_tab(config::controls::id); });
    add_button("Quit", [preview] {
        if (preview) ultramodern::quit();
        else open_quit_game_prompt();
    });

    auto* saves = context.create_element<Element>(columns);
    saves->set_display(Display::Flex);
    saves->set_flex_direction(FlexDirection::Column);
    saves->set_width(480);
    saves->set_max_width(100, Unit::Percent);
    saves->set_gap(12);
    auto* label = context.create_element<Label>(saves, "Savestates", LabelStyle::Large);
    label->set_font_family("Fredoka");
    auto* save = context.create_element<Button>(saves, "Quick Save    F5", ButtonStyle::Secondary);
    auto* load = context.create_element<Button>(saves, "Quick Load    F8", ButtonStyle::Secondary);
    bool available = false;
#ifdef SBK_CONTINUATIONS
    available = !preview && sbk::savestate::driver::enabled();
#endif
    save->set_enabled(available);
    load->set_enabled(available);
    save->add_pressed_callback([] { if (actions.submit(MenuAction::QuickSave)) config::close(); });
    load->add_pressed_callback([] { if (actions.submit(MenuAction::QuickLoad)) config::close(); });
    context.create_element<Label>(saves, preview ? "Available while a game is running." :
        "One quick slot. Loading returns to your last quick save.", LabelStyle::Small);
    context.create_element<Label>(saves, "Controller Pak: virtual / user data", LabelStyle::Small);
    auto* mountains = context.create_element<Svg>(saves, "slope.svg");
    mountains->set_width(360);
    mountains->set_max_width(100, Unit::Percent);
    mountains->set_height(160);
}
}
void register_menu(bool preview) {
    recompui::config::create_tab("Menu", "sbk_menu", [preview](auto context, auto* parent) {
        build_menu(context, parent, preview);
    }, nullptr, nullptr);
}
void poll_menu_actions() {
    const auto action = actions.take();
#ifdef SBK_CONTINUATIONS
    if (action == MenuAction::QuickSave) sbk::savestate::driver::quick_save();
    if (action == MenuAction::QuickLoad) sbk::savestate::driver::quick_load();
#else
    (void)action;
#endif
}
}
