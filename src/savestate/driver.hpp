#pragma once

#include "runtime_domains.hpp"
#include "storage.hpp"

#include <filesystem>
#include <functional>
#include <string_view>

// Savestate driver on the frontend thread. Owns the snapshot service and the
// P2 freeze/resume sequence for every savestate operation:
//   * quick save: freeze -> capture -> resume -> encode + atomic write (worker)
//   * quick load: read + fully validate the file (worker) -> freeze ->
//     transactional restore -> resume. A file that fails validation never
//     freezes the game.
//   * DEVELOPMENT ONLY in-memory capture/restore (P4 dev trigger).
// At most one operation runs; one further request may wait behind it.
namespace sbk::savestate::driver {

enum class Notice : uint8_t {
    Saving, Saved, Loading, Loaded, NoSave, Incompatible, Corrupted, SaveFailed, LoadFailed, Busy, NotReady
};
const char* notice_text(Notice notice);
bool notice_is_error(Notice notice);

struct Config {
    BuildIdentity build;
    uint64_t rom_hash = 0;                   // XXH3-64 of the validated ROM
    HostAudio audio;
    void (*audio_pause)(bool paused) = nullptr; // P2 CloseVI audio pause
    std::filesystem::path directory;          // savestate folder (slot files)
    std::function<void(Notice)> notify;       // user feedback, frontend thread
    std::string writer = "SnowboardKidsRecompiled";
};

// Before runtime workers start. Enables the P2 coordinator.
bool init(const Config& config);
bool enabled();
void set_memory(uint8_t* rdram);

void quick_save(std::string_view slot = storage::kQuickSlot);
void quick_load(std::string_view slot = storage::kQuickSlot);
// DEVELOPMENT ONLY: single in-memory snapshot (logs "P4 DEV ...").
void dev_capture();
void dev_restore(FaultPoint fault = FaultPoint::None);

// Frontend thread, once per frame. Never waits for guest owners; a Frozen
// transaction itself runs synchronously here.
void poll();
// At application exit: lets an in-flight quick save finish its atomic write
// (bounded wait). An interrupted write never damages the previous file.
void shutdown();
}
