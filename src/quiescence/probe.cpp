#include "probe.hpp"
#include "quiescence.hpp"
#ifdef SBK_CONTINUATIONS
#include "continuation/execution.hpp"
#include "continuation/runtime_owner.hpp"
#endif
#include "ultramodern/ultramodern.hpp"
#include "ultramodern/renderer_context.hpp"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace sbk::quiescence {
namespace {
using HostClock = std::chrono::steady_clock;
std::atomic<uint8_t*> memory{};
uint32_t (*audio_bytes)() = nullptr;
unsigned target = 0, cycles = 0, interval_ms = 250, hold_ms = 25;
uint64_t operation = 0, last_trace = 0;
HostClock::time_point next_request, requested, frozen_at;
std::chrono::high_resolution_clock::time_point frozen_time;
std::vector<uint8_t> before;
uint32_t frozen_audio = 0;
bool captured = false;
std::string control_file;
std::atomic<uint16_t> control_buttons{0};
std::atomic<float> control_x{0.0f}, control_y{0.0f};
unsigned setting(const char* name, unsigned fallback) {
    auto value = std::getenv(name);
    return value ? static_cast<unsigned>(std::strtoul(value, nullptr, 10)) : fallback;
}
void dump_trace() {
    for (auto& entry : trace()) {
        if (entry.sequence <= last_trace) continue;
        std::fprintf(stderr, "P2 seq=%llu gen=%llu state=%s participant=%s op=%s detail=%llu\n",
            (unsigned long long)entry.sequence, (unsigned long long)entry.generation,
            name(entry.state), entry.participant.c_str(), entry.operation.c_str(),
            (unsigned long long)entry.detail);
        last_trace = entry.sequence;
    }
}
}
void probe_init(void (*audio_pause)(bool), uint32_t (*audio_size)()) {
    if (auto path = std::getenv("SBK_P2_CONTROL_FILE")) control_file = path;
    target = setting("SBK_P2_CYCLES", 0);
    if (!target) return;
    enable(); set_audio_callback(audio_pause); audio_bytes = audio_size;
    interval_ms = setting("SBK_P2_INTERVAL_MS", 250);
    hold_ms = setting("SBK_P2_HOLD_MS", 25);
    next_request = HostClock::now() + std::chrono::milliseconds(setting("SBK_P2_START_MS", 10000));
    // Audit bytes only, never written to disk or restored into the runtime.
    before.resize(8 * 1024 * 1024);
    std::fprintf(stderr, "P2 enabled cycles=%u hold_ms=%u interval_ms=%u audit_bytes=%zu\n", target, hold_ms, interval_ms, before.size());
}
bool probe_input(int controller, uint16_t* buttons, float* x, float* y) {
    if (control_file.empty() || controller != 0) return false;
    *buttons = control_buttons.load();
    *x = control_x.load();
    *y = control_y.load();
    return true;
}
void probe_memory(uint8_t* rdram) { memory = rdram; }
void probe_poll() {
    poll();
    if (!control_file.empty()) {
        std::ifstream input(control_file);
        unsigned buttons = 0;
        if (input >> std::hex >> buttons) {
            control_buttons = static_cast<uint16_t>(buttons);
            float stick_x = 0.0f, stick_y = 0.0f;
            if (input >> stick_x >> stick_y) {
                control_x = stick_x;
                control_y = stick_y;
            } else {
                control_x = 0.0f;
                control_y = 0.0f;
            }
        }
    }
    if (!target) return;
    auto now = HostClock::now();
    auto current = status();
    if (current.state == State::Idle && !operation && cycles < target && now >= next_request && memory.load()) {
        operation = request(); requested = now;
    }
    if (current.state == State::Frozen && operation && !captured) {
        std::memcpy(before.data(), memory.load(), before.size());
        frozen_time = logical_now(); frozen_audio = audio_bytes(); frozen_at = now;
        captured = true;
        note("audit", "frozen-memory-clock-audio", before.size());

        auto* renderer = get_renderer_context();
        if (renderer && setting("SBK_P3_TEST", 1)) {
            // P3.1 Authority Validation: Pre-reset readbacks
            std::vector<uint8_t> pre_gpu_color_pixels;
            uint64_t pre_gpu_color_hash = 0;
            bool color_gpu_read = renderer->readback_gpu_target(false, pre_gpu_color_pixels, &pre_gpu_color_hash);

            uint64_t pre_rdram_color_hash = 0;
            bool color_rdram_read = renderer->get_rdram_framebuffer_hash(false, &pre_rdram_color_hash);

            bool color_gpu_eq_rdram = (color_gpu_read && color_rdram_read && pre_gpu_color_hash == pre_rdram_color_hash);

            std::vector<uint8_t> pre_gpu_depth_pixels;
            uint64_t pre_gpu_depth_hash = 0;
            bool depth_gpu_read = renderer->readback_gpu_target(true, pre_gpu_depth_pixels, &pre_gpu_depth_hash);

            uint64_t pre_rdram_depth_hash = 0;
            bool depth_rdram_read = renderer->get_rdram_framebuffer_hash(true, &pre_rdram_depth_hash);

            bool depth_valid = (depth_gpu_read && depth_rdram_read);
            bool depth_gpu_eq_rdram = (depth_valid && pre_gpu_depth_hash == pre_rdram_depth_hash);

            auto t0 = HostClock::now();
            std::vector<uint8_t> blob;
            bool exported = renderer->export_semantic_state(blob);
            auto t1 = HostClock::now();

            auto t2 = HostClock::now();
            bool reset_ok = renderer->reset_semantic_state();
            auto t3 = HostClock::now();

            auto t4 = HostClock::now();
            bool imported = renderer->import_semantic_state(blob.data(), blob.size());
            auto t5 = HostClock::now();

            auto t6 = HostClock::now();
            bool presented = renderer->present_restored_frame();
            auto t7 = HostClock::now();

            // P3.1 Authority Validation: Post-import readbacks
            std::vector<uint8_t> post_gpu_color_pixels;
            uint64_t post_gpu_color_hash = 0;
            bool post_color_read = renderer->readback_gpu_target(false, post_gpu_color_pixels, &post_gpu_color_hash);

            bool color_roundtrip_match = (color_gpu_read && post_color_read && pre_gpu_color_hash == post_gpu_color_hash);

            std::vector<uint8_t> post_gpu_depth_pixels;
            uint64_t post_gpu_depth_hash = 0;
            bool post_depth_read = false;
            bool depth_roundtrip_match = false;
            if (depth_valid) {
                post_depth_read = renderer->readback_gpu_target(true, post_gpu_depth_pixels, &post_gpu_depth_hash);
                depth_roundtrip_match = (post_depth_read && pre_gpu_depth_hash == post_gpu_depth_hash);
            }

            uint64_t post_rdram_color_hash = 0;
            renderer->get_rdram_framebuffer_hash(false, &post_rdram_color_hash);

            auto exp_us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
            auto rst_us = std::chrono::duration_cast<std::chrono::microseconds>(t3 - t2).count();
            auto imp_us = std::chrono::duration_cast<std::chrono::microseconds>(t5 - t4).count();
            auto prs_us = std::chrono::duration_cast<std::chrono::microseconds>(t7 - t6).count();

            bool hash_match = (pre_rdram_color_hash == post_rdram_color_hash);

            std::fprintf(stderr, "P3.1 AUTHORITY gen=%llu color_pre_gpu=%016llx color_pre_rdram=%016llx color_post_gpu=%016llx color_gpu_eq_rdram=%d color_roundtrip_match=%d depth_valid=%d depth_pre_gpu=%016llx depth_pre_rdram=%016llx depth_post_gpu=%016llx depth_gpu_eq_rdram=%d depth_roundtrip_match=%d exp_us=%lld rst_us=%lld imp_us=%lld prs_us=%lld\n",
                (unsigned long long)operation,
                (unsigned long long)pre_gpu_color_hash, (unsigned long long)pre_rdram_color_hash, (unsigned long long)post_gpu_color_hash,
                color_gpu_eq_rdram ? 1 : 0, color_roundtrip_match ? 1 : 0,
                depth_valid ? 1 : 0,
                (unsigned long long)pre_gpu_depth_hash, (unsigned long long)pre_rdram_depth_hash, (unsigned long long)post_gpu_depth_hash,
                depth_gpu_eq_rdram ? 1 : 0, depth_roundtrip_match ? 1 : 0,
                (long long)exp_us, (long long)rst_us, (long long)imp_us, (long long)prs_us);

            std::fprintf(stderr, "P3 ROUNDTRIP gen=%llu exported=%d reset=%d imported=%d presented=%d size=%zu export_us=%lld reset_us=%lld import_us=%lld present_us=%lld ref_hash=%016llx post_hash=%016llx match=%d\n",
                (unsigned long long)operation, exported, reset_ok, imported, presented, blob.size(),
                (long long)exp_us, (long long)rst_us, (long long)imp_us, (long long)prs_us,
                (unsigned long long)pre_rdram_color_hash, (unsigned long long)post_rdram_color_hash, hash_match ? 1 : 0);

            if (!exported || !reset_ok || !imported || !presented || !hash_match) {
                dump_trace();
                std::fprintf(stderr, "P3 FAILED gen=%llu: semantic roundtrip failure (exported=%d reset=%d imported=%d presented=%d match=%d)\n",
                    (unsigned long long)operation, exported, reset_ok, imported, presented, hash_match ? 1 : 0);
                cancel("P3 roundtrip failed"); target = 0; return;
            }
        }
    }
    if (captured && current.state == State::Frozen && now - frozen_at >= std::chrono::milliseconds(hold_ms)) {
        bool same = std::memcmp(before.data(), memory.load(), before.size()) == 0 &&
            logical_now() == frozen_time && audio_bytes() == frozen_audio;
        std::fprintf(stderr, "P2 AUDIT gen=%llu unchanged=%d audio_bytes=%u owners=%zu\n",
            (unsigned long long)operation, same, frozen_audio, current.owners);
#ifdef SBK_CONTINUATIONS
        std::fprintf(stderr, "P4A FROZEN_INVARIANT gen=%llu live_owners=%zu total_dispatches=%llu startup_retired=%d\n",
            (unsigned long long)operation,
            sbk::continuation::live_owner_count(),
            (unsigned long long)sbk::continuation::total_dispatch_count(),
            sbk::continuation::startup_is_retired() ? 1 : 0);
#endif
        if (!same) {
            dump_trace();
            std::fprintf(stderr, "P2 FAILED: mutation while Frozen; cancelling probe\n");
            cancel("Frozen audit changed"); target = 0; return;
        }
        resume(operation); captured = false; operation = 0; ++cycles;
        next_request = now + std::chrono::milliseconds(interval_ms);
        dump_trace();
        if (cycles == target) {
            std::fprintf(stderr, "P2 COMPLETE cycles=%u Frozen audits passed; equivalence to unfrozen execution NOT established\n", cycles);
        }
    }
    if (operation && now - requested > std::chrono::seconds(10)) {
        std::fprintf(stderr, "P2 TIMEOUT gen=%llu state=%s active=%zu rsp=%llu gfx=%llu failure=%s\n",
            (unsigned long long)operation, name(current.state), current.active_owners,
            (unsigned long long)current.rsp_pending, (unsigned long long)current.graphics_pending, current.failure.c_str());
        dump_trace(); cancel("probe timeout"); target = 0;
    }
}
}
