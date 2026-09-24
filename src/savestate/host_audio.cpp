#include "host_audio.hpp"

#include <algorithm>
#include <cstring>

namespace sbk::savestate::audio {

void PcmLedger::append(const uint8_t* data, size_t size) {
    const size_t capacity = ring_.size();
    if (!capacity || !size) return;
    if (size >= capacity) { // only the newest `capacity` bytes can matter
        data += size - capacity;
        size = capacity;
    }
    const size_t first = std::min(size, capacity - head_);
    std::memcpy(ring_.data() + head_, data, first);
    std::memcpy(ring_.data(), data + first, size - first);
    head_ = (head_ + size) % capacity;
    filled_ = std::min(capacity, filled_ + size);
}

bool PcmLedger::tail(size_t size, std::vector<uint8_t>& out) const {
    if (size > filled_) return false;
    out.resize(size);
    if (!size) return true;
    const size_t capacity = ring_.size();
    const size_t start = (head_ + capacity - size) % capacity;
    const size_t first = std::min(size, capacity - start);
    std::memcpy(out.data(), ring_.data() + start, first);
    std::memcpy(out.data() + first, ring_.data(), size - first);
    return true;
}

void PcmLedger::reset(std::span<const uint8_t> contents) {
    head_ = filled_ = 0;
    append(contents.data(), contents.size());
}

void Boundary::submitted(const uint8_t* data, size_t size) {
    std::lock_guard lock{mutex_};
    ledger_.append(data, size);
}

size_t Boundary::ledger_filled() const {
    std::lock_guard lock{mutex_};
    return ledger_.filled();
}

bool Boundary::capture(const DeviceFormat& format, std::span<const float> history, uint32_t queued,
    AudioState& out, std::string& error) const {
    out.host_input_rate = format.input_rate;
    out.host_output_rate = format.output_rate;
    out.host_output_channels = format.output_channels;
    out.host_history.clear();
    for (float sample : history) {
        uint32_t bits;
        std::memcpy(&bits, &sample, sizeof(bits));
        out.host_history.push_back(bits);
    }
    const size_t frame = size_t(format.output_channels) * sizeof(float);
    if (!frame || queued % frame) {
        error = "host audio queue is not frame aligned";
        return false;
    }
    std::lock_guard lock{mutex_};
    if (!ledger_.tail(queued, out.host_backlog)) {
        error = "queued audio exceeds the ledger";
        return false;
    }
    return true;
}

bool Boundary::validate(const AudioState& in, const DeviceFormat& format, size_t history_size, std::string& error) const {
    if (in.host_output_rate != format.output_rate || in.host_output_channels != format.output_channels) {
        error = "audio device output format differs from the snapshot";
        return false;
    }
    if (in.host_history.size() != history_size) {
        error = "audio conversion history shape differs";
        return false;
    }
    const size_t frame = size_t(in.host_output_channels) * sizeof(float);
    if (!frame || in.host_backlog.size() % frame) {
        error = "audio backlog is not frame aligned";
        return false;
    }
    std::lock_guard lock{mutex_};
    if (in.host_backlog.size() > ledger_.capacity() || in.host_backlog.size() > UINT32_MAX) {
        error = "audio backlog exceeds the host ledger";
        return false;
    }
    return true;
}

bool Boundary::install(const AudioState& in, const DeviceFormat& format, std::span<float> history,
    const HostQueue& queue, std::string& error) {
    if (!validate(in, format, history.size(), error)) return false;
    // The domain restored the guest frequency (and converter) first.
    if (in.host_input_rate != format.input_rate) {
        error = "audio conversion input rate differs after frequency restore";
        return false;
    }
    for (size_t i = 0; i < history.size(); ++i) std::memcpy(&history[i], &in.host_history[i], sizeof(float));
    // Flush the abandoned timeline's queued audio, then queue the saved
    // backlog exactly once. Never requeue audio that was already consumed.
    queue.clear();
    std::lock_guard lock{mutex_};
    ledger_.reset({});
    if (!in.host_backlog.empty() &&
        !queue.queue(in.host_backlog.data(), static_cast<uint32_t>(in.host_backlog.size()), error))
        return false;
    ledger_.reset(in.host_backlog);
    return true;
}
}
