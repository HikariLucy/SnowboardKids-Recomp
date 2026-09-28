#pragma once
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>
namespace sbk::first_run {
enum class Phase { Missing, Select, Validate, GenerateCPU, GenerateRSP, Compile, ValidateModule, Install, Ready, Error };
struct Model {
    Phase phase = Phase::Missing;
    int current = 0, total = 0;
    std::string detail;
    void line(const std::string& line);
};
const char* phase_name(Phase phase);
// Owns the complete process group/job. poll() never waits for a child or pipe.
class Process {
public:
    Process() = default;
    ~Process();
    Process(const Process&) = delete;
    Process& operator=(const Process&) = delete;
    bool start(const std::vector<std::string>& args, const std::filesystem::path& log, std::string& error);
    void poll(const std::function<void(const std::string&)>& on_line);
    void cancel();
    bool running() const { return active; }
    int exit_code() const { return result; }
private:
    void consume(const char* bytes, size_t count, const std::function<void(const std::string&)>& callback);
    bool active = false, cancelling = false;
    int result = -1;
    std::chrono::steady_clock::time_point cancelled_at;
    std::string pending;
    std::ofstream output;
#if defined(_WIN32)
    void* child = nullptr;
    void* pipe = nullptr;
    void* job = nullptr;
#else
    int child = -1, pipe = -1;
#endif
};
// Literal argv (no shell). protocol selects SBK_PROGRESS lines for the window.
std::vector<std::string> builder_command(const std::filesystem::path& install,
    const std::filesystem::path& rom, const std::filesystem::path& target, bool protocol = true);
}
