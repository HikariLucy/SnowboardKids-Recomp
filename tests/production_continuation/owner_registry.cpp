#include "continuation/owner_registry.hpp"
#include <cassert>
#include <iostream>
#include <memory>
#include <stdexcept>

struct Context {
    int value;
    int& destroyed;
    ~Context() { ++destroyed; }
};

template<class Exception, class F> void rejects(F action) {
    bool rejected = false;
    try { action(); } catch (const Exception&) { rejected = true; }
    assert(rejected);
}

int main() {
    // Address-only lookup or retirement would let an obsolete worker corrupt a
    // replacement. Assert on the replacement context, not container internals.
    int destroyed = 0;
    sbk::continuation::OwnerRegistry<Context> registry;
    auto context = [&] (int value) {
        return std::unique_ptr<Context>(new Context{value, destroyed});
    };
    constexpr uint32_t address = 0x80001000;
    auto first = registry.create(address, context(10));
    assert(first.address == address && first.lifetime != 0);
    assert(registry.find(first)->value == 10);
    auto other = registry.create(0x80002000, context(20));
    rejects<std::logic_error>([&] { registry.create(address, context(99)); });
    assert(registry.find(first)->value == 10);
    assert(destroyed == 1); // rejected context was not leaked
    assert(registry.retire(first));
    assert(destroyed == 2);
    assert(!registry.find(first));
    assert(!registry.current_key(address));
    auto replacement = registry.create(address, context(30));
    assert(replacement.lifetime > first.lifetime);
    assert(!registry.find(first));
    assert(!registry.retire(first));
    assert(registry.current_key(address) == replacement);
    assert(registry.find(replacement)->value == 30);
    assert(registry.find(other)->value == 20);
    assert(!registry.find({address, 0}));
    assert(!registry.retire({address, 0}));
    rejects<std::invalid_argument>([&] { registry.create(0, context(40)); });
    rejects<std::invalid_argument>([&] { registry.create(0x80003000, nullptr); });
    assert(!registry.current_key(0x80003000));
    assert(registry.retire(replacement));
    assert(registry.retire(other));
    assert(destroyed == 5);
    std::cout << "owner registry: lifetime isolation, rejection and ownership passed\n";
}
