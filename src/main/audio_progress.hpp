#pragma once

#include <algorithm>
#include <cstdint>

namespace sbk::audio_progress {

// SBK requests one audio task per 60 Hz retrace and rounds its target to
// sixteen stereo frames. AI reports the current DMA, not the entire SDL
// playback queue. Exposing more than one task's target makes the guest's
// signed output-length calculation wrap before osAiSetNextBuffer.
inline uint32_t target_frames(uint32_t sample_rate) {
    return ((sample_rate + 59) / 60 + 15) & ~15u;
}

inline uint32_t remaining_frames(uint64_t queued_output_bytes,
                                 uint32_t output_channels,
                                 uint32_t sample_rate,
                                 uint32_t output_sample_rate) {
    if (!output_channels || !output_sample_rate) return 0;
    const uint64_t queued_output_frames =
        queued_output_bytes / (uint64_t(output_channels) * sizeof(float));
    const uint64_t input_frames =
        queued_output_frames * sample_rate / output_sample_rate;
    const uint64_t one_vi = sample_rate / 60;
    const uint64_t pending = input_frames > one_vi ? input_frames - one_vi : 0;
    return static_cast<uint32_t>(std::min<uint64_t>(pending, target_frames(sample_rate)));
}

} // namespace sbk::audio_progress
