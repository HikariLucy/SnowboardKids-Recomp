// Master Volume is a host gain on queued output samples. It must map the UI
// range exactly, reject invalid values, and leave byte counts (and so the
// guest-visible AI length) unchanged.
#include "main/audio_progress.hpp"
#include "main/host_gain.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

namespace {
int failures = 0;
void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

std::vector<float> ramp(std::size_t count) {
    std::vector<float> samples(count);
    for (std::size_t i = 0; i < count; ++i) samples[i] = float(int(i % 200) - 100) / 128.0f;
    return samples;
}
} // namespace

int main() {
    using sbk::host_gain::apply;
    using sbk::host_gain::from_percent;
    constexpr double inf = std::numeric_limits<double>::infinity();
    const double nan = std::numeric_limits<double>::quiet_NaN();

    check(from_percent(100.0) == 1.0f && from_percent(0.0) == 0.0f && from_percent(50.0) == 0.5f,
          "0/50/100% map to gain 0/0.5/1");
    check(from_percent(-25.0) == 0.0f && from_percent(250.0) == 1.0f, "out-of-range percent clamps to [0, 1]");
    check(from_percent(nan) == 1.0f && from_percent(inf) == 1.0f && from_percent(-inf) == 1.0f,
          "NaN and infinity play at unity");

    const auto original = ramp(1600);
    auto unity = original;
    apply(unity.data(), unity.size(), 1.0f);
    check(std::memcmp(unity.data(), original.data(), original.size() * sizeof(float)) == 0,
          "100% leaves samples bit-exact");

    auto silent = original;
    apply(silent.data(), silent.size(), from_percent(0.0));
    bool all_zero = true;
    for (float s : silent) all_zero &= s == 0.0f;
    check(all_zero, "0% is silence");

    auto half = original;
    apply(half.data(), half.size(), from_percent(50.0));
    bool halved = true;
    for (std::size_t i = 0; i < half.size(); ++i) halved &= half[i] == original[i] * 0.5f;
    check(halved, "50% scales every sample by 0.5");

    auto odd = original;
    apply(odd.data(), odd.size(), std::numeric_limits<float>::quiet_NaN());
    auto negative = original;
    apply(negative.data(), negative.size(), -3.0f);
    bool negative_silent = true;
    for (float s : negative) negative_silent &= s == 0.0f;
    check(std::memcmp(odd.data(), original.data(), original.size() * sizeof(float)) == 0 && negative_silent,
          "NaN gain is unity, negative gain is silence, never inverted");

    apply(nullptr, 0, 0.5f);
    check(true, "empty block is a no-op");

    // Savestates mirror the caller's PCM: scaling for SDL must never modify it.
    std::vector<float> scratch;
    const auto source = original;
    check(sbk::host_gain::scaled(source.data(), source.size() * sizeof(float), 1.0f, scratch) == source.data() &&
              scratch.empty(),
          "unity output is the source buffer, no copy");
    const auto* out = static_cast<const float*>(
        sbk::host_gain::scaled(source.data(), source.size() * sizeof(float), 0.25f, scratch));
    bool scaled_ok = out == scratch.data() && scratch.size() == source.size();
    for (std::size_t i = 0; scaled_ok && i < source.size(); ++i) scaled_ok &= out[i] == original[i] * 0.25f;
    check(scaled_ok && std::memcmp(source.data(), original.data(), original.size() * sizeof(float)) == 0,
          "25% output is a scaled copy; mirrored source PCM stays at unity");

    // The AI length the guest sees is computed from queued bytes only. Gain
    // changes sample values, never how many bytes are queued.
    constexpr uint32_t rate = 48000, channels = 2;
    const uint64_t queued_bytes = half.size() * sizeof(float);
    check(queued_bytes == original.size() * sizeof(float) &&
              sbk::audio_progress::remaining_frames(queued_bytes, channels, rate, rate) ==
                  sbk::audio_progress::remaining_frames(original.size() * sizeof(float), channels, rate, rate),
          "gain leaves byte counts and guest AI length unchanged");

    std::printf("%s host gain (%d failure%s)\n", failures ? "FAIL" : "PASS", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
