#pragma once

#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>

namespace sbk::continuation {

struct OwnerKey {
    uint32_t address;
    uint64_t lifetime;
    bool operator==(const OwnerKey&) const = default;
};

// Host-only authority. Access is serialized by the runtime scheduler; this
// container must never be accessed concurrently without that lock. The runtime
// must retire/join a worker before retiring its context. Returned pointers are
// borrowed only until retire(). Guest OSThread::context is not consulted.
// Keep this registry alive across timeline changes: lifetime allocation must
// never restart when a guest address is reused. No import/restore API exists.
template<class Context> class OwnerRegistry {
    struct Entry {
        uint64_t lifetime;
        std::unique_ptr<Context> context;
    };
    std::map<uint32_t, Entry> entries_;
    uint64_t last_lifetime_ = 0;

public:
    OwnerRegistry() = default;
    OwnerRegistry(const OwnerRegistry&) = delete;
    OwnerRegistry& operator=(const OwnerRegistry&) = delete;
    OwnerRegistry(OwnerRegistry&&) = delete;
    OwnerRegistry& operator=(OwnerRegistry&&) = delete;

    OwnerKey create(uint32_t address, std::unique_ptr<Context> context) {
        if (address == 0 || !context) {
            throw std::invalid_argument("Owner requires a guest address and context");
        }
        if (entries_.contains(address)) {
            throw std::logic_error("Guest thread already has a live owner");
        }
        if (last_lifetime_ == std::numeric_limits<uint64_t>::max()) {
            throw std::overflow_error("Owner lifetime exhausted");
        }
        const auto lifetime = last_lifetime_ + 1;
        entries_.emplace(address, Entry{lifetime, std::move(context)});
        last_lifetime_ = lifetime;
        return {address, lifetime};
    }

    // Scheduler-only lookup; workers retain the exact key from create().
    std::optional<OwnerKey> current_key(uint32_t address) const {
        auto it = entries_.find(address);
        if (it == entries_.end()) return std::nullopt;
        return OwnerKey{address, it->second.lifetime};
    }

    Context* find(OwnerKey key) {
        auto it = entries_.find(key.address);
        if (it == entries_.end() || it->second.lifetime != key.lifetime) return nullptr;
        return it->second.context.get();
    }

    const Context* find(OwnerKey key) const {
        auto it = entries_.find(key.address);
        if (it == entries_.end() || it->second.lifetime != key.lifetime) return nullptr;
        return it->second.context.get();
    }

    bool retire(OwnerKey key) {
        auto it = entries_.find(key.address);
        if (it == entries_.end() || it->second.lifetime != key.lifetime) return false;
        entries_.erase(it);
        return true;
    }
};

} // namespace sbk::continuation
