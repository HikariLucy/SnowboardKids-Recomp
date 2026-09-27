#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <vector>

// Host-side master volume. Applied to converted output samples right before
// they are queued to SDL: the guest's audio data, the AI FIFO accounting
// (queued byte counts) and the resampler history are not touched.
namespace sbk::host_gain {

// Audio tab percent (0-100) to a linear gain in [0, 1]. Anything invalid
// (NaN, infinity) plays at unity rather than going silent unexpectedly.
inline float from_percent(double percent) {
    if (!std::isfinite(percent)) return 1.0f;
    return static_cast<float>(std::clamp(percent, 0.0, 100.0) / 100.0);
}

// Unity leaves the samples bit-exact.
inline void apply(float* samples, std::size_t count, float gain) {
    if (!(gain < 1.0f)) return;
    gain = std::max(gain, 0.0f);
    for (std::size_t i = 0; i < count; ++i) samples[i] *= gain;
}

// Output bytes for SDL. At unity the input is returned as is; otherwise a
// scaled copy is made in `scratch`, so the caller's PCM (mirrored into
// savestates) stays at unity gain and a restored backlog plays at the
// current volume.
inline const void* scaled(const void* bytes, std::size_t size, float gain, std::vector<float>& scratch) {
    if (!(gain < 1.0f)) return bytes;
    scratch.resize(size / sizeof(float));
    std::memcpy(scratch.data(), bytes, scratch.size() * sizeof(float));
    apply(scratch.data(), scratch.size(), gain);
    return scratch.data();
}

} // namespace sbk::host_gain
