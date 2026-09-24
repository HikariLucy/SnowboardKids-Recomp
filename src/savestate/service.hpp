#pragma once

#include "snapshot.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// P4-B capture / P4-C transactional restore over the existing P2 coordinator.
// Both run only inside a Frozen generation (Frozen -> Capture|Restore -> Frozen);
// the caller owns request/resume. Nothing here is persisted.
namespace sbk::savestate {

enum class Transaction : uint8_t { Capture, Restore };

// One semantic state domain. capture() exports owned values, validate() runs
// before any mutation, install() runs only inside a Restore transaction.
class Domain {
public:
    virtual ~Domain() = default;
    virtual DomainId id() const = 0;
    // Hashed as additional domains when this domain owns them (renderer planes).
    virtual uint32_t extra_domains() const { return 0; }
    virtual bool capture(InMemorySnapshot& out, std::string& error) = 0;
    virtual bool validate(const InMemorySnapshot&, std::string&) const { return true; }
    virtual bool install(const InMemorySnapshot& in, std::string& error) = 0;
    // Execution owners leave cooperatively before memory is replaced.
    virtual bool retire(std::string&) { return true; }
    // Transaction hooks (admission seal / deferred-arrival policy).
    virtual void begin(Transaction) {}
    // committed_restore: a new timeline replaced the old one.
    virtual void end(Transaction, bool /*committed_restore*/) {}
};

// Controlled failure points for rollback validation (tests / dev only).
enum class FaultPoint : uint8_t {
    None,
    AfterRetire,
    AfterMemory,
    AfterTime,
    AfterScheduler,
    AfterVi,
    AfterOverlays,
    AfterRsp,
    AfterContinuations,
    BeforeRenderer,
    AfterRenderer,
    AfterAudio,
    PostValidate,
    DuringRollback,   // rollback reconstruction itself fails -> stays sealed/Restore
    Count
};
const char* fault_name(FaultPoint fault);

struct PhaseTiming { std::string name; uint64_t micros; };

struct Result {
    bool ok = false;
    bool rolled_back = false;
    bool unrecoverable = false;     // barrier left in Restore, admissions sealed
    std::string error;
    std::string rollback_error;
    DomainHashes hashes;            // captured, or verified post-install
    uint64_t total_micros = 0;
    std::vector<PhaseTiming> phases;
    MemoryMetrics memory;
    size_t payload_bytes = 0;
    size_t rollback_bytes = 0;
};

class SnapshotService {
public:
    // Install order is fixed by DomainId-specific ordering below; registration
    // order does not matter. Domains missing at restore reject before mutation.
    void add(Domain* domain);
    void set_build(BuildIdentity build) { build_ = build; }
    uint32_t available() const;
    // Optional callback at each named step (tests inject external arrivals).
    std::function<void(const char* step)> on_step;

    Result capture(uint64_t generation, InMemorySnapshot& out);
    Result restore(uint64_t generation, const InMemorySnapshot& snapshot, FaultPoint fault = FaultPoint::None);

    // Memory metrics from the last capture into a snapshot.
    MemoryMetrics last_memory_metrics;

private:
    Domain* find(DomainId id) const;
    bool capture_all(InMemorySnapshot& out, Result& result, std::string& error);
    bool install_timeline(const InMemorySnapshot& snapshot, FaultPoint fault, Result& result, std::string& error);
    void step(const char* name);
    std::vector<Domain*> domains_;
    BuildIdentity build_;
};

// Memory domain helper shared by runtime/test adapters.
class MemoryDomain final : public Domain {
public:
    using Slots = std::function<std::vector<NormalizedSlot>(const InMemorySnapshot&)>;
    MemoryDomain(uint8_t* base, uint64_t extent, Slots slots) : base_(base), extent_(extent), slots_(std::move(slots)) {}
    DomainId id() const override { return DomainId::Memory; }
    bool capture(InMemorySnapshot& out, std::string& error) override;
    bool validate(const InMemorySnapshot& in, std::string& error) const override;
    bool install(const InMemorySnapshot& in, std::string& error) override;
    MemoryMetrics metrics;
private:
    uint8_t* base_;
    uint64_t extent_;
    Slots slots_;
};
}
