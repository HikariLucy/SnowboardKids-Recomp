#include "module_loader.hpp"

#include <cstring>
#include <cstdio>
#include <cstdlib>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace sbk::module {

const char* status_string(LoadStatus status) {
    switch (status) {
        case LoadStatus::Success: return "Success";
        case LoadStatus::FileNotFound: return "Module file not found";
        case LoadStatus::LinkerError: return "Dynamic linker error";
        case LoadStatus::MissingExport: return "Missing canonical export sbk_game_module_get_api";
        case LoadStatus::NullApi: return "Module export returned null API pointer";
        case LoadStatus::InvalidMagic: return "Invalid module magic: not a valid SBK game module";
        case LoadStatus::UnsupportedAbi: return "Unsupported module ABI version";
        case LoadStatus::ModuleTooOld: return "MODULE_TOO_OLD: Module ABI is older than required by engine";
        case LoadStatus::ModuleTooNew: return "MODULE_TOO_NEW: Module ABI is newer than supported by engine";
        case LoadStatus::InvalidStructSize: return "Module struct size is smaller than expected ABI size";
        case LoadStatus::RomMismatch: return "WRONG_ROM: Module was generated for a different ROM image";
        case LoadStatus::GameIdMismatch: return "WRONG_GAME: Module game ID does not match expected Snowboard Kids identifier";
        case LoadStatus::CorpusMismatch: return "CORPUS_MISMATCH: Module continuation corpus does not match engine expectation";
        case LoadStatus::MissingMandatorySymbols: return "Module is missing mandatory function pointers";
        case LoadStatus::InitFailed: return "Module initialization returned failure";
    }
    return "Unknown load status";
}

GameModule::GameModule() = default;

GameModule::~GameModule() {
    unload();
}

GameModule::GameModule(GameModule&& other) noexcept
    : handle_(other.handle_),
      api_(other.api_),
      loaded_path_(std::move(other.loaded_path_)),
      initialized_(other.initialized_) {
    other.handle_ = nullptr;
    other.api_ = nullptr;
    other.initialized_ = false;
}

GameModule& GameModule::operator=(GameModule&& other) noexcept {
    if (this != &other) {
        unload();
        handle_ = other.handle_;
        api_ = other.api_;
        loaded_path_ = std::move(other.loaded_path_);
        initialized_ = other.initialized_;
        other.handle_ = nullptr;
        other.api_ = nullptr;
        other.initialized_ = false;
    }
    return *this;
}

LoadStatus GameModule::load(const std::filesystem::path& path, std::string& error_out) {
    unload();

    if (!std::filesystem::exists(path)) {
        error_out = "File does not exist: " + path.string();
        return LoadStatus::FileNotFound;
    }

    void* handle = nullptr;
#if defined(_WIN32)
    handle = static_cast<void*>(LoadLibraryW(path.wstring().c_str()));
    if (!handle) {
        DWORD err = GetLastError();
        error_out = "LoadLibrary failed with error code " + std::to_string(err) + " on: " + path.string();
        return LoadStatus::LinkerError;
    }
#else
    handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        const char* err = dlerror();
        error_out = err ? err : "Unknown dlopen error";
        return LoadStatus::LinkerError;
    }
#endif

    void* symbol = nullptr;
#if defined(_WIN32)
    symbol = reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(handle), SBK_MODULE_EXPORT_SYMBOL));
#else
    symbol = dlsym(handle, SBK_MODULE_EXPORT_SYMBOL);
#endif

    if (!symbol) {
        error_out = std::string("MISSING_EXPORT: Export symbol '") + SBK_MODULE_EXPORT_SYMBOL + "' not found in " + path.string();
#if defined(_WIN32)
        FreeLibrary(static_cast<HMODULE>(handle));
#else
        dlclose(handle);
#endif
        return LoadStatus::MissingExport;
    }

    auto get_api_fn = reinterpret_cast<SbkGetGameModuleApiFn>(symbol);
    const SbkGameModuleApiV1* api = get_api_fn();
    if (!api) {
        error_out = "sbk_game_module_get_api() returned null pointer";
#if defined(_WIN32)
        FreeLibrary(static_cast<HMODULE>(handle));
#else
        dlclose(handle);
#endif
        return LoadStatus::NullApi;
    }

    if (api->magic != SBK_MODULE_MAGIC) {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "Magic mismatch: got 0x%016llX, expected 0x%016llX",
                      static_cast<unsigned long long>(api->magic),
                      static_cast<unsigned long long>(SBK_MODULE_MAGIC));
        error_out = buf;
#if defined(_WIN32)
        FreeLibrary(static_cast<HMODULE>(handle));
#else
        dlclose(handle);
#endif
        return LoadStatus::InvalidMagic;
    }

    if (api->abi_version < SBK_MODULE_ABI_VERSION) {
        error_out = "MODULE_TOO_OLD: Module ABI version " + std::to_string(api->abi_version) +
                    " is older than required ABI version " + std::to_string(SBK_MODULE_ABI_VERSION);
#if defined(_WIN32)
        FreeLibrary(static_cast<HMODULE>(handle));
#else
        dlclose(handle);
#endif
        return LoadStatus::ModuleTooOld;
    }

    if (api->abi_version > SBK_MODULE_ABI_VERSION) {
        error_out = "MODULE_TOO_NEW: Module ABI version " + std::to_string(api->abi_version) +
                    " is newer than supported ABI version " + std::to_string(SBK_MODULE_ABI_VERSION);
#if defined(_WIN32)
        FreeLibrary(static_cast<HMODULE>(handle));
#else
        dlclose(handle);
#endif
        return LoadStatus::ModuleTooNew;
    }

    if (api->struct_size < sizeof(SbkGameModuleApiV1)) {
        error_out = "Struct size mismatch: module reported " + std::to_string(api->struct_size) +
                    ", engine requires at least " + std::to_string(sizeof(SbkGameModuleApiV1));
#if defined(_WIN32)
        FreeLibrary(static_cast<HMODULE>(handle));
#else
        dlclose(handle);
#endif
        return LoadStatus::InvalidStructSize;
    }

    if (!api->entrypoint || !api->get_rsp_microcode || !api->register_overlays) {
        error_out = "Module missing mandatory entrypoint / RSP / overlay function pointers";
#if defined(_WIN32)
        FreeLibrary(static_cast<HMODULE>(handle));
#else
        dlclose(handle);
#endif
        return LoadStatus::MissingMandatorySymbols;
    }

    handle_ = handle;
    api_ = api;
    loaded_path_ = std::filesystem::absolute(path);
    initialized_ = false;
    return LoadStatus::Success;
}

bool GameModule::validate(uint64_t expected_rom_hash,
                          const char* expected_game_id,
                          std::string& error_out,
                          uint64_t expected_corpus_digest,
                          LoadStatus* specific_error) const {
    if (!is_loaded()) {
        error_out = "No module currently loaded";
        return false;
    }

    if (expected_rom_hash != 0 && api_->rom_hash != 0 && api_->rom_hash != expected_rom_hash) {
        char buf[160];
        std::snprintf(buf, sizeof(buf), "WRONG_ROM: Module was built for ROM hash 0x%016llX, expected 0x%016llX",
                      static_cast<unsigned long long>(api_->rom_hash),
                      static_cast<unsigned long long>(expected_rom_hash));
        error_out = buf;
        if (specific_error) *specific_error = LoadStatus::RomMismatch;
        return false;
    }

    if (expected_game_id && api_->game_id && std::strcmp(api_->game_id, expected_game_id) != 0) {
        error_out = std::string("WRONG_GAME: Module game ID '") + api_->game_id +
                    "', expected '" + expected_game_id + "'";
        if (specific_error) *specific_error = LoadStatus::GameIdMismatch;
        return false;
    }

    if (expected_corpus_digest != 0 && api_->corpus_digest != 0 && api_->corpus_digest != expected_corpus_digest) {
        char buf[160];
        std::snprintf(buf, sizeof(buf), "CORPUS_MISMATCH: Module corpus digest 0x%016llX differs from expected 0x%016llX",
                      static_cast<unsigned long long>(api_->corpus_digest),
                      static_cast<unsigned long long>(expected_corpus_digest));
        error_out = buf;
        if (specific_error) *specific_error = LoadStatus::CorpusMismatch;
        return false;
    }

    return true;
}

bool GameModule::initialize(const SbkEngineApiV1& engine_api, std::string& error_out) {
    if (!is_loaded()) {
        error_out = "No module currently loaded";
        return false;
    }

    if (api_->init) {
        int rc = api_->init(&engine_api);
        if (rc != 0) {
            error_out = "Module init() returned error code " + std::to_string(rc);
            return false;
        }
    }
    initialized_ = true;
    return true;
}

void GameModule::unload() {
    if (initialized_ && api_ && api_->shutdown) {
        api_->shutdown();
        initialized_ = false;
    }
    if (handle_) {
#if defined(_WIN32)
        FreeLibrary(static_cast<HMODULE>(handle_));
#else
        dlclose(handle_);
#endif
        handle_ = nullptr;
    }
    api_ = nullptr;
    loaded_path_.clear();
}

std::vector<std::filesystem::path> GameModule::candidate_paths(
    const std::filesystem::path& app_dir,
    const std::filesystem::path& user_data_dir,
    const std::filesystem::path& explicit_path) {
    std::vector<std::filesystem::path> candidates;

    if (!explicit_path.empty()) {
        candidates.push_back(explicit_path);
    }

    if (const char* env = std::getenv("SBK_GAME_MODULE"); env && *env) {
        candidates.push_back(std::filesystem::path(env));
    }

#if defined(_WIN32)
    const std::string mod_name = "SnowboardKidsGame.dll";
#elif defined(__APPLE__)
    const std::string mod_name = "SnowboardKidsGame.dylib";
#else
    const std::string mod_name = "SnowboardKidsGame.so";
#endif

    // User data directory candidates (FASE 5)
    if (!user_data_dir.empty()) {
        candidates.push_back(user_data_dir / "modules" / "snowboardkids-us" / mod_name);
        candidates.push_back(user_data_dir / "modules" / mod_name);
    } else {
        candidates.push_back(std::filesystem::current_path() / "modules" / "snowboardkids-us" / mod_name);
        candidates.push_back(std::filesystem::current_path() / "modules" / mod_name);
        candidates.push_back(std::filesystem::current_path() / mod_name);
        candidates.push_back(std::filesystem::current_path() / "build" / mod_name);
    }

    // Only search application directory if no user data directory was provided
    if (user_data_dir.empty() && !app_dir.empty()) {
        candidates.push_back(app_dir / "modules" / "snowboardkids-us" / mod_name);
        candidates.push_back(app_dir / "modules" / mod_name);
        candidates.push_back(app_dir / mod_name);
    }

    return candidates;
}

LoadStatus GameModule::load_candidate(const std::filesystem::path& app_dir,
                                      const std::filesystem::path& user_data_dir,
                                      const std::filesystem::path& explicit_path,
                                      std::string& error_out) {
    auto candidates = candidate_paths(app_dir, user_data_dir, explicit_path);
    std::string last_error;
    for (const auto& path : candidates) {
        if (std::filesystem::exists(path)) {
            LoadStatus st = load(path, error_out);
            if (st == LoadStatus::Success) {
                return LoadStatus::Success;
            }
            last_error = error_out;
        }
    }
    if (!last_error.empty()) {
        error_out = last_error;
    } else {
        error_out = "No game module found in searched candidate paths.";
    }
    return LoadStatus::FileNotFound;
}

} // namespace sbk::module
