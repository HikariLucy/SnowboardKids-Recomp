#include "main/audio_progress.hpp"
#include <cassert>
#include <cstdint>
#include <initializer_list>

int main() {
    using sbk::audio_progress::remaining_frames;
    using sbk::audio_progress::target_frames;
    constexpr uint32_t rate = 48000;
    constexpr uint32_t channels = 2;
    constexpr uint32_t target = 800;
    assert(target_frames(rate) == target);
    const auto bytes = [channels](uint64_t frames) {
        return frames * channels * sizeof(float);
    };
    const auto guest_length = [&](uint64_t host_frames) {
        return remaining_frames(bytes(host_frames), channels, rate, rate);
    };
    // Host playback may queue more than one task; the guest must see at most
    // one AI task. The helper receives the host byte count by value.
    assert(guest_length(0) == 0);
    for (uint32_t pending : {target - 1, target, target + 1,
                             2 * target, 16000u}) {
        const uint64_t queued_bytes = bytes(uint64_t(rate / 60) + pending);
        const uint32_t reported = remaining_frames(queued_bytes, channels, rate, rate);
        assert(reported == (pending < target ? pending : target));
        assert(queued_bytes == bytes(uint64_t(rate / 60) + pending));
        const int32_t task = (int32_t(target) - (int32_t(reported) - int32_t(target / 4)) + 0x68) & 0xfff0;
        assert(task >= 0);
    }
    assert(guest_length(target + 400) == 400);

    // The live stall sent 0xfffff340 (-3264) bytes to osAiSetNextBuffer.
    // At 48 kHz that corresponds to a guest task length of -816 frames.
    // The runtime applies a further 800-byte (200 stereo-frame)
    // offset after this callback. A 2712-frame SDL backlog therefore exposed
    // 1712 frames to the guest and produced the observed -816-frame task.
    constexpr uint32_t backlog = 2712;
    constexpr uint32_t runtime_offset = target / 4;
    const uint32_t old_reported = backlog - target - runtime_offset;
    const int16_t old_task = static_cast<int16_t>(
        (int32_t(target) - int32_t(old_reported) + 0x68) & 0xfff0);
    assert(old_task == -816);
    const auto reported = guest_length(backlog);
    assert(reported == target);
    const int32_t computed =
        (int32_t(target) - int32_t(reported - runtime_offset) + 0x68) & 0xfff0;
    const int32_t next_task = computed < int32_t(target - 0x10)
        ? int32_t(target - 0x10) : computed;
    assert(next_task == 784);
    assert(remaining_frames(uint64_t(backlog) * channels * sizeof(float),
                            channels, rate, 96000) == 556);
}
