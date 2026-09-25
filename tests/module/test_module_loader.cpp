#include "module/module_loader.hpp"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <vector>

#define CHECK(expr, msg) \
    do { \
        if (!(expr)) { \
            std::fprintf(stderr, "FAIL: %s at %s:%d: %s\n", #expr, __FILE__, __LINE__, msg); \
            std::exit(1); \
        } \
    } while (0)

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "Usage: %s <path_to_synthetic_module>\n", argv[0]);
        return 2;
    }

    std::filesystem::path module_path = argv[1];
    std::cout << "[TEST] Running module loader tests with: " << module_path << std::endl;

    sbk::module::GameModule module;
    std::string err;

    // 1. Missing module test
    {
        sbk::module::GameModule missing;
        auto st = missing.load("non_existent_file_xyz.so", err);
        CHECK(st == sbk::module::LoadStatus::FileNotFound, "Missing module must report FileNotFound");
        CHECK(!err.empty(), "Error message must not be empty on missing module");
        std::cout << "[PASS] Missing module cleanly rejected: " << err << std::endl;
    }

    // 2. Load valid synthetic module
    {
        auto st = module.load(module_path, err);
        CHECK(st == sbk::module::LoadStatus::Success, err.c_str());
        CHECK(module.is_loaded(), "Module must report is_loaded()");
        CHECK(module.api() != nullptr, "Module API must be non-null");
        CHECK(module.api()->magic == SBK_MODULE_MAGIC, "Magic must match SBK_MODULE_MAGIC");
        CHECK(module.api()->abi_version == SBK_MODULE_ABI_VERSION, "ABI version must match");
        std::cout << "[PASS] Successfully loaded synthetic module" << std::endl;
    }

    // 3. Validation tests
    {
        // Valid validation
        bool ok = module.validate(0xF384619787B78D4BULL, "snowboardkids.n64.us", err);
        CHECK(ok, err.c_str());

        // Valid with matching corpus digest
        ok = module.validate(0xF384619787B78D4BULL, "snowboardkids.n64.us", err, 0x1234567890ABCDEFULL);
        CHECK(ok, err.c_str());

        // Corpus mismatch test
        sbk::module::LoadStatus corpus_err_status = sbk::module::LoadStatus::Success;
        ok = module.validate(0xF384619787B78D4BULL, "snowboardkids.n64.us", err, 0x9999888877776666ULL, &corpus_err_status);
        CHECK(!ok, "Corpus mismatch must fail validation");
        CHECK(corpus_err_status == sbk::module::LoadStatus::CorpusMismatch, "Status must be CorpusMismatch");
        CHECK(err.find("CORPUS_MISMATCH") != std::string::npos, "Error must distinguish CORPUS_MISMATCH");
        std::cout << "[PASS] Corpus mismatch cleanly rejected: " << err << std::endl;

        // ROM hash mismatch
        sbk::module::LoadStatus rom_err_status = sbk::module::LoadStatus::Success;
        ok = module.validate(0x1111222233334444ULL, "snowboardkids.n64.us", err, 0, &rom_err_status);
        CHECK(!ok, "ROM mismatch must fail validation");
        CHECK(rom_err_status == sbk::module::LoadStatus::RomMismatch, "Status must be RomMismatch");
        CHECK(err.find("WRONG_ROM") != std::string::npos, "Error must distinguish WRONG_ROM");
        std::cout << "[PASS] ROM mismatch cleanly rejected: " << err << std::endl;

        // Game ID mismatch
        sbk::module::LoadStatus game_err_status = sbk::module::LoadStatus::Success;
        ok = module.validate(0xF384619787B78D4BULL, "supermario64.n64.us", err, 0, &game_err_status);
        CHECK(!ok, "Game ID mismatch must fail validation");
        CHECK(game_err_status == sbk::module::LoadStatus::GameIdMismatch, "Status must be GameIdMismatch");
        CHECK(err.find("WRONG_GAME") != std::string::npos, "Error must distinguish WRONG_GAME");
        std::cout << "[PASS] Game ID mismatch cleanly rejected: " << err << std::endl;
    }

    // 4. Candidate paths test
    {
        std::filesystem::path app_dir = "/fake/app";
        std::filesystem::path user_data = "/fake/userdata";
        auto candidates = sbk::module::GameModule::candidate_paths(app_dir, user_data);
        CHECK(!candidates.empty(), "Candidate paths must not be empty");
        bool found_userdata = false;
        for (const auto& c : candidates) {
            if (c.string().find("/fake/userdata/modules") != std::string::npos) {
                found_userdata = true;
                break;
            }
        }
        CHECK(found_userdata, "Candidate paths must search user data modules directory");
        std::cout << "[PASS] User data candidate search path verified" << std::endl;
    }

    // 5. Initialize module
    {
        SbkEngineApiV1 engine_api{};
        engine_api.abi_version = 1;
        engine_api.struct_size = sizeof(SbkEngineApiV1);
        bool ok = module.initialize(engine_api, err);
        CHECK(ok, err.c_str());
        std::cout << "[PASS] Module initialize() completed" << std::endl;
    }

    // 6. Entrypoint invocation
    {
        std::vector<uint8_t> rdram(4096, 0);
        module.api()->entrypoint(rdram.data(), nullptr);
        CHECK(rdram[0] == 0xAA && rdram[1] == 0xBB && rdram[2] == 0xCC && rdram[3] == 0xDD,
              "Synthetic entrypoint must write expected byte pattern to RDRAM");
        std::cout << "[PASS] Synthetic entrypoint verified" << std::endl;
    }

    // 7. RSP resolver
    {
        auto ucode = module.api()->get_rsp_microcode(nullptr);
        CHECK(ucode != nullptr, "RSP microcode function pointer must be non-null");
        std::cout << "[PASS] RSP microcode resolver verified" << std::endl;
    }

    // 8. Continuation step dispatch
    {
        CHECK(module.api()->continuation_count == 1, "Expected 1 synthetic continuation");
        const auto& desc = module.api()->continuations[0];
        CHECK(desc.id == 1001ull, "Expected id 1001");
        CHECK(desc.scratch_count == 1, "Expected scratch_count 1");

        SbkFrame frame{};
        frame.function = desc.id;
        frame.continuation = 0;
        frame.scratch_count = 1;

        // Step 1: Yields and sets scratch[0] = 42
        SbkAction a1 = desc.step(nullptr, nullptr, &frame);
        CHECK(a1.kind == SBK_ACTION_YIELD, "Step 1 must return Yield");
        CHECK(frame.continuation == 101, "Frame continuation must be updated to 101");
        CHECK(frame.scratch[0] == 42, "Frame scratch[0] must be 42");

        // Step 2: Resumes at 101, returns Return with result = 84
        SbkAction a2 = desc.step(nullptr, nullptr, &frame);
        CHECK(a2.kind == SBK_ACTION_RETURN, "Step 2 must return Return");
        CHECK(frame.continuation == 0, "Frame continuation must be 0");
        CHECK(frame.result == 84, "Frame result must be 84 (42 * 2)");

        std::cout << "[PASS] Continuation step dispatch across module boundary verified" << std::endl;
    }

    // 9. Clean unload
    {
        module.unload();
        CHECK(!module.is_loaded(), "Module must report !is_loaded() after unload()");
        CHECK(module.api() == nullptr, "Module api() must be null after unload");
        std::cout << "[PASS] Clean unload verified" << std::endl;
    }

    std::cout << "\nALL MODULE LOADER TESTS PASSED SUCCESSFULLY!" << std::endl;
    return 0;
}
