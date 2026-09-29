// ROM-free probe of RT64's run-time DXIL path on Windows.
//
// RT64 (D3D12) compiles generated shader text as lib_6_3 and links it with the
// library shaders embedded at build time (RT64::RasterShader). This probe does
// exactly that with RT64's own ShaderCompiler and generateShaderText, writes
// the linked containers for tests/release/dxil_hash.py, and checks:
//   - every variant compiles and links, and the result carries a hash;
//   - dxcompiler.dll never loads dxil.dll (the separately licensed validator),
//     even when one is on the search path (gate);
//   - D3D12 on the WARP adapter accepts such shaders and rejects them with a
//     zeroed or corrupted hash (controls), with and without the debug layer,
//     reporting any OS component that loads dxil.dll meanwhile.
//
//   SnowboardKidsDxcRuntimeProbe <out-dir> [--report-only] [--control-load]
//
// --control-load finally calls LoadLibraryW(L"dxil.dll") to show whether a
// validator was findable on the search path during the run.
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "render/rt64_raster_shader.h"
#include "render/rt64_shader_compiler.h"
#include "shaders/RasterPSLibrary.hlsl.dxil.h"
#include "shaders/RasterPSLibraryMS.hlsl.dxil.h"
#include "shaders/RasterVSLibrary.hlsl.dxil.h"

namespace {

int failures = 0;

void check(bool ok, const char *what) {
    std::printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) failures++;
}

std::wstring modulePath(const wchar_t *name) {
    HMODULE module = GetModuleHandleW(name);
    if (!module) return L"";
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(module, path, MAX_PATH);
    return path;
}

std::vector<uint8_t> bytes(IDxcBlob *blob) {
    const auto *data = static_cast<const uint8_t *>(blob->GetBufferPointer());
    return std::vector<uint8_t>(data, data + blob->GetBufferSize());
}

bool hasHash(const std::vector<uint8_t> &container) {
    if (container.size() < 32 || std::memcmp(container.data(), "DXBC", 4) != 0) return false;
    for (int i = 4; i < 20; i++) {
        if (container[i] != 0) return true;
    }
    return false;
}

// The compiler version string in a container's VERS part, if any.
std::string compilerVersion(const void *data, size_t size) {
    const auto *base = static_cast<const uint8_t *>(data);
    if (size < 32 || std::memcmp(base, "DXBC", 4) != 0) return "not a container";
    uint32_t count = 0;
    std::memcpy(&count, base + 28, 4);
    for (uint32_t i = 0; i < count; i++) {
        uint32_t offset = 0;
        std::memcpy(&offset, base + 32 + 4 * i, 4);
        if (std::memcmp(base + offset, "VERS", 4) == 0) {
            // DxilCompilerVersion: u16 major, u16 minor, u32 flags, u32 commits,
            // u32 string bytes, then "<commit hash>\0<version>\0".
            const char *strings = reinterpret_cast<const char *>(base + offset + 8 + 16);
            return std::string(strings) + " / " + std::string(strings + std::strlen(strings) + 1);
        }
    }
    return "no VERS part";
}

void save(const std::filesystem::path &dir, const std::string &name, const std::vector<uint8_t> &data) {
    std::ofstream(dir / name, std::ios::binary).write(reinterpret_cast<const char *>(data.data()), data.size());
}

// RT64::RasterShader's DXIL branch, verbatim apart from error handling.
void compileAndLink(const RT64::ShaderCompiler &compiler, const RT64::ShaderDescription &desc, bool msaa,
                    std::vector<uint8_t> &vs, std::vector<uint8_t> &ps) {
    RT64::RasterShaderText text = RT64::RasterShader::generateShaderText(desc, msaa);
    static const wchar_t *vsNames[] = {L"RasterVSEntry", L"RasterVSLibrary"};
    static const wchar_t *psNames[] = {L"RasterPSEntry", L"RasterPSLibrary"};
    IDxcBlob *vsLibs[] = {nullptr, nullptr};
    IDxcBlob *psLibs[] = {nullptr, nullptr};
    compiler.dxcUtils->CreateBlobFromPinned(RasterVSLibraryBlobDXIL, sizeof(RasterVSLibraryBlobDXIL), DXC_CP_ACP,
                                            reinterpret_cast<IDxcBlobEncoding **>(&vsLibs[1]));
    const void *psLib = msaa ? RasterPSLibraryMSBlobDXIL : RasterPSLibraryBlobDXIL;
    const uint32_t psLibSize = msaa ? sizeof(RasterPSLibraryMSBlobDXIL) : sizeof(RasterPSLibraryBlobDXIL);
    compiler.dxcUtils->CreateBlobFromPinned(psLib, psLibSize, DXC_CP_ACP, reinterpret_cast<IDxcBlobEncoding **>(&psLibs[1]));
    compiler.compile(text.vertexShader, L"VSMain", L"lib_6_3", RenderShaderFormat::DXIL, &vsLibs[0]);
    compiler.compile(text.pixelShader, L"PSMain", L"lib_6_3", RenderShaderFormat::DXIL, &psLibs[0]);
    if (!vsLibs[0] || !psLibs[0]) throw std::runtime_error("library compilation produced no blob");
    IDxcBlob *vsBlob = nullptr;
    IDxcBlob *psBlob = nullptr;
    compiler.link(L"VSMain", L"vs_6_3", vsLibs, vsNames, 2, &vsBlob);
    compiler.link(L"PSMain", L"ps_6_3", psLibs, psNames, 2, &psBlob);
    if (!vsBlob || !psBlob) throw std::runtime_error("link produced no blob");
    vs = bytes(vsBlob);
    ps = bytes(psBlob);
    for (IDxcBlob *blob : {vsLibs[0], vsLibs[1], psLibs[0], psLibs[1], vsBlob, psBlob}) blob->Release();
}

constexpr const char *TrivialVS =
    "float4 VSMain(uint id : SV_VertexID) : SV_Position {"
    "  return float4((id & 1) * 2.0 - 1.0, (id >> 1) * 2.0 - 1.0, 0, 1); }";
constexpr const char *TrivialPSEntry =
    "float4 shade(float4 c);"
    "[shader(\"pixel\")] float4 PSMain() : SV_Target { return shade(float4(1, 0.5, 0.25, 1)); }";
constexpr const char *TrivialPSCompiled = "float4 PSMain() : SV_Target { return float4(1, 0.5, 0.25, 1); }";
constexpr const char *TrivialPSLibrary = "export float4 shade(float4 c) { return c * 0.5; }";

std::vector<uint8_t> compileText(const RT64::ShaderCompiler &compiler, const char *text, const wchar_t *entry,
                                 const wchar_t *profile) {
    IDxcBlob *blob = nullptr;
    compiler.compile(text, entry, profile, RenderShaderFormat::DXIL, &blob);
    if (!blob) throw std::runtime_error("compile produced no blob");
    auto result = bytes(blob);
    blob->Release();
    return result;
}

std::vector<uint8_t> linkTrivialPS(const RT64::ShaderCompiler &compiler) {
    IDxcBlob *libs[] = {nullptr, nullptr};
    compiler.compile(TrivialPSEntry, L"PSMain", L"lib_6_2", RenderShaderFormat::DXIL, &libs[0]);
    compiler.compile(TrivialPSLibrary, L"shade", L"lib_6_2", RenderShaderFormat::DXIL, &libs[1]);
    static const wchar_t *names[] = {L"entry", L"library"};
    IDxcBlob *linked = nullptr;
    compiler.link(L"PSMain", L"ps_6_2", libs, names, 2, &linked);
    if (!linked) throw std::runtime_error("trivial link produced no blob");
    auto result = bytes(linked);
    for (IDxcBlob *blob : {libs[0], libs[1], linked}) blob->Release();
    return result;
}

// Prints and clears the D3D12 debug-layer messages, when the layer is installed.
void dumpMessages(ID3D12Device *device) {
    ID3D12InfoQueue *queue = nullptr;
    if (FAILED(device->QueryInterface(IID_PPV_ARGS(&queue)))) return;
    for (UINT64 i = 0; i < queue->GetNumStoredMessages(); i++) {
        SIZE_T size = 0;
        queue->GetMessage(i, nullptr, &size);
        std::vector<char> storage(size);
        auto *message = reinterpret_cast<D3D12_MESSAGE *>(storage.data());
        if (SUCCEEDED(queue->GetMessage(i, message, &size))) std::printf("  d3d12: %s\n", message->pDescription);
    }
    queue->ClearStoredMessages();
    queue->Release();
}

HRESULT createPipeline(ID3D12Device *device, ID3D12RootSignature *root, const std::vector<uint8_t> &vs,
                       const std::vector<uint8_t> &ps) {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
    desc.pRootSignature = root;
    desc.VS = {vs.data(), vs.size()};
    desc.PS = {ps.data(), ps.size()};
    desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    desc.SampleMask = UINT_MAX;
    desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    desc.RasterizerState.DepthClipEnable = TRUE;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    ID3D12PipelineState *pipeline = nullptr;
    HRESULT result = device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipeline));
    if (pipeline) pipeline->Release();
    dumpMessages(device);
    return result;
}

struct TrivialShaders {
    std::vector<uint8_t> vs, psCompiled, psLinked;
};

// WARP on the CI runners supports shader model 6.2, so these use 6.2 profiles
// (RT64 itself uses lib_6_3 -> vs/ps_6_3); signing is the same code path.
TrivialShaders compileTrivial(const RT64::ShaderCompiler &compiler, const std::filesystem::path &out) {
    TrivialShaders shaders;
    shaders.vs = compileText(compiler, TrivialVS, L"VSMain", L"vs_6_2");
    shaders.psCompiled = compileText(compiler, TrivialPSCompiled, L"PSMain", L"ps_6_2");
    shaders.psLinked = linkTrivialPS(compiler);
    save(out, "warp-vs.dxil", shaders.vs);
    save(out, "warp-ps-compiled.dxil", shaders.psCompiled);
    save(out, "warp-ps-linked.dxil", shaders.psLinked);
    check(hasHash(shaders.vs) && hasHash(shaders.psCompiled) && hasHash(shaders.psLinked),
          "trivial compiled VS/PS and linked PS carry a hash");
    return shaders;
}

void reportValidator(const char *phase) {
    const std::wstring dxil = modulePath(L"dxil.dll");
    std::wprintf(L"dxil.dll loaded after %hs: %ls\n", phase, dxil.empty() ? L"no" : dxil.c_str());
}

// Creates pipelines on WARP: the D3D12 runtime verifies the DXIL hash
// ("signature") at shader creation. With debugLayer, the D3D12 SDK layers are
// enabled first and their messages printed (a later, separate device).
void runWarp(const TrivialShaders &shaders, bool debugLayer) {
    std::printf("--- D3D12 WARP, debug layer %s ---\n", debugLayer ? "on" : "off");
    if (debugLayer) {
        ID3D12Debug *debug = nullptr;
        if (FAILED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
            std::printf("D3D12 debug layer not installed (no message details)\n");
            return;
        }
        debug->EnableDebugLayer();
        debug->Release();
    }
    IDXGIFactory4 *factory = nullptr;
    IDXGIAdapter *warp = nullptr;
    ID3D12Device *device = nullptr;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) || FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp))) ||
        FAILED(D3D12CreateDevice(warp, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)))) {
        check(false, "create a D3D12 device on the WARP adapter");
        return;
    }
    D3D12_FEATURE_DATA_SHADER_MODEL model = {D3D_SHADER_MODEL_6_7};
    while (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &model, sizeof(model))) &&
           model.HighestShaderModel > D3D_SHADER_MODEL_5_1) {
        model.HighestShaderModel = D3D_SHADER_MODEL(model.HighestShaderModel - 1);
    }
    std::printf("WARP highest shader model: 0x%X\n", model.HighestShaderModel);
    D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
    ID3DBlob *serialized = nullptr;
    ID3D12RootSignature *root = nullptr;
    D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, nullptr);
    device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(&root));

    const char *suffix = debugLayer ? " (debug layer)" : "";
    HRESULT compiled = createPipeline(device, root, shaders.vs, shaders.psCompiled);
    std::printf("pipeline, compiled VS + compiled PS: 0x%08lX\n", compiled);
    check(SUCCEEDED(compiled), (std::string("D3D12 (WARP) accepts compiled shaders") + suffix).c_str());
    HRESULT linked = createPipeline(device, root, shaders.vs, shaders.psLinked);
    std::printf("pipeline, compiled VS + linked PS: 0x%08lX\n", linked);
    check(SUCCEEDED(linked), (std::string("D3D12 (WARP) accepts a run-time linked PS") + suffix).c_str());

    auto zeroed = shaders.psLinked;
    std::memset(zeroed.data() + 4, 0, 16);
    HRESULT unsigned_ = createPipeline(device, root, shaders.vs, zeroed);
    std::printf("pipeline, PS hash zeroed: 0x%08lX\n", unsigned_);
    check(FAILED(unsigned_), (std::string("control: D3D12 (WARP) rejects the PS without a hash") + suffix).c_str());
    auto corrupted = shaders.psLinked;
    corrupted[4] ^= 0xFF;
    HRESULT corrupt = createPipeline(device, root, shaders.vs, corrupted);
    std::printf("pipeline, PS hash corrupted: 0x%08lX\n", corrupt);
    check(FAILED(corrupt), (std::string("control: D3D12 (WARP) rejects the PS with a wrong hash") + suffix).c_str());

    root->Release();
    serialized->Release();
    device->Release();
    warp->Release();
    factory->Release();
}

} // namespace

int main(int argc, char **argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0); // keep output ordered in CI logs
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <out-dir> [--report-only] [--control-load]\n", argv[0]);
        return 2;
    }
    const std::filesystem::path out = argv[1];
    std::filesystem::create_directories(out);
    bool reportOnly = false;
    bool controlLoad = false;
    for (int i = 2; i < argc; i++) {
        reportOnly |= std::strcmp(argv[i], "--report-only") == 0;
        controlLoad |= std::strcmp(argv[i], "--control-load") == 0;
    }

    std::printf("VERS RasterVSLibrary (build time): %s\n",
                compilerVersion(RasterVSLibraryBlobDXIL, sizeof(RasterVSLibraryBlobDXIL)).c_str());
    std::printf("VERS RasterPSLibrary (build time): %s\n",
                compilerVersion(RasterPSLibraryBlobDXIL, sizeof(RasterPSLibraryBlobDXIL)).c_str());

    try {
        RT64::ShaderCompiler compiler;
        if (!compiler.dxcCompiler || !compiler.dxcUtils) throw std::runtime_error("DxcCreateInstance failed");
        std::wprintf(L"dxcompiler.dll loaded from: %ls\n", modulePath(L"dxcompiler.dll").c_str());

        int index = 0;
        for (bool msaa : {false, true}) {
            for (bool smooth : {false, true}) {
                RT64::ShaderDescription desc = {};
                desc.flags.smoothShade = smooth;
                std::vector<uint8_t> vs, ps;
                try {
                    compileAndLink(compiler, desc, msaa, vs, ps);
                } catch (const std::exception &error) {
                    std::printf("RT64 runtime compile+link (msaa=%d smooth=%d) threw: %s\n", msaa, smooth, error.what());
                    check(false, "RT64 runtime compile+link");
                    continue;
                }
                std::printf("VERS runtime VS: %s\n", compilerVersion(vs.data(), vs.size()).c_str());
                save(out, "rt64-vs-" + std::to_string(index) + ".dxil", vs);
                save(out, "rt64-ps-" + std::to_string(index) + ".dxil", ps);
                index++;
                check(hasHash(vs) && hasHash(ps), "RT64 runtime-linked VS/PS carry a hash");
            }
        }
        check(index == 4, "all RT64 runtime compile+link variants succeed");
        const TrivialShaders shaders = compileTrivial(compiler, out);

        // Everything DXC does in the engine is done by now: the compiler must
        // not have loaded the validator, even when one is on the search path.
        const std::wstring dxil = modulePath(L"dxil.dll");
        std::wprintf(L"dxil.dll loaded by the shader compiler: %ls\n", dxil.empty() ? L"no" : dxil.c_str());
        check(dxil.empty(), "dxcompiler.dll never loaded dxil.dll");

        // D3D12 then checks the hashes. Which OS components it loads is
        // reported, not gated: none of them ships with the game.
        runWarp(shaders, false);
        reportValidator("D3D12 on WARP");
        runWarp(shaders, true);
        reportValidator("D3D12 on WARP with the debug layer");
    } catch (const std::exception &error) {
        std::printf("exception: %s\n", error.what());
        check(false, "probe ran to completion");
    }

    if (controlLoad) {
        HMODULE control = LoadLibraryW(L"dxil.dll");
        std::wprintf(L"control: LoadLibraryW(dxil.dll) now: %ls\n",
                     control ? modulePath(L"dxil.dll").c_str() : L"not found on the search path");
    }
    std::printf("%d failure(s)%s\n", failures, reportOnly ? " (report only)" : "");
    return (failures && !reportOnly) ? 1 : 0;
}
