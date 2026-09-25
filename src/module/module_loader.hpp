#pragma once

#include "module_abi.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace sbk::module {

enum class LoadStatus {
    Success = 0,
    FileNotFound,
    LinkerError,
    MissingExport,
    NullApi,
    InvalidMagic,
    UnsupportedAbi,
    ModuleTooOld,
    ModuleTooNew,
    InvalidStructSize,
    RomMismatch,
    GameIdMismatch,
    CorpusMismatch,
    MissingMandatorySymbols,
    InitFailed
};

const char* status_string(LoadStatus status);

class GameModule {
public:
    GameModule();
    ~GameModule();

    // Non-copyable, movable
    GameModule(const GameModule&) = delete;
    GameModule& operator=(const GameModule&) = delete;
    GameModule(GameModule&& other) noexcept;
    GameModule& operator=(GameModule&& other) noexcept;

    // Load from an explicit path
    LoadStatus load(const std::filesystem::path& path, std::string& error_out);

    // Discover and load using standard candidate locations
    LoadStatus load_candidate(const std::filesystem::path& app_dir,
                              const std::filesystem::path& user_data_dir,
                              const std::filesystem::path& explicit_path,
                              std::string& error_out);

    // Overload for backwards compatibility
    LoadStatus load_candidate(const std::filesystem::path& app_dir,
                              const std::filesystem::path& explicit_path,
                              std::string& error_out) {
        return load_candidate(app_dir, {}, explicit_path, error_out);
    }

    // Validate module against expected ROM identity, game id, and optional corpus digest
    bool validate(uint64_t expected_rom_hash,
                  const char* expected_game_id,
                  std::string& error_out,
                  uint64_t expected_corpus_digest = 0,
                  LoadStatus* specific_error = nullptr) const;

    // Initialize with engine API
    bool initialize(const SbkEngineApiV1& engine_api, std::string& error_out);

    // Unload the module cleanly
    void unload();

    bool is_loaded() const { return handle_ != nullptr && api_ != nullptr; }
    const SbkGameModuleApiV1* api() const { return api_; }
    const std::filesystem::path& loaded_path() const { return loaded_path_; }

    static std::vector<std::filesystem::path> candidate_paths(
        const std::filesystem::path& app_dir,
        const std::filesystem::path& user_data_dir = {},
        const std::filesystem::path& explicit_path = {});

private:
    void* handle_ = nullptr;
    const SbkGameModuleApiV1* api_ = nullptr;
    std::filesystem::path loaded_path_;
    bool initialized_ = false;
};

} // namespace sbk::module
