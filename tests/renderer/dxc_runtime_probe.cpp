// ROM-free probe of RT64's run-time DXIL path on Windows.
//
// RT64 (D3D12) compiles generated shader text as lib_6_3 and links it with the
// library shaders embedded at build time (RT64::RasterShader). This probe does
// exactly that with RT64's own ShaderCompiler and generateShaderText, writes
// the linked containers for tests/release/dxil_hash.py, and reports:
//   - whether dxil.dll (the DXC validator/signer) got loaded, and from where;
//   - whether the containers carry a hash (D3D12 rejects unsigned DXIL);
//   - whether D3D12 on the WARP adapter accepts shaders produced this way and
//     rejects the same shaders with a zeroed or corrupted hash (control).
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
    compiler.compile(TrivialPSEntry, L"PSMain", L"lib_6_3", RenderShaderFormat::DXIL, &libs[0]);
    compiler.compile(TrivialPSLibrary, L"shade", L"lib_6_3", RenderShaderFormat::DXIL, &libs[1]);
    static const wchar_t *names[] = {L"entry", L"library"};
    IDxcBlob *linked = nullptr;
    compiler.link(L"PSMain", L"ps_6_3", libs, names, 2, &linked);
    if (!linked) throw std::runtime_error("trivial link produced no blob");
    auto result = bytes(linked);
    for (IDxcBlob *blob : {libs[0], libs[1], linked}) blob->Release();
    return result;
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
    return result;
}

void warpChecks(const RT64::ShaderCompiler &compiler, const std::filesystem::path &out) {
    IDXGIFactory4 *factory = nullptr;
    IDXGIAdapter *warp = nullptr;
    ID3D12Device *device = nullptr;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) || FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp))) ||
        FAILED(D3D12CreateDevice(warp, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)))) {
        check(false, "create a D3D12 device on the WARP adapter");
        return;
    }
    D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
    ID3DBlob *serialized = nullptr;
    ID3D12RootSignature *root = nullptr;
    D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, nullptr);
    device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(&root));

    const auto vs = compileText(compiler, TrivialVS, L"VSMain", L"vs_6_3");
    const auto ps = linkTrivialPS(compiler);
    save(out, "warp-vs.dxil", vs);
    save(out, "warp-ps-linked.dxil", ps);
    check(hasHash(vs) && hasHash(ps), "trivial compiled VS and linked PS carry a hash");
    HRESULT accepted = createPipeline(device, root, vs, ps);
    std::printf("D3D12 WARP pipeline, compiled VS + linked PS: 0x%08lX\n", accepted);
    check(SUCCEEDED(accepted), "D3D12 (WARP) accepts the pipeline");

    auto zeroed = ps;
    std::memset(zeroed.data() + 4, 0, 16);
    HRESULT unsigned_ = createPipeline(device, root, vs, zeroed);
    std::printf("D3D12 WARP pipeline, PS hash zeroed: 0x%08lX\n", unsigned_);
    check(FAILED(unsigned_), "control: D3D12 (WARP) rejects the same PS without a hash");
    auto corrupted = ps;
    corrupted[4] ^= 0xFF;
    HRESULT corrupt = createPipeline(device, root, vs, corrupted);
    std::printf("D3D12 WARP pipeline, PS hash corrupted: 0x%08lX\n", corrupt);
    check(FAILED(corrupt), "control: D3D12 (WARP) rejects the same PS with a wrong hash");

    root->Release();
    serialized->Release();
    device->Release();
    warp->Release();
    factory->Release();
}

} // namespace

int main(int argc, char **argv) {
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
        warpChecks(compiler, out);
    } catch (const std::exception &error) {
        std::printf("exception: %s\n", error.what());
        check(false, "probe ran to completion");
    }

    const std::wstring dxil = modulePath(L"dxil.dll");
    std::wprintf(L"dxil.dll loaded during the run: %ls\n", dxil.empty() ? L"no" : dxil.c_str());
    check(dxil.empty(), "dxil.dll was never loaded");

    if (controlLoad) {
        HMODULE control = LoadLibraryW(L"dxil.dll");
        std::wprintf(L"control: LoadLibraryW(dxil.dll) now: %ls\n",
                     control ? modulePath(L"dxil.dll").c_str() : L"not found on the search path");
    }
    std::printf("%d failure(s)%s\n", failures, reportOnly ? " (report only)" : "");
    return (failures && !reportOnly) ? 1 : 0;
}
