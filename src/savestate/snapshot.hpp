#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

// P4-B in-memory snapshot. Explicit, versioned and host independent: every
// field is guest-visible or logical timeline state, stored as fixed-width
// values. No native stacks/return addresses/TLS, std::thread, locks,
// semaphores, SDL/Vulkan/RT64 objects or host pointers are representable.
// This is not a file format (.sbks is out of scope) and is never persisted.
namespace sbk::savestate {

constexpr uint32_t kSnapshotMagic = 0x344B4253;   // 'SBK4'
constexpr uint32_t kSnapshotVersion = 1;
constexpr uint32_t kGuestBase = 0x80000000u;

enum class DomainId : uint8_t {
    Memory, Continuations, Scheduler, Time, Vi, Audio, Input, Overlays, Rsp,
    Renderer, Color, Depth, Count
};
constexpr size_t kDomainCount = static_cast<size_t>(DomainId::Count);
const char* domain_name(DomainId id);
constexpr uint32_t domain_bit(DomainId id) { return 1u << static_cast<unsigned>(id); }

// Build compatibility: a snapshot restores only into the same generated corpus.
struct BuildIdentity {
    uint64_t corpus_digest = 0;
    uint32_t function_count = 0;
    uint32_t hle_count = 0;
};

// Host-transient bytes inside guest memory (OSThread::context native
// pointers). Stored and hashed as zero; rebound from the owner registry.
struct NormalizedSlot {
    uint32_t offset;
    uint32_t size;
};

// Dense logical extent, stored sparsely: pages that are entirely zero are
// omitted and restored as zero. This is zero elision, not dirty tracking.
struct GuestMemory {
    uint64_t extent = 0;
    uint32_t page_size = 4096;
    std::vector<uint32_t> pages;            // ascending indices of nonzero pages
    std::vector<uint8_t> data;              // pages.size() * page_size bytes
    std::vector<NormalizedSlot> normalized; // ascending offsets
};

struct CpuState {
    std::array<uint64_t, 32> gpr{};
    std::array<uint64_t, 32> fpr{}; // bit patterns
    uint64_t hi = 0, lo = 0;
    uint32_t status = 0;
    uint8_t fr = 0;                 // mips3_float_mode
    uint8_t rounding = 0;           // MIPS encoding: 0 nearest, 1 zero, 2 up, 3 down
};

struct FrameState {
    uint64_t function = 0, continuation = 0;
    uint64_t hi = 0, lo = 0, result = 0;
    int32_t c1cs = 0;
    uint32_t indirect_target = 0;
    std::vector<uint64_t> scratch;
};

struct BlockedState {
    uint64_t hle_id = 0;
    uint8_t phase = 0;              // continuation::BlockedPhase
    std::array<uint64_t, 4> args{};
    int32_t result = 0;
    bool tail = false;
};

// How the owner waits at the Frozen boundary. Running = held the run token
// (parked); Sleeping = waiting on its scheduler semaphore.
enum class RunState : uint8_t { Sleeping = 0, Running = 1 };

struct ThreadState {
    uint32_t address = 0;           // guest OSThread
    uint64_t logical_lifetime = 0;
    uint32_t entrypoint = 0, argument = 0;
    RunState run = RunState::Sleeping;
    uint8_t pending = 0;            // continuation::PendingOp (None for Sleeping)
    bool started = false;
    CpuState cpu;
    std::vector<FrameState> frames;
    BlockedState blocked;
};

struct ContinuationState {
    uint64_t logical_lifetime_counter = 0;
    std::vector<ThreadState> threads; // ascending guest address
};

struct InboxMessage {
    int32_t mq = 0;
    uint32_t msg = 0;
    bool jam = false;
    bool requeue_if_blocked = false;
};

struct SchedulerState {
    int32_t running_queue_head = 0;
    std::vector<InboxMessage> inbox;  // delivery order
};

struct TimeState {
    int64_t logical_ns = 0;
    int64_t ostime_offset = 0;
    std::vector<int32_t> active_timers; // guest OSTimer, canonical order
};

struct ViModeState {
    uint32_t mode = 0;               // 0 null, 1 runtime dummy, else guest address
    int32_t framebuffer = 0, mq = 0;
    uint32_t msg = 0, state = 0, control = 0;
    int32_t retrace_count = 0;
};

struct EventRegistration { int32_t mq = 0; uint32_t msg = 0; };

struct ViState {
    int32_t cur_state = 0, field = 0;
    std::array<ViModeState, 2> states{};
    std::array<uint32_t, 14> regs{}, update_screen_regs{};
    uint64_t total_vis = 0;
    int32_t remaining_retraces = 0;
    bool dummy_odd = false;
    EventRegistration sp, dp, ai, si;
};

// P4 required semantic audio. Host device/converter objects are rebuilt.
struct AudioState {
    uint32_t guest_frequency = 0;     // osAiSetFrequency result
    bool host_present = false;
    uint32_t host_input_rate = 0;
    uint32_t host_output_rate = 0, host_output_channels = 0; // backlog PCM format
    std::vector<uint32_t> host_history; // conversion boundary samples (float bits)
    std::vector<uint8_t> host_backlog;  // submitted, not yet consumed PCM (output format)
};

// Guest input latches live in RDRAM and SI completions in the scheduler inbox.
// The host adapter keeps no input latch; this records that explicitly.
struct InputState {
    uint32_t host_latches = 0;
};

struct LoadedSection { uint32_t section_table_index = 0; int32_t ram_addr = 0; };
struct OverlayState {
    std::vector<LoadedSection> loaded;          // load order
    std::vector<int32_t> section_addresses;
};

struct RspState {
    std::vector<uint8_t> dmem;
};

// P3/P3.1 semantic renderer blob ('SBK3'), including GPU-authoritative planes.
struct RendererState {
    std::vector<uint8_t> blob;
};

struct DomainHashes {
    std::array<uint64_t, kDomainCount> domain{};
    uint64_t aggregate = 0;
    bool operator==(const DomainHashes&) const = default;
};

struct InMemorySnapshot {
    uint32_t magic = kSnapshotMagic;
    uint32_t version = kSnapshotVersion;
    BuildIdentity build;
    uint32_t present = 0;            // domain_bit mask of captured domains
    GuestMemory memory;
    ContinuationState continuations;
    SchedulerState scheduler;
    TimeState time;
    ViState vi;
    AudioState audio;
    InputState input;
    OverlayState overlays;
    RspState rsp;
    RendererState renderer;
    DomainHashes hashes;             // computed when capture completes
};

// Canonical encoding and hashing -------------------------------------------
uint64_t hash_domain(const InMemorySnapshot& snapshot, DomainId id);
DomainHashes compute_hashes(const InMemorySnapshot& snapshot);
// The exact byte stream fed to the domain hash (tests inspect the format).
std::vector<uint8_t> canonical_bytes(const InMemorySnapshot& snapshot, DomainId id);
// Structural validation before any live mutation.
bool validate_snapshot(const InMemorySnapshot& snapshot, std::string& error);
size_t payload_bytes(const InMemorySnapshot& snapshot);
// First differing present domain, or Count when equal.
DomainId first_mismatch(const DomainHashes& expected, const DomainHashes& actual, uint32_t present);

// Guest memory helpers --------------------------------------------------------
struct MemoryMetrics {
    uint64_t mapped_bytes = 0;
    int64_t resident_bytes = -1;     // -1 when not measurable
    uint64_t nonzero_page_bytes = 0;
    uint64_t nonzero_bytes = 0;
};
// Copies nonzero pages of [base, base+extent) with the slots zeroed.
void capture_memory(const uint8_t* base, uint64_t extent, std::vector<NormalizedSlot> slots,
    GuestMemory& out, MemoryMetrics* metrics);
// Installs the full extent: saved pages copied, every other page zeroed.
void install_memory(uint8_t* base, const GuestMemory& memory);
}
