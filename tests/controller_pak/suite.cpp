// Suite driver: runs every mode of this executable in a fresh process, so the
// cross-process A/B/C persistence checks never share in-memory state.
#include <cerrno>
#include <filesystem>
#include <initializer_list>
#include <iostream>
#include <random>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace fs = std::filesystem;

// Read from the compiler's own predefined macros: this is what built the test.
std::string compiler_id() {
#if defined(__clang__) && defined(_MSC_VER)
    return "ClangCl " __clang_version__;
#elif defined(_MSC_VER)
    return "MSVC " + std::to_string(_MSC_FULL_VER);
#elif defined(__clang__)
    return "Clang " __clang_version__;
#elif defined(__MINGW32__)
    return "GNU (MinGW) " __VERSION__;
#elif defined(__GNUC__)
    return "GNU " __VERSION__;
#else
    return "unknown";
#endif
}

namespace {
fs::path self_path(const char* argv0) {
#ifdef _WIN32
    std::wstring buffer(32768, L'\0');
    const DWORD size = GetModuleFileNameW(nullptr, buffer.data(), DWORD(buffer.size()));
    if (size > 0 && size < buffer.size()) {
        buffer.resize(size);
        return buffer;
    }
#elif defined(__linux__)
    std::error_code error;
    auto path = fs::read_symlink("/proc/self/exe", error);
    if (!error) return path;
#endif
    return fs::absolute(argv0);
}

#ifdef _WIN32
// Inverse of CommandLineToArgvW: spaces and non-ASCII survive unchanged.
std::wstring quote(const std::wstring& arg) {
    std::wstring out = L"\"";
    size_t slashes = 0;
    for (wchar_t c : arg) {
        if (c == L'\\') {
            ++slashes;
            continue;
        }
        out.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\');
        slashes = 0;
        out += c;
    }
    out.append(slashes * 2, L'\\');
    return out + L'"';
}
#endif

bool run_child(const fs::path& self, const std::vector<fs::path>& args) {
    std::cout.flush();
#ifdef _WIN32
    std::wstring command = quote(self.wstring());
    for (const auto& arg : args) command += L" " + quote(arg.wstring());
    STARTUPINFOW startup{};
    startup.cb = sizeof startup;
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    for (HANDLE handle : {startup.hStdInput, startup.hStdOutput, startup.hStdError})
        if (handle && handle != INVALID_HANDLE_VALUE)
            SetHandleInformation(handle, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(self.c_str(), command.data(), nullptr, nullptr, TRUE, 0,
                        nullptr, nullptr, &startup, &process))
        return false;
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return code == 0;
#else
    std::vector<std::string> storage{self.string()};
    for (const auto& arg : args) storage.push_back(arg.string());
    std::vector<char*> argv;
    for (auto& arg : storage) argv.push_back(arg.data());
    argv.push_back(nullptr);
    pid_t pid;
    if (posix_spawn(&pid, storage[0].c_str(), nullptr, nullptr, argv.data(), environ) != 0)
        return false;
    int status = 0;
    while (waitpid(pid, &status, 0) < 0)
        if (errno != EINTR) return false;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
#endif
}

// Spaces in every scratch path; removed on success and failure alike.
struct Scratch {
    fs::path path;
    Scratch() {
        std::random_device random;
        const auto temp = fs::temp_directory_path();
        for (int attempt = 0; attempt < 100 && path.empty(); ++attempt) {
            auto candidate = temp / ("Snowboard Kids PFS Test " + std::to_string(random()));
            if (fs::create_directory(candidate)) path = candidate;
        }
    }
    ~Scratch() {
        std::error_code error;
        if (!path.empty()) fs::remove_all(path, error);
    }
};
} // namespace

int run_suite(const char* argv0) {
    std::cout << "compiler: " << compiler_id() << "\n";
    const auto self = self_path(argv0);
    Scratch scratch;
    if (scratch.path.empty()) {
        std::cerr << "FAIL could not create a scratch directory\n";
        return 1;
    }
    const auto& root = scratch.path;
    const auto durable = root / "durable pak";
    std::vector<std::vector<fs::path>> runs = {
        {"normal", root / "normal"},
        {"unicode", root / "unicode"},
        {"a", durable},
        {"b", durable},
        {"c", durable},
    };
    for (const char* kind : {"truncate", "wrong-size", "id", "id-all", "inode", "directory", "chain"})
        runs.push_back({"corrupt", root / kind, kind});
    runs.push_back({"hle", root / "hle"});
    for (const auto& run : runs) {
        if (!run_child(self, run)) {
            std::cerr << "FAIL mode " << run[0].string() << " (" << run[1].string() << ")\n";
            return 1;
        }
    }
    std::cout << "PASS Controller Pak suite: " << runs.size() << " isolated processes\n";
    return 0;
}
