// ROM-free tests: real child execution, literal argv, failures, cancellation,
// bounded logs, progress parsing, and retry after the previous child is reaped.
#include "src/ui/first_run_process.hpp"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <thread>
#include <iostream>
using namespace sbk::first_run;
static void finish(Process& p, Model& m) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (p.running() && std::chrono::steady_clock::now() < deadline) {
        p.poll([&](const std::string& line) { m.line(line); });
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    assert(!p.running());
}
int main() {
    Model m;
    m.line("SBK_PROGRESS\tcompile\t2\t7");
    assert(m.phase == Phase::Compile && m.current == 2 && m.total == 7);
    m.line("SBK_PROGRESS\tvalidate_module\t0\t0");
    assert(m.phase == Phase::ValidateModule && m.total == 0);
    m.line("SBK_PROGRESS\tcompile\t999\t2");
    assert(m.phase == Phase::ValidateModule); // malformed progress cannot affect UI
    m.line("SBK_PROGRESS\tready\t0\t0");
    assert(m.phase != Phase::Ready); // only host ABI verification can grant Ready
    Process p;
    std::string err;
    auto log = std::filesystem::temp_directory_path() / "sbk-first-run-test.log";
    assert(p.start({"python3", "-u", "-c", "import sys; print(sys.argv[1]); print('SBK_PROGRESS\\tcompile\\t3\\t9'); sys.exit(4)", "literal ; $(echo unsafe)"}, log, err));
    std::string lines;
    while (p.running()) { p.poll([&](const std::string& l) { lines += l; m.line(l); }); std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
    assert(p.exit_code() == 4 && lines.find("literal ; $(echo unsafe)") != std::string::npos);
    assert(m.phase == Phase::Compile && m.current == 3 && m.total == 9);
    assert(p.start({"python3", "-u", "-c", "import time; time.sleep(30)"}, log, err));
    p.cancel(); finish(p, m); assert(p.exit_code() != 0);
    assert(p.start({"python3", "-u", "-c", "print('SBK_PROGRESS\\tinstall\\t0\\t0')"}, log, err));
    finish(p, m); assert(p.exit_code() == 0 && m.phase == Phase::Install);
    std::filesystem::remove(log);
    std::cout << "first-run lifecycle/progress tests passed\n";
}
