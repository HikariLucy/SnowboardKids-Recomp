#pragma once

#include "runtime_domains.hpp"

// DEVELOPMENT ONLY. Temporary in-process capture/restore driver for the P4
// live gate. Not the final UX: no .sbks, no disk, no slots, no F5/F8. Holds at
// most ONE in-memory snapshot, replaced only by a successful capture.
//
// Enable with SBK_P4_SAVESTATE_DEV=1. Triggers:
//   * Ctrl+F6 = capture, Ctrl+F7 = restore (window focus required), and/or
//   * SBK_P4_SAVESTATE_CONTROL=<file>: lines "<seq> capture" / "<seq> restore"
//     / "<seq> restore <fault-name>"; each new, larger <seq> runs once.
namespace sbk::savestate::dev {
struct Config {
    BuildIdentity build;
    HostAudio audio;
    void (*audio_pause)(bool paused) = nullptr; // P2 CloseVI audio pause
};
bool init(const Config& config); // before runtime workers start; false = disabled
bool enabled();
void set_memory(uint8_t* rdram);
// Frontend thread, after event handling. Never waits for guest owners; the
// Frozen transaction itself runs synchronously on this thread.
void poll(bool capture_key, bool restore_key);
}
