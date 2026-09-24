#pragma once

#include "service.hpp"

#include <chrono>
#include <functional>

// Semantic adapters over the patched runtime and the continuation registry.
// Requires SBK_CONTINUATIONS and the n64modernruntime-savestate patch.
namespace sbk::savestate {

// OSThread::context slots of every saved owner (host-transient pointers).
std::vector<NormalizedSlot> thread_context_slots(const InMemorySnapshot& snapshot);

class ContinuationDomain final : public Domain {
public:
    explicit ContinuationDomain(uint8_t* rdram) : rdram_(rdram) {}
    DomainId id() const override { return DomainId::Continuations; }
    bool capture(InMemorySnapshot& out, std::string& error) override;
    bool validate(const InMemorySnapshot& in, std::string& error) const override;
    bool retire(std::string& error) override;
    bool install(const InMemorySnapshot& in, std::string& error) override;
    std::chrono::milliseconds timeout{3000};
private:
    uint8_t* rdram_;
};

class SchedulerDomain final : public Domain {
public:
    DomainId id() const override { return DomainId::Scheduler; }
    bool capture(InMemorySnapshot& out, std::string& error) override;
    bool install(const InMemorySnapshot& in, std::string& error) override;
    void begin(Transaction) override;
    void end(Transaction, bool committed_restore) override;
    // Deferred-arrival accounting across transactions (never silent).
    uint64_t admitted = 0, superseded = 0;
};

class TimeDomain final : public Domain {
public:
    DomainId id() const override { return DomainId::Time; }
    bool capture(InMemorySnapshot& out, std::string& error) override;
    bool install(const InMemorySnapshot& in, std::string& error) override;
};

// Guest audio frequency plus an optional host PCM boundary adapter.
struct HostAudio {
    std::function<bool(AudioState&, std::string&)> capture;
    std::function<bool(const AudioState&, std::string&)> install;
};
class AudioDomain final : public Domain {
public:
    explicit AudioDomain(HostAudio host = {}) : host_(std::move(host)) {}
    DomainId id() const override { return DomainId::Audio; }
    bool capture(InMemorySnapshot& out, std::string& error) override;
    bool validate(const InMemorySnapshot& in, std::string& error) const override;
    bool install(const InMemorySnapshot& in, std::string& error) override;
private:
    HostAudio host_;
};

class InputDomain final : public Domain {
public:
    DomainId id() const override { return DomainId::Input; }
    bool capture(InMemorySnapshot& out, std::string&) override { out.input.host_latches = 0; return true; }
    bool install(const InMemorySnapshot&, std::string&) override { return true; }
};
}
