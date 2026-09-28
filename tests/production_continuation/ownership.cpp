#include "continuation/runtime_owner.hpp"
#include <cassert>
#include <cstdio>
using namespace sbk::continuation;
int main() {
    assert(current_execution() == nullptr);
    OSThread guest{};
    for (unsigned cycle = 0; cycle < 200; ++cycle) {
        auto* old = create_owner(0x80001000, &guest);
        OwnerKey old_key{};
        old->host_thread = std::thread([&] {
            bind_worker(old);
            old_key = current_owner_key();
            auto* execution = current_execution();
            assert(execution && current_context() == old);
            execution->blocked.hle_id = 73;
            old->initialized.signal();
            old->running.wait();
            assert(!owner_is_current());
            assert(current_owner_key() == old_key);
            assert(current_execution() == execution && execution->blocked.hle_id == 73);
            assert(!finish_current_owner());
        });
        old->initialized.wait();
        // Corrupt the transient guest cache deliberately; registry remains authoritative.
        guest.context = nullptr;
        assert(context_for(&guest) == old);
        assert(detach_owner(&guest) == old);
        auto* fresh = create_owner(0x80001000, &guest);
        fresh->host_thread = std::thread([&] {
            bind_worker(fresh);
            assert(current_owner_key().lifetime > old_key.lifetime);
            assert(current_execution()->blocked.hle_id == 0);
            fresh->initialized.signal();
            fresh->running.wait();
            assert(owner_is_current());
            assert(finish_current_owner());
        });
        fresh->initialized.wait();
        old->running.signal();
        old->host_thread.join();
        // Old cleanup must preserve the new lifetime and its guest cache.
        release_joined_owner(old);
        assert(guest.context == fresh && context_for(&guest) == fresh);
        fresh->running.signal();
        fresh->host_thread.join();
        release_joined_owner(fresh);
        assert(guest.context == nullptr);
    }
    std::puts("PASS: 200 real-worker address-reuse cycles, exact-lifetime TLS, owned execution, stale cleanup isolation");
}
