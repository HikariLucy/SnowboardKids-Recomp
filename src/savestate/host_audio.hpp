#pragma once

#include "snapshot.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <span>
#include <string>
#include <vector>

// P4/P5 host audio boundary, independent of SDL so it can be tested without a
// device. Only semantic PCM is snapshot state: the not-yet-consumed backlog in
// the device output format, the conversion history and the rates. The host
// device, its mixer and its callback buffers are never serialized.
//
// Restore policy (explicit): flush the host queue, then requeue the saved
// backlog exactly once. Audio already handed below the host queue (the device
// buffer, at most one device period plus OS mixer latency) cannot be recalled;
// it drains while the barrier is Frozen and is neither rewound nor replayed.
namespace sbk::savestate::audio {

// Bounded mirror of submitted output PCM. SDL cannot read queued audio back, so
// the newest `capacity` submitted bytes are kept; at a Frozen boundary the
// device is paused and the queued byte count selects the exact backlog.
class PcmLedger {
public:
    explicit PcmLedger(size_t capacity) : ring_(capacity) {}
    void append(const uint8_t* data, size_t size);
    // Newest `size` bytes, oldest first. False if more than was recorded.
    bool tail(size_t size, std::vector<uint8_t>& out) const;
    void reset(std::span<const uint8_t> contents);
    size_t filled() const { return filled_; }
    size_t capacity() const { return ring_.size(); }
private:
    std::vector<uint8_t> ring_;
    size_t head_ = 0, filled_ = 0;
};

struct DeviceFormat {
    uint32_t input_rate = 0;        // guest AI rate after conversion setup
    uint32_t output_rate = 0;       // device output rate
    uint32_t output_channels = 0;   // device output channels (float32 samples)
};

// Host queue operations. queue() is called at most once per install.
struct HostQueue {
    std::function<uint32_t()> queued_bytes;
    std::function<void()> clear;
    std::function<bool(const uint8_t*, uint32_t, std::string&)> queue;
};

class Boundary {
public:
    explicit Boundary(size_t ledger_bytes = 4 * 1024 * 1024) : ledger_(ledger_bytes) {}
    // Audio submit path, after the host queue accepted the bytes.
    void submitted(const uint8_t* data, size_t size);
    // Frozen capture: `queued` is the host queue size with the device paused.
    bool capture(const DeviceFormat& format, std::span<const float> history, uint32_t queued,
        AudioState& out, std::string& error) const;
    // Pre-mutation check that `in` can be installed on this device.
    bool validate(const AudioState& in, const DeviceFormat& format, size_t history_size, std::string& error) const;
    // Restore: flush the host queue, requeue the saved backlog once, restore
    // the conversion history. The device stays paused until the barrier resumes.
    bool install(const AudioState& in, const DeviceFormat& format, std::span<float> history,
        const HostQueue& queue, std::string& error);
    size_t ledger_filled() const;
private:
    mutable std::mutex mutex_;
    PcmLedger ledger_;
};
}
