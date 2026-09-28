#include "dev_trigger.hpp"

#include "driver.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

namespace sbk::savestate::dev {
namespace {
using HostClock = std::chrono::steady_clock;

bool on = false;
std::string control_path;
uint64_t control_seq = 0;
HostClock::time_point next_control_read;
bool capture_was_down = false, restore_was_down = false;

FaultPoint parse_fault(const std::string& name) {
    for (unsigned i = 0; i < static_cast<unsigned>(FaultPoint::Count); ++i) {
        if (name == fault_name(static_cast<FaultPoint>(i))) return static_cast<FaultPoint>(i);
    }
    std::fprintf(stderr, "P4 DEV unknown fault '%s' ignored\n", name.c_str());
    return FaultPoint::None;
}

void read_control() {
    if (control_path.empty() || HostClock::now() < next_control_read) return;
    next_control_read = HostClock::now() + std::chrono::milliseconds(100);
    std::ifstream input(control_path);
    std::string line;
    while (std::getline(input, line)) {
        std::istringstream fields(line);
        uint64_t seq = 0;
        std::string command, argument;
        if (!(fields >> seq >> command)) continue;
        fields >> argument;
        if (seq <= control_seq) continue;
        control_seq = seq;
        if (command == "capture") driver::dev_capture();
        else if (command == "restore") driver::dev_restore(argument.empty() ? FaultPoint::None : parse_fault(argument));
        else if (command == "quicksave") driver::quick_save(argument.empty() ? storage::kQuickSlot : argument);
        else if (command == "quickload") driver::quick_load(argument.empty() ? storage::kQuickSlot : argument);
        else std::fprintf(stderr, "P4 DEV unknown command '%s' (seq=%llu)\n", command.c_str(), (unsigned long long)seq);
    }
}
}

bool init() {
    const char* flag = std::getenv("SBK_P4_SAVESTATE_DEV");
    if (!flag || std::strcmp(flag, "1") != 0 || !driver::enabled()) return false;
    if (const char* path = std::getenv("SBK_P4_SAVESTATE_CONTROL")) control_path = path;
    on = true;
    std::fprintf(stderr, "P4 DEV savestate DEVELOPMENT ONLY: in-memory single snapshot. "
        "Ctrl+F6=capture Ctrl+F7=restore control_file=%s\n", control_path.empty() ? "(none)" : control_path.c_str());
    return true;
}

bool enabled() { return on; }

void poll(bool capture_key, bool restore_key) {
    if (!on) return;
    if (capture_key && !capture_was_down) driver::dev_capture();
    if (restore_key && !restore_was_down) driver::dev_restore();
    capture_was_down = capture_key;
    restore_was_down = restore_key;
    read_control();
}
}
