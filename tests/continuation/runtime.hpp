#pragma once
#include "recomp.h"
#include <cstdint>
#include <vector>
#include <memory>
#include <span>
#include <stdexcept>

namespace p1 {
// Namespace 1 is this prototype's fixed, non-overlay main executable section.
constexpr uint64_t id(uint32_t pc) { return (uint64_t{1} << 32) | pc; }
constexpr uint64_t root_id = id(0x80700000), middle_id = id(0x80700100);
constexpr uint64_t leaf_id = id(0x80001858);
constexpr uint64_t send_id = id(0x800A0D30), recv_id = id(0x800A0990);
constexpr uint32_t request_queue = 0x800E4B78, reply_queue = 0x800E4BB0;
constexpr size_t memory_size = 8 * 1024 * 1024;
struct Frame {
    uint64_t function;
    uint32_t continuation = 0;
    uint64_t hi = 0, lo = 0, result = 0;
    int32_t c1cs = 0;
};
struct Action { uint64_t callee = 0; }; // zero means generated return
struct Blocked { uint32_t queue = 0, destination = 0, flags = 0; };
Action dispatch(uint8_t*, recomp_context*, Frame&);
uint64_t pending_callee(const Frame&); // throws for unknown IDs
void reference_root(uint8_t*, recomp_context*);
extern const char* const build_identity;

class Machine {
public:
    recomp_context ctx{};
    std::vector<uint8_t> memory;
    std::vector<Frame> frames;
    Blocked blocked;
    uint32_t rounding = 0, sends = 0, receives = 0;
    explicit Machine(bool initialize = true, uint32_t fr = 0);
    Machine(const Machine&) = delete;
    Machine& operator=(const Machine&) = delete;
    void rebind();
    bool run(); // false: parked; true: complete
    void deliver(uint32_t value);
    void send(uint32_t queue, uint32_t value);
    bool receive(uint32_t queue, uint32_t dest, uint32_t flags);
    std::vector<uint8_t> encode() const;
    static std::unique_ptr<Machine> decode(std::span<const uint8_t>);
    void validate() const;
    uint32_t word(uint32_t addr) const;
    void word(uint32_t addr, uint32_t value);
};
void check(bool value, const char* message);
}
