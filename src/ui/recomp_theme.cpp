#include "recomp_theme.h"

#include "elements/ui_frontend_theme.h"
#include "elements/ui_theme.h"
#include "recompui/recompui.h"

namespace snowboardkids::theme {
void apply() {
    using namespace recompui;
    namespace t = recompui::theme;
    using t::color;
    const Color ink{23, 43, 70, 255};
    const Color snow{245, 248, 237, 255};
    const Color ice{213, 237, 241, 255};
    const Color slope{35, 90, 120, 255};
    const Color sun{255, 211, 92, 255};
    const Color warning{123, 53, 28, 255};
    const Color disabled{83, 101, 115, 255};
    const Color disabled_bg{220, 226, 228, 255};

    register_primary_font("LatoLatin-Regular.ttf", "LatoLatin");
    for (const char* font : {"LatoLatin-Italic.ttf", "LatoLatin-Bold.ttf",
                             "LatoLatin-BoldItalic.ttf", "Fredoka.ttf"}) {
        register_extra_font(font);
    }
    // PromptFont and Noto Emoji are loaded by the frontend's font loader.
    t::set_typography_preset(t::Typography::Header1, 48.0f);
    t::set_typography_preset(t::Typography::Header2, 36.0f);
    t::set_typography_preset(t::Typography::Header3, 30.0f);
    t::set_typography_preset(t::Typography::LabelLG, 28.0f);
    t::set_typography_preset(t::Typography::LabelMD, 24.0f);
    t::set_typography_preset(t::Typography::LabelSM, 21.0f);
    t::set_typography_preset(t::Typography::LabelXS, 21.0f);
    t::set_typography_preset(t::Typography::Body, 24.0f);

    auto theme = t::make_default_frontend_theme();
    auto colors = [&](std::initializer_list<color> ids, Color value) {
        for (color id : ids) theme.colors[static_cast<std::size_t>(id)] = value;
    };
    colors({color::Background1, color::Background2, color::BGOverlay, color::ModalOverlay}, snow);
    // Shadows are background roles here (footers, headers): keep them light.
    colors({color::Background3, color::Elevated, color::ElevatedSoft, color::BGShadow, color::BGShadow2}, ice);
    colors({color::Border, color::BorderSoft,
            color::BorderHard, color::BorderSolid, color::ElevatedBorder,
            color::ElevatedBorderHard}, ink);
    colors({color::Text, color::TextActive, color::TextDim, color::TextA5, color::TextA20,
            color::TextA30, color::TextA50, color::TextA80}, ink);
    colors({color::TextInactive}, disabled);
    colors({color::Primary, color::PrimaryL, color::PrimaryD}, ink);
    colors({color::PrimaryA5, color::PrimaryA20, color::PrimaryA30,
            color::PrimaryA50, color::PrimaryA80}, ice);
    // Secondary drives focus pulses and rings on light panels: keep it dark.
    // Sun stays the focus background via the explicit widget styles below.
    colors({color::Secondary, color::SecondaryD,
            color::SecondaryA5, color::SecondaryA20, color::SecondaryA30,
            color::SecondaryA50, color::SecondaryA80}, slope);
    colors({color::SecondaryL}, ink);
    colors({color::Warning, color::WarningL, color::WarningD,
            color::Danger, color::DangerL, color::DangerD}, warning);
    colors({color::WarningA5, color::WarningA20, color::WarningA30,
            color::WarningA50, color::WarningA80, color::DangerA5, color::DangerA20,
            color::DangerA30, color::DangerA50, color::DangerA80}, sun);
    colors({color::Success, color::SuccessL, color::SuccessD}, slope);
    colors({color::SuccessA5, color::SuccessA20, color::SuccessA30,
            color::SuccessA50, color::SuccessA80}, ice);
    colors({color::BW05, color::BW10, color::BW25, color::BW50, color::BW75, color::BW90}, ice);
    theme.border_radius_sm = 4.0f;
    theme.border_radius_md = 8.0f;
    theme.border_radius_lg = 8.0f;
    theme.border_width = 3.0f;
    theme.primary_button = t::ButtonStylePalette{ink, snow, ink, ink, sun, ink, disabled};
    theme.secondary_button = t::ButtonStylePalette{ink, ice, ink, ink, sun, ink, disabled};

    auto panel = [&](Style& style) {
        style.set_background_color(snow);
        style.set_color(ink);
        style.set_font_family("LatoLatin");
        style.set_font_size(24.0f);
        style.set_border_color(ink);
        style.set_border_width(6.0f);
        style.set_border_radius(8.0f);
    };
    auto interactive = [&](t::InteractiveStyle& style) {
        style.normal.set_background_color(snow);
        style.normal.set_color(ink);
        style.normal.set_font_family("LatoLatin");
        style.normal.set_border_color(ink);
        style.normal.set_border_width(3.0f);
        style.normal.set_border_radius(4.0f);
        style.normal.set_min_height(56.0f);
        style.hover.set_color(ink);
        style.hover.set_background_color(ice);
        style.hover.set_border_color(ink);
        style.focus.set_color(ink);
        style.focus.set_background_color(sun);
        style.focus.set_border_color(ink);
        style.focus.set_border_width(4.0f);
        style.disabled.set_color(disabled);
        style.disabled.set_background_color(disabled_bg);
        style.disabled.set_border_color(disabled);
    };
    theme.modal.overlay.set_background_color(Color{23, 43, 70, 180});
    theme.modal.overlay.set_font_family("LatoLatin");
    panel(theme.modal.frame);
    theme.modal.frame.set_padding(24.0f);
    theme.modal.header.set_background_color(snow);
    theme.modal.header.set_font_family("Fredoka");
    theme.modal.header.set_border_bottom_color(ink);
    theme.modal.header.set_border_bottom_width(3.0f);
    theme.modal.body.set_background_color(snow);
    theme.modal.body.set_color(ink);
    theme.modal.tabs.emplace();
    auto& tabs = *theme.modal.tabs;
    // Unselected tabs are dimmed (5.9:1 on snow); selected and focused use ink.
    tabs.normal.set_color(disabled);
    tabs.normal.set_font_family("Fredoka");
    tabs.hover.set_color(ink);
    tabs.hover.set_background_color(ice);
    tabs.selected.set_color(ink);
    tabs.selected.set_background_color(sun);
    tabs.pulsing.set_color(ink);
    tabs.pulsing.set_background_color(ice);
    tabs.focus_text_color = ink; // readable on sun (selected) and ice (focused)
    // Focused tab also gets an ink bar on top (selected has the bar below), so
    // focus and selection differ by shape, not only by ice vs sun.
    tabs.normal.set_border_top_width(4.0f);
    tabs.normal.set_border_top_color(Color{0, 0, 0, 0});
    tabs.pulsing.set_border_top_color(ink);
    tabs.indicator.set_height(4.0f);
    tabs.indicator_color = ink;

    theme.prompt.overlay.set_background_color(Color{23, 43, 70, 180});
    panel(theme.prompt.content);
    theme.prompt.controls.set_background_color(snow);
    theme.prompt.controls.set_border_top_color(ink);
    theme.prompt.controls.set_border_top_width(3.0f);
    interactive(theme.prompt.button);
    theme.prompt.secondary_button.emplace();
    interactive(*theme.prompt.secondary_button);
    interactive(theme.controls.binding_button);

    // Settings options: selected = bold ink + underline; focused = sun block
    // with an ink bar on the left. Neither state relies on color alone, and
    // focus is steady (no pulse).
    auto& radio = theme.radio.emplace();
    radio.normal.set_background_color(Color{0, 0, 0, 0});
    radio.normal.set_border_color(Color{0, 0, 0, 0});
    radio.normal.set_border_left_width(6.0f);
    radio.normal.set_padding_left(6.0f);
    radio.normal.set_padding_right(6.0f);
    radio.normal.set_font_weight(400);
    radio.checked.set_color(ink);
    radio.checked.set_border_color(Color{0, 0, 0, 0}); // upstream greys every side
    radio.checked.set_border_bottom_color(ink);
    radio.checked.set_font_weight(700);
    radio.focus.set_color(ink);
    radio.focus.set_background_color(sun);
    radio.focus.set_border_left_color(ink);
    radio.focus.set_border_bottom_color(ink);
    // Sliders: a visible slope track; the focused thumb grows into a sun
    // square with a thick ink border (size + border, not color alone).
    auto& slider = theme.slider.emplace();
    slider.bar.set_height(4.0f);
    slider.bar.set_background_color(slope);
    auto thumb = [&](Style& style, float size, Color fill, float border) {
        const float total = size + 2.0f * border;
        style.set_width(size);
        style.set_height(size);
        style.set_margin_top(-(total - 4.0f) / 2.0f);
        style.set_margin_left(-total / 2.0f);
        style.set_margin_right(-total / 2.0f);
        style.set_border_width(border);
        style.set_border_color(ink);
        style.set_border_radius(4.0f);
        style.set_background_color(fill);
    };
    thumb(slider.thumb, 14.0f, ink, 3.0f);
    thumb(slider.thumb_focus, 20.0f, sun, 4.0f);
    thumb(slider.thumb_disabled, 14.0f, disabled_bg, 3.0f);
    slider.thumb_disabled.set_border_color(disabled);
    theme.controls.input_device_toggle = t::TogglePreset::Filled;

    auto& assignment = theme.in_game_player_assignment;
    assignment.page.set_background_color(slope);
    assignment.page.set_font_family("LatoLatin");
    panel(assignment.content);
    assignment.content.set_padding(24.0f);
    assignment.heading.set_color(ink);
    assignment.heading.set_font_family("Fredoka");
    assignment.description.set_color(ink);
    assignment.card_border = ink;
    assignment.card_label = ink;
    assignment.card_waiting = disabled;
    assignment.card_active = ink;
    assignment.card_assigned = ink;
    assignment.card_assigned_background = sun;
    interactive(assignment.button);
    theme.mods.details.set_background_color(snow);
    theme.mods.enable_toggle = t::TogglePreset::Filled;
    t::set_frontend_theme(theme);
}
} // namespace snowboardkids::theme
