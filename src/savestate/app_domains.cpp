#include "app_domains.hpp"

#include "librecomp/overlays.hpp"
#include "quiescence/quiescence.hpp"
#include "ultramodern/renderer_context.hpp"
#include "ultramodern/savestate.hpp"

#include <algorithm>
#include <cstring>

extern uint8_t dmem[]; // librecomp RSP data memory (0x1000 bytes)

namespace sbk::savestate {
namespace us = ultramodern::savestate;

bool ViDomain::capture(InMemorySnapshot& out, std::string&) {
    const auto v = us::export_vi_events();
    auto& s = out.vi;
    s.cur_state = v.cur_state; s.field = v.field;
    for (int i = 0; i < 2; ++i) {
        const auto& m = v.states[i];
        s.states[i] = {m.mode, m.framebuffer, m.mq, m.msg, m.state, m.control, m.retrace_count};
    }
    std::copy(std::begin(v.regs), std::end(v.regs), s.regs.begin());
    std::copy(std::begin(v.update_screen_regs), std::end(v.update_screen_regs), s.update_screen_regs.begin());
    s.total_vis = v.total_vis; s.remaining_retraces = v.remaining_retraces; s.dummy_odd = v.dummy_odd;
    s.sp = {v.sp.mq, v.sp.msg}; s.dp = {v.dp.mq, v.dp.msg}; s.ai = {v.ai.mq, v.ai.msg}; s.si = {v.si.mq, v.si.msg};
    return true;
}

bool ViDomain::install(const InMemorySnapshot& in, std::string& error) {
    us::ViEventState v{};
    const auto& s = in.vi;
    v.cur_state = s.cur_state; v.field = s.field;
    for (int i = 0; i < 2; ++i) {
        const auto& m = s.states[i];
        v.states[i] = {m.mode, m.framebuffer, m.mq, m.msg, m.state, m.control, m.retrace_count};
    }
    std::copy(s.regs.begin(), s.regs.end(), v.regs);
    std::copy(s.update_screen_regs.begin(), s.update_screen_regs.end(), v.update_screen_regs);
    v.total_vis = s.total_vis; v.remaining_retraces = s.remaining_retraces; v.dummy_odd = s.dummy_odd;
    v.sp = {s.sp.mq, s.sp.msg}; v.dp = {s.dp.mq, s.dp.msg}; v.ai = {s.ai.mq, s.ai.msg}; v.si = {s.si.mq, s.si.msg};
    if (!us::import_vi_events(v)) { error = "VI state rejected"; return false; }
    return true;
}

bool OverlayDomain::capture(InMemorySnapshot& out, std::string&) {
    const auto state = recomp::overlays::export_overlay_state();
    out.overlays.loaded.clear();
    for (const auto& section : state.loaded) out.overlays.loaded.push_back({section.section_table_index, section.ram_addr});
    out.overlays.section_addresses = state.section_addresses;
    return true;
}

bool OverlayDomain::install(const InMemorySnapshot& in, std::string& error) {
    recomp::overlays::OverlayState state{};
    for (const auto& section : in.overlays.loaded) state.loaded.push_back({section.section_table_index, section.ram_addr});
    state.section_addresses = in.overlays.section_addresses;
    if (!recomp::overlays::import_overlay_state(state)) { error = "overlay table shape differs from this build"; return false; }
    return true;
}

bool RspDomain::capture(InMemorySnapshot& out, std::string&) {
    out.rsp.dmem.assign(dmem, dmem + 0x1000);
    return true;
}

bool RspDomain::install(const InMemorySnapshot& in, std::string&) {
    std::memcpy(dmem, in.rsp.dmem.data(), 0x1000);
    return true;
}

bool RendererDomain::capture(InMemorySnapshot& out, std::string& error) {
    auto* renderer = sbk::quiescence::get_renderer_context();
    if (!renderer) { error = "no renderer context"; return false; }
    if (!renderer->export_semantic_state(out.renderer.blob)) { error = "renderer semantic export failed"; return false; }
    return true;
}

bool RendererDomain::install(const InMemorySnapshot& in, std::string& error) {
    auto* renderer = sbk::quiescence::get_renderer_context();
    if (!renderer) { error = "no renderer context"; return false; }
    // Guest memory is already installed: import synchronizes RAM hashes so the
    // restored GPU planes are not mistaken for CPU framebuffer writes.
    if (!renderer->reset_semantic_state()) { error = "renderer reset failed"; return false; }
    if (!renderer->import_semantic_state(in.renderer.blob.data(), in.renderer.blob.size())) {
        error = "renderer semantic import failed";
        return false;
    }
    // Presents the restored frame without a guest step or synthetic VI/SP/DP/AI.
    if (!renderer->present_restored_frame()) { error = "restored frame presentation failed"; return false; }
    return true;
}
}
