#include "probe.hpp"
#include "quiescence.hpp"
#include "ultramodern/ultramodern.hpp"
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
    }
    if (captured && current.state == State::Frozen && now - frozen_at >= std::chrono::milliseconds(hold_ms)) {
        bool same = std::memcmp(before.data(), memory.load(), before.size()) == 0 &&
            logical_now() == frozen_time && audio_bytes() == frozen_audio;
        std::fprintf(stderr, "P2 AUDIT gen=%llu unchanged=%d audio_bytes=%u owners=%zu\n",
            (unsigned long long)operation, same, frozen_audio, current.owners);
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
