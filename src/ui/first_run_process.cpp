#include "first_run_process.hpp"
#include <algorithm>
#include <cstdlib>
#include <sstream>
#include <thread>
#include <cerrno>
#include <cstring>
#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
namespace sbk::first_run {
const char* phase_name(Phase p) {
    switch (p) {
    case Phase::Missing: return "Welcome to the slopes";
    case Phase::Select: return "Choose your ROM";
    case Phase::Validate: return "Validate ROM and tools";
    case Phase::GenerateCPU: return "Prepare CPU code";
    case Phase::GenerateRSP: return "Generate audio code";
    case Phase::Compile: return "Compile game module";
    case Phase::ValidateModule: return "Validate game module";
    case Phase::Install: return "Install game module";
    case Phase::Ready: return "Ready to ride";
    case Phase::Error: return "Setup needs attention";
    }
    return "Setup";
}
void Model::line(const std::string& s) {
    if (s.rfind("SBK_PROGRESS\t", 0) != 0) return;
    std::istringstream input(s.substr(13));
    std::string name, extra;
    int cur, tot;
    if (!(input >> name >> cur >> tot) || (input >> extra) || cur < 0 || tot < 0 || cur > tot) return;
    Phase next;
    if (name == "validate") next = Phase::Validate;
    else if (name == "cpu") next = Phase::GenerateCPU;
    else if (name == "rsp") next = Phase::GenerateRSP;
    else if (name == "compile") next = Phase::Compile;
    else if (name == "validate_module") next = Phase::ValidateModule;
    else if (name == "install") next = Phase::Install;
    else return; // Ready is owned by the host's ABI validation, never child output.
    phase = next;
    current = next == Phase::Compile ? cur : 0;
    total = next == Phase::Compile ? tot : 0;
}
std::vector<std::string> builder_command(const std::filesystem::path& install,
    const std::filesystem::path& rom, const std::filesystem::path& target, bool protocol) {
    std::filesystem::path script;
    if (const auto* env = std::getenv("SBK_BUILDER_SCRIPT"); env && std::filesystem::is_regular_file(env)) script = env;
    if (script.empty()) {
        const auto cwd = std::filesystem::current_path();
        for (const auto& p : {install / "scripts/build-game-module.py", install / "build-game-module.py",
                install / "../scripts/build-game-module.py", cwd / "scripts/build-game-module.py",
                cwd / "../scripts/build-game-module.py", cwd / "build-game-module.py"}) {
            if (std::filesystem::is_regular_file(p)) { script = std::filesystem::absolute(p); break; }
        }
    }
    if (script.empty()) return {};
#ifdef _WIN32
    const char* python = "python";
#else
    const char* python = "python3";
#endif
    std::vector<std::string> args{python, "-u", script.string(), rom.string(), "--out-dir", target.string(), "--non-interactive"};
    if (protocol) args.push_back("--progress-protocol");
    return args;
}
void Process::consume(const char* bytes, size_t count, const std::function<void(const std::string&)>& callback) {
    output.write(bytes, count); output.flush();
    for (size_t i = 0; i < count; ++i) {
        if (bytes[i] == '\n') { callback(pending); pending.clear(); }
        else if (bytes[i] != '\r' && pending.size() < 8192) pending += bytes[i];
    }
}
#ifdef _WIN32
static std::wstring widen(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0); MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n); return w;
}
static std::wstring quote(const std::string& s) {
    std::wstring out = L"\""; unsigned slashes = 0;
    for (wchar_t c : widen(s)) {
        if (c == L'\\') { ++slashes; continue; }
        out.append(slashes * (c == L'"' ? 2 : 1), L'\\'); slashes = 0;
        if (c == L'"') out += L'\\'; out += c;
    }
    out.append(slashes * 2, L'\\'); return out + L'"';
}
#endif
bool Process::start(const std::vector<std::string>& args, const std::filesystem::path& log, std::string& error) {
    if (active || args.empty()) { error = "Builder unavailable or already running."; return false; }
    output.close(); output.clear(); output.open(log, std::ios::out | std::ios::app);
    if (!output) { error = "Cannot write the setup log: " + log.string(); return false; }
    output << "\n--- New setup attempt ---\n"; output.flush(); pending.clear(); result = -1; cancelling = false;
#ifdef _WIN32
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE}; HANDLE read = nullptr, write = nullptr;
    if (!CreatePipe(&read, &write, &sa, 0)) { error = "Cannot create builder output pipe."; return false; }
    SetHandleInformation(read, HANDLE_FLAG_INHERIT, 0);
    HANDLE owned_job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{}; limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!owned_job || !SetInformationJobObject(owned_job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
        CloseHandle(read); CloseHandle(write); if (owned_job) CloseHandle(owned_job); error = "Cannot create builder process job."; return false;
    }
    STARTUPINFOW si{}; si.cb = sizeof(si); si.dwFlags = STARTF_USESTDHANDLES; si.hStdOutput = write; si.hStdError = write; si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi{}; std::wstring cmd;
    for (auto& a : args) { if (!cmd.empty()) cmd += L' '; cmd += quote(a); }
    bool ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr, &si, &pi);
    CloseHandle(write);
    if (!ok || !AssignProcessToJobObject(owned_job, pi.hProcess)) {
        if (ok) { TerminateProcess(pi.hProcess, 8); WaitForSingleObject(pi.hProcess, INFINITE); CloseHandle(pi.hThread); CloseHandle(pi.hProcess); }
        CloseHandle(read); CloseHandle(owned_job); error = "Cannot launch Python builder."; return false;
    }
    ResumeThread(pi.hThread); CloseHandle(pi.hThread); child = pi.hProcess; pipe = read; job = owned_job;
#else
    int fds[2]; if (::pipe(fds) < 0) { error = std::strerror(errno); return false; }
    fcntl(fds[0], F_SETFD, FD_CLOEXEC); fcntl(fds[1], F_SETFD, FD_CLOEXEC);
    std::vector<char*> argv; for (auto& s : args) argv.push_back(const_cast<char*>(s.c_str())); argv.push_back(nullptr);
    int pid = fork();
    if (pid == 0) {
        setpgid(0, 0); dup2(fds[1], STDOUT_FILENO); dup2(fds[1], STDERR_FILENO);
        close(fds[0]); close(fds[1]); execvp(argv[0], argv.data());
        const char msg[] = "Cannot launch Python builder. Install Python 3 and retry.\n";
        (void)!write(STDERR_FILENO, msg, sizeof(msg) - 1); _exit(127);
    }
    close(fds[1]);
    if (pid < 0) { close(fds[0]); error = std::strerror(errno); return false; }
    setpgid(pid, pid); child = pid; pipe = fds[0]; fcntl(pipe, F_SETFL, O_NONBLOCK);
#endif
    active = true; return true;
}
void Process::cancel() {
    if (!active || cancelling) return;
    cancelling = true; cancelled_at = std::chrono::steady_clock::now();
#ifdef _WIN32
    TerminateJobObject(job, 8);
#else
    kill(-child, SIGTERM);
#endif
}
void Process::poll(const std::function<void(const std::string&)>& on_line) {
    if (!active) return;
    // Bound work per frame even when the compiler floods stderr.
    char buffer[4096];
    for (int n = 0; n < 32; ++n) {
#ifdef _WIN32
        DWORD available = 0, got = 0;
        if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr) || !available) break;
        if (!ReadFile(pipe, buffer, (DWORD)std::min<size_t>(sizeof(buffer), available), &got, nullptr) || !got) break;
#else
        auto got = read(pipe, buffer, sizeof(buffer)); if (got <= 0) break;
#endif
        consume(buffer, got, on_line);
    }
#ifdef _WIN32
    if (WaitForSingleObject(child, 0) != WAIT_OBJECT_0) return;
    DWORD code; GetExitCodeProcess(child, &code); result = (int)code;
    CloseHandle(child); CloseHandle(job); child = job = nullptr;
    CloseHandle(pipe); pipe = nullptr;
#else
    if (cancelling && std::chrono::steady_clock::now() - cancelled_at > std::chrono::milliseconds(1500)) kill(-child, SIGKILL);
    int status = 0; int ended = waitpid(child, &status, WNOHANG);
    if (ended == 0 || (ended < 0 && errno == EINTR)) return;
    result = ended < 0 ? 1 : WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    // End compiler descendants even when the parent exits unexpectedly.
    kill(-child, SIGKILL);
    // Final drain after exit (bounded pipe capacity), preserving the last status.
    while (true) { auto got = read(pipe, buffer, sizeof(buffer)); if (got <= 0) break; consume(buffer, got, on_line); }
    close(pipe); child = pipe = -1;
#endif
    if (!pending.empty()) { on_line(pending); pending.clear(); }
    output.flush(); output.close(); active = false;
}
Process::~Process() {
    cancel();
    while (active) { poll([](const std::string&) {}); if (active) std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
}
}
