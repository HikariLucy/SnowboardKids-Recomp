#pragma once

#include <filesystem>

namespace sbk::pfs {
void configure(std::filesystem::path config_directory);
void set_port_present(int port, bool present);
}
