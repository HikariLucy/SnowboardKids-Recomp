#pragma once

// DEVELOPMENT ONLY input source for the savestate driver (P4 live gate and
// automation). The user-facing controls are quick save/load (driver.hpp).
//
// Enable with SBK_P4_SAVESTATE_DEV=1. Triggers:
//   * Ctrl+F6 = in-memory capture, Ctrl+F7 = in-memory restore (window focus
//     required; one in-memory snapshot, replaced only by a successful capture);
//   * SBK_P4_SAVESTATE_CONTROL=<file>: lines "<seq> capture", "<seq> restore",
//     "<seq> restore <fault-name>", "<seq> quicksave [slot]" and
//     "<seq> quickload [slot]"; each new, larger <seq> runs once.
namespace sbk::savestate::dev {
// Reads the environment. Call after the driver is initialized.
bool init();
bool enabled();
// Frontend thread, before driver::poll().
void poll(bool capture_key, bool restore_key);
}
