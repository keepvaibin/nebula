// Material shader equivalence: renders seeded random pixel keys with seeded random constants, textures, lights and
// interpolants on WARP, and writes every target's bytes per key to argv[1]. Built against an older tree with
// OLD_GENERATOR (its ShaderGenerator::generate_ps per key) and against this tree (the material shader, its source read
// from argv[2], the key's program packed into the constants), equal outputs prove the two pixel stages draw the same.
// run.ps1 builds both and compares; compare.py reports the differing keys, pixels and their largest difference.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include "galaxy/render/draw_state.h"
#include "galaxy/render/shader_keys.h"
#include "galaxy/render/uber_constants.h"
#if defined(OLD_GENERATOR)
#include "galaxy/render/shader_gen.h"
#else
#include "galaxy/render/material_shaders.h"
#endif

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

using Microsoft::WRL::ComPtr;
using namespace galaxy::render;

namespace {

constexpr unsigned kSize = 64u;
constexpr unsigned kTexSize = 32u;
constexpr unsigned kKeys = 4000u;

void check(HRESULT hr, const char* what) {
    if (FAILED(hr)) {
        std::fprintf(stderr, "FAILED %s: 0x%08lx\n", what, static_cast<unsigned long>(hr));
        std::exit(4);
    }
}

ComPtr<ID3DBlob> compile(const std::string& src, const char* target) {
    ComPtr<ID3DBlob> code, errors;
    const HRESULT hr = D3DCompile(src.data(), src.size(), "harness", nullptr, nullptr, "main", target,
                                  D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_PACK_MATRIX_ROW_MAJOR |
                                      D3DCOMPILE_OPTIMIZATION_LEVEL1,
                                  0, &code, &errors);
    if (FAILED(hr)) {
        std::fprintf(stderr, "compile failed: %s\n", errors ? static_cast<const char*>(errors->GetBufferPointer()) : "");
        return nullptr;
    }
    return code;
}

// The test vertex stage: an 8 x 8 grid of quads, every attribute a hash of its grid point (shared vertices agree).
std::string vertex_shader(bool light_inputs, bool motion) {
    std::string s = R"HLSL(
float h(uint x) { x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16; return float(x & 0xFFFFFFu) / 16777216.0; }
struct VSOut {
    float4 pos : SV_Position;
    float4 col0 : COLOR0;
    float4 col1 : COLOR1;
    float3 uv[8] : TEXCOORD;
)HLSL";
    if (light_inputs) {
        s += "    float3 ppl_pos : TEXCOORD8;\n    float3 ppl_nrm : TEXCOORD9;\n"
             "    float4 ppl_vcol0 : TEXCOORD10;\n    float4 ppl_vcol1 : TEXCOORD11;\n";
    }
    if (motion) {
        s += "    float4 mv_cur : TEXCOORD12;\n    float4 mv_prev : TEXCOORD13;\n";
    }
    s += R"HLSL(};
VSOut main(uint id : SV_VertexID) {
    const uint quad = id / 6u, corner = id % 6u;
    const uint2 c = uint2(corner == 1u || corner == 4u || corner == 5u ? 1u : 0u, corner == 2u || corner == 3u || corner == 5u ? 1u : 0u);
    const uint2 g = uint2(quad % 8u, quad / 8u) + c;
    const uint seed = (g.y * 9u + g.x) * 64u + 0x9E37u;
    VSOut o = (VSOut)0;
    o.pos = float4(float2(g) / 4.0 - 1.0, 0.05 + 0.9 * h(seed), 1.0);
    o.col0 = float4(h(seed + 1u), h(seed + 2u), h(seed + 3u), h(seed + 4u)) * 1.1;
    o.col1 = float4(h(seed + 5u), h(seed + 6u), h(seed + 7u), h(seed + 8u)) * 1.1;
    [unroll] for (uint i = 0u; i < 8u; ++i) {
        o.uv[i] = float3(h(seed + 9u + 3u * i) * 4.0 - 1.5, h(seed + 10u + 3u * i) * 4.0 - 1.5, 0.5 + h(seed + 11u + 3u * i));
    }
)HLSL";
    if (light_inputs) {
        s += "    o.ppl_pos = float3(h(seed + 40u) * 20.0 - 10.0, h(seed + 41u) * 20.0 - 10.0, -1.0 - h(seed + 42u) * 20.0);\n"
             "    o.ppl_nrm = float3(h(seed + 43u), h(seed + 44u), h(seed + 45u)) * 2.0 - 1.0;\n"
             "    o.ppl_vcol0 = float4(h(seed + 46u), h(seed + 47u), h(seed + 48u), h(seed + 49u));\n"
             "    o.ppl_vcol1 = float4(h(seed + 50u), h(seed + 51u), h(seed + 52u), h(seed + 53u));\n";
    }
    if (motion) {
        s += "    o.mv_cur = float4(h(seed + 54u) * 2.0 - 1.0, h(seed + 55u) * 2.0 - 1.0, 32.0, 0.5 + h(seed + 56u));\n"
             "    o.mv_prev = float4(h(seed + 57u) * 2.0 - 1.0, h(seed + 58u) * 2.0 - 1.0, 32.0, 0.5 + h(seed + 59u));\n";
    }
    s += "    return o;\n}\n";
    return s;
}

struct Device {
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> alloc;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence> fence;
    UINT64 fence_value = 0;
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12DescriptorHeap> srv_heap, sampler_heap, rtv_heap, dsv_heap;
    ComPtr<ID3D12Resource> textures[8], targets[4], depth, readback, upload, light_table, light_rows;
    UINT srv_size = 0, rtv_size = 0;

    void wait() {
        check(queue->Signal(fence.Get(), ++fence_value), "signal");
        HANDLE e = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        check(fence->SetEventOnCompletion(fence_value, e), "event");
        WaitForSingleObject(e, INFINITE);
        CloseHandle(e);
    }
};

// A value that outlives the call it is passed to (the D3D12 calls take pointers to descriptions).
template <class T>
const T* keep(const T& v) {
    static T ring[16];
    static unsigned at = 0;
    T* slot = &ring[at++ % 16u];
    *slot = v;
    return slot;
}

D3D12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE t) {
    D3D12_HEAP_PROPERTIES p{};
    p.Type = t;
    return p;
}
D3D12_RESOURCE_DESC buffer(UINT64 size) {
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    d.Width = size;
    d.Height = 1;
    d.DepthOrArraySize = 1;
    d.MipLevels = 1;
    d.SampleDesc.Count = 1;
    d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return d;
}
D3D12_RESOURCE_DESC texture(DXGI_FORMAT f, UINT w, UINT h, D3D12_RESOURCE_FLAGS flags) {
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    d.Width = w;
    d.Height = h;
    d.DepthOrArraySize = 1;
    d.MipLevels = 1;
    d.Format = f;
    d.SampleDesc.Count = 1;
    d.Flags = flags;
    return d;
}

// The targets of a pixel variant: colour, then motion (RG32F), reactive (R32F), layer and transmittance.
constexpr DXGI_FORMAT kTargetFormats[4] = {DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R32G32B32A32_FLOAT,
                                           DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM};

void barrier(ID3D12GraphicsCommandList* list, ID3D12Resource* r, D3D12_RESOURCE_STATES a, D3D12_RESOURCE_STATES b) {
    D3D12_RESOURCE_BARRIER br{};
    br.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    br.Transition.pResource = r;
    br.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    br.Transition.StateBefore = a;
    br.Transition.StateAfter = b;
    list->ResourceBarrier(1, &br);
}

Device make_device(std::mt19937& rng) {
    Device d;
    ComPtr<IDXGIFactory4> factory;
    check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "factory");
    ComPtr<IDXGIAdapter> warp;
    check(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)), "warp");
    check(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&d.device)), "device");
    D3D12_COMMAND_QUEUE_DESC qd{};
    check(d.device->CreateCommandQueue(&qd, IID_PPV_ARGS(&d.queue)), "queue");
    check(d.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&d.alloc)), "alloc");
    check(d.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, d.alloc.Get(), nullptr, IID_PPV_ARGS(&d.list)),
          "list");
    check(d.list->Close(), "close");
    check(d.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&d.fence)), "fence");

    // Root: b0 (vertex/lighting constants), b1 (pixel constants), t0-t7 (table), t0 space1, t3 space1, samplers s0-s7.
    D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 8, 0, 0, 0};
    D3D12_DESCRIPTOR_RANGE sampler_range{D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 8, 0, 0, 0};
    D3D12_ROOT_PARAMETER params[6]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].Descriptor = {0, 0};
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[1].Descriptor = {1, 0};
    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[2].DescriptorTable = {1, &range};
    params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[3].Descriptor = {0, 1};
    params[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[4].Descriptor = {3, 1};
    params[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[5].DescriptorTable = {1, &sampler_range};
    D3D12_STATIC_SAMPLER_DESC samplers[8]{};
    for (unsigned i = 0; i < 8; ++i) {
        samplers[i].Filter = i % 2 == 0 ? D3D12_FILTER_MIN_MAG_MIP_LINEAR : D3D12_FILTER_MIN_MAG_MIP_POINT;
        samplers[i].AddressU = i % 3 == 0 ? D3D12_TEXTURE_ADDRESS_MODE_CLAMP : D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samplers[i].AddressV = i % 3 == 1 ? D3D12_TEXTURE_ADDRESS_MODE_MIRROR : D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samplers[i].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samplers[i].MaxLOD = D3D12_FLOAT32_MAX;
        samplers[i].ShaderRegister = i;
        samplers[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
    D3D12_ROOT_SIGNATURE_DESC rd{6, params, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ComPtr<ID3DBlob> blob, err;
    check(D3D12SerializeRootSignature(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err), "root serialize");
    check(d.device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&d.root)),
          "root");

    D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 8, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
    check(d.device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&d.srv_heap)), "srv heap");
    hd = {D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 8, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
    check(d.device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&d.sampler_heap)), "sampler heap");
    for (unsigned i = 0; i < 8; ++i) {
        D3D12_SAMPLER_DESC sd{};
        sd.Filter = samplers[i].Filter;
        sd.AddressU = samplers[i].AddressU;
        sd.AddressV = samplers[i].AddressV;
        sd.AddressW = samplers[i].AddressW;
        sd.MaxLOD = D3D12_FLOAT32_MAX;
        D3D12_CPU_DESCRIPTOR_HANDLE h = d.sampler_heap->GetCPUDescriptorHandleForHeapStart();
        h.ptr += i * d.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
        d.device->CreateSampler(&sd, h);
    }
    hd = {D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 4, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0};
    check(d.device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&d.rtv_heap)), "rtv heap");
    hd = {D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0};
    check(d.device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&d.dsv_heap)), "dsv heap");
    d.srv_size = d.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    d.rtv_size = d.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    const auto up = heap(D3D12_HEAP_TYPE_UPLOAD);
    const auto def = heap(D3D12_HEAP_TYPE_DEFAULT);
    const auto rb = heap(D3D12_HEAP_TYPE_READBACK);
    check(d.device->CreateCommittedResource(&up, D3D12_HEAP_FLAG_NONE, keep(buffer(1u << 20)), D3D12_RESOURCE_STATE_GENERIC_READ,
                                            nullptr, IID_PPV_ARGS(&d.upload)), "upload");
    check(d.device->CreateCommittedResource(&rb, D3D12_HEAP_FLAG_NONE, keep(buffer(5u * kSize * kSize * 16u)),
                                            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&d.readback)), "readback");
    // Textures: random RGBA8, uploaded once.
    std::uint8_t* mapped = nullptr;
    check(d.upload->Map(0, nullptr, reinterpret_cast<void**>(&mapped)), "map");
    for (unsigned i = 0; i < 8; ++i) {
        check(d.device->CreateCommittedResource(&def, D3D12_HEAP_FLAG_NONE,
                                                keep(texture(DXGI_FORMAT_R8G8B8A8_UNORM, kTexSize, kTexSize, D3D12_RESOURCE_FLAG_NONE)),
                                                D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&d.textures[i])), "tex");
        for (unsigned b = 0; b < kTexSize * kTexSize * 4u; ++b) {
            // Mostly smooth values with some exact 0 and 255 bytes.
            const unsigned v = rng() % 280u;
            mapped[i * kTexSize * kTexSize * 4u + b] = static_cast<std::uint8_t>(v >= 255u ? (v >= 268u ? 0u : 255u) : v);
        }
    }
    d.upload->Unmap(0, nullptr);
    check(d.alloc->Reset(), "reset");
    check(d.list->Reset(d.alloc.Get(), nullptr), "list reset");
    for (unsigned i = 0; i < 8; ++i) {
        D3D12_TEXTURE_COPY_LOCATION dst{d.textures[i].Get(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, {}};
        D3D12_TEXTURE_COPY_LOCATION src{d.upload.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, {}};
        src.PlacedFootprint.Offset = i * kTexSize * kTexSize * 4u;
        src.PlacedFootprint.Footprint = {DXGI_FORMAT_R8G8B8A8_UNORM, kTexSize, kTexSize, 1, kTexSize * 4u};
        d.list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        barrier(d.list.Get(), d.textures[i].Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        D3D12_CPU_DESCRIPTOR_HANDLE h = d.srv_heap->GetCPUDescriptorHandleForHeapStart();
        h.ptr += i * d.srv_size;
        d.device->CreateShaderResourceView(d.textures[i].Get(), nullptr, h);
    }
    check(d.list->Close(), "close");
    ID3D12CommandList* lists[] = {d.list.Get()};
    d.queue->ExecuteCommandLists(1, lists);
    d.wait();

    for (unsigned t = 0; t < 4; ++t) {
        D3D12_CLEAR_VALUE cv{kTargetFormats[t], {0.25f, 0.5f, 0.75f, 1.0f}};
        check(d.device->CreateCommittedResource(&def, D3D12_HEAP_FLAG_NONE,
                                                keep(texture(kTargetFormats[t], kSize, kSize, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET)),
                                                D3D12_RESOURCE_STATE_RENDER_TARGET, &cv, IID_PPV_ARGS(&d.targets[t])), "target");
        D3D12_CPU_DESCRIPTOR_HANDLE h = d.rtv_heap->GetCPUDescriptorHandleForHeapStart();
        h.ptr += t * d.rtv_size;
        d.device->CreateRenderTargetView(d.targets[t].Get(), nullptr, h);
    }
    D3D12_CLEAR_VALUE dv{DXGI_FORMAT_D32_FLOAT, {}};
    dv.DepthStencil.Depth = 0.0f;
    check(d.device->CreateCommittedResource(&def, D3D12_HEAP_FLAG_NONE,
                                            keep(texture(DXGI_FORMAT_D32_FLOAT, kSize, kSize, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)),
                                            D3D12_RESOURCE_STATE_DEPTH_WRITE, &dv, IID_PPV_ARGS(&d.depth)), "depth");
    d.device->CreateDepthStencilView(d.depth.Get(), nullptr, d.dsv_heap->GetCPUDescriptorHandleForHeapStart());

    // Lights: the row table maps row r to row r; rows hold random light terms, colour words random.
    std::vector<std::uint32_t> table(64u * 4u);
    for (unsigned r = 0; r < table.size(); ++r) table[r] = r;
    std::vector<float> rows(256u * 4u);
    std::uniform_real_distribution<float> u(-1.0f, 1.0f);
    for (unsigned r = 0; r < 256u; ++r) {
        for (unsigned c = 0; c < 4u; ++c) {
            float v = u(rng) * (r % 4u == 2u || r % 4u == 3u ? 8.0f : 1.0f);
            if (r % 4u == 0u && c == 3u) {
                const std::uint32_t w = rng();
                std::memcpy(&v, &w, 4);
            }
            rows[r * 4u + c] = v;
        }
    }
    const auto make_buffer = [&](ComPtr<ID3D12Resource>& out, const void* data, std::size_t size) {
        check(d.device->CreateCommittedResource(&up, D3D12_HEAP_FLAG_NONE, keep(buffer(size)), D3D12_RESOURCE_STATE_GENERIC_READ,
                                                nullptr, IID_PPV_ARGS(&out)), "buffer");
        void* m = nullptr;
        check(out->Map(0, nullptr, &m), "map buffer");
        std::memcpy(m, data, size);
        out->Unmap(0, nullptr);
    };
    make_buffer(d.light_table, table.data(), table.size() * 4u);
    make_buffer(d.light_rows, rows.data(), rows.size() * 4u);
    return d;
}

// A random drawing state's pixel key (as the shader equivalence harness draws them), with the backend's bits.
PixelShaderKey random_key(std::mt19937& rng) {
    const auto r = [&](unsigned n) { return static_cast<unsigned>(rng() % n); };
    const auto b = [&]() { return r(2) != 0u; };
    NativeDrawState s;
    s.gen.num_texgens = static_cast<std::uint8_t>(r(9));
    s.gen.num_color_chans = static_cast<std::uint8_t>(r(3));
    s.gen.num_tev_stages = static_cast<std::uint8_t>(1u + r(r(2) ? 4u : 16u));
    s.gen.num_ind_stages = static_cast<std::uint8_t>(r(3) == 0 ? r(5) : 0u);
    s.z = ZMode{b(), static_cast<CompareFunc>(r(8)), b()};
    s.blend = BlendMode{b(), r(4) == 0, b(), b(), b(), static_cast<BlendFactor>(r(8)), static_cast<BlendFactor>(r(8)),
                        r(4) == 0, static_cast<std::uint8_t>(r(16)), b(), static_cast<std::uint8_t>(r(256))};
    s.pe = PeControl{static_cast<std::uint8_t>(r(3)), static_cast<std::uint8_t>(r(2)), b()};
    s.alpha = AlphaCompare{static_cast<std::uint8_t>(r(256)), static_cast<std::uint8_t>(r(256)),
                           static_cast<CompareFunc>(r(4) == 0 ? r(8) : 7u), static_cast<CompareFunc>(r(4) == 0 ? r(8) : 7u),
                           static_cast<AlphaTestLogic>(r(4))};
    for (auto& t : s.tev) {
        t.color_a = static_cast<TevColorArg>(r(16));
        t.color_b = static_cast<TevColorArg>(r(16));
        t.color_c = static_cast<TevColorArg>(r(16));
        t.color_d = static_cast<TevColorArg>(r(16));
        t.color_bias = static_cast<TevBias>(r(4));
        t.color_sub = b();
        t.color_clamp = b();
        t.color_scale = static_cast<TevScale>(r(4));
        t.color_dest = static_cast<std::uint8_t>(r(4));
        t.alpha_a = static_cast<TevAlphaArg>(r(8));
        t.alpha_b = static_cast<TevAlphaArg>(r(8));
        t.alpha_c = static_cast<TevAlphaArg>(r(8));
        t.alpha_d = static_cast<TevAlphaArg>(r(8));
        t.alpha_bias = static_cast<TevBias>(r(4));
        t.alpha_sub = b();
        t.alpha_clamp = b();
        t.alpha_scale = static_cast<TevScale>(r(4));
        t.alpha_dest = static_cast<std::uint8_t>(r(4));
        t.ras_swap = static_cast<std::uint8_t>(r(4));
        t.tex_swap = static_cast<std::uint8_t>(r(4));
        t.order = TevOrder{static_cast<std::uint8_t>(r(8)), static_cast<std::uint8_t>(r(8)), b(),
                           static_cast<std::uint8_t>(r(8))};
        t.kcsel = static_cast<std::uint8_t>(r(32));
        t.kasel = static_cast<std::uint8_t>(r(32));
    }
    for (auto& row : s.swap) {
        for (auto& c : row) c = static_cast<std::uint8_t>(r(4));
    }
    s.fog_type = static_cast<FogType>(r(8));
    s.z_texture.op = static_cast<std::uint8_t>(r(5) == 0 ? r(3) : 0u);
    s.z_texture.type = static_cast<std::uint8_t>(r(3));
    for (auto& ind : s.ind) {
        ind = IndirectStage{};
        if (r(3) == 0) {
            ind.stage = static_cast<std::uint8_t>(r(4));
            ind.format = static_cast<std::uint8_t>(r(4));
            ind.bias = static_cast<std::uint8_t>(r(8));
            ind.alpha = static_cast<std::uint8_t>(r(4));
            ind.matrix = static_cast<std::uint8_t>(r(16));
            ind.wrap_s = static_cast<std::uint8_t>(r(8));
            ind.wrap_t = static_cast<std::uint8_t>(r(8));
            ind.lod = b();
            ind.prev = b();
        }
    }
    for (auto& o : s.ind_order) {
        o.map = static_cast<std::uint8_t>(r(8));
        o.coord = static_cast<std::uint8_t>(r(8));
        o.scale_s = static_cast<std::uint8_t>(r(10));
        o.scale_t = static_cast<std::uint8_t>(r(10));
    }
    PixelShaderKey k = build_pixel_shader_key(s, 1u, true);  // true colour, as the renderer builds every key
    // The backend's bits: per-pixel and enhanced lighting, ray tracing's paths, motion/reactive/layer, finite
    // attenuation, glow/lit translucent outputs.
    std::uint8_t reserved = 0u;
    if (r(4) == 0) reserved |= kPsKeyPerPixelLighting;
    if (r(4) == 0) reserved |= kPsKeyEnhancedLighting;
    if (b()) reserved |= kPsKeyPplFiniteLightAttn;
    if (r(3) == 0) reserved |= kPsKeyRtPaths;
    const unsigned out = r(6);
    if (out == 1u) reserved |= kPsKeyMotion;
    if (out == 2u) reserved |= kPsKeyReactive;
    if (out == 3u) reserved |= kPsKeyReactive | kPsKeyLayer;
    k.reserved = reserved;
    if ((reserved & kPsKeyEnhancedLighting) != 0u) {
        k.ind_reserved = static_cast<std::uint32_t>(rng()) & 0x7FFFFFu;
    }
    if (r(4) == 0) k.output_flags = static_cast<std::uint8_t>(k.output_flags | (r(8) << 5u));
    return k;
}

PixelConstants random_constants(std::mt19937& rng, const PixelShaderKey& key) {
    std::uniform_real_distribution<float> u01(0.0f, 1.0f);
    PixelConstants c{};
    for (auto& reg : c.tev_regs) {
        for (auto& v : reg) v = (rng() % 8u == 0u) ? (static_cast<float>(static_cast<int>(rng() % 2048u) - 1024)) / 255.0f
                                                   : static_cast<float>(rng() % 256u) / 255.0f;
    }
    for (auto& k : c.konst) {
        for (auto& v : k) v = static_cast<float>(rng() % 256u) / 255.0f;
    }
    c.alpha_refs[0] = static_cast<float>(rng() % 256u) / 255.0f;
    c.alpha_refs[1] = static_cast<float>(rng() % 256u) / 255.0f;
    std::uint32_t tint = (rng() % 256u) << 8u;
    std::memcpy(&c.alpha_refs[2], &tint, 4);
    c.alpha_refs[3] = u01(rng);
    c.fog_params[0] = u01(rng) * 2.0f;
    c.fog_params[1] = static_cast<float>(1u + rng() % (1u << 24));
    c.fog_params[2] = static_cast<float>(rng() % 12u);
    c.fog_params[3] = u01(rng);
    c.fog_params[4] = u01(rng);
    c.fog_params[5] = u01(rng);
    c.fog_params[6] = u01(rng);
    c.fog_params[7] = rng() % 2u == 0u ? 0.0f : 1.0f;
    for (auto& t : c.tex_dims) {
        t[0] = static_cast<float>(kTexSize);
        t[1] = static_cast<float>(kTexSize);
        t[2] = static_cast<float>(1u + rng() % 4u) * 8.0f;
        t[3] = static_cast<float>(1u + rng() % 4u) * 8.0f;
    }
    c.tex_dims[7][3] = rng() % 3u == 0u ? 1.0f + 0.4f * u01(rng) : 2.0f * u01(rng);
    c.ztex_params[0] = u01(rng) * 0.01f;
    c.ztex_params[1] = 1.0f;
    c.ztex_params[2] = 1.0f + u01(rng) * 4.0f;
    const unsigned w = rng() % 4u;
    c.ztex_params[3] = w == 0u ? 0.5f : w == 1u ? -0.3f : w == 2u ? -1.0f - static_cast<float>(rng() % 0xFFFFFFu) : 0.0f;
    for (auto& m : c.ind_mtx) {
        for (unsigned i = 0; i < 3; ++i) m[i] = static_cast<int>(rng() % 2048u) - 1024;
        m[3] = static_cast<int>(rng() % 24u) - 6;
    }
    c.uber_ps_header[3] = (rng() % 2u == 0u ? 0u : (static_cast<std::uint32_t>(rng()) & 0xFFFF0000u)) |
                          (rng() % 4u == 0u ? 32u : 0u);
    (void)key;
    return c;
}

// The light rows both forms read: the row table's identity mapping (rows 0xA8 + 4n + w) in trees with row tables, the
// light block at the same rows in trees with per-kind blocks.
template <class T>
void set_light_rows(T& v) {
    if constexpr (requires { v.row_bases; }) {
        v.row_bases[3] = 0xA8u;
    }
    if constexpr (requires { v.mtx_palette_base; }) {
        v.mtx_palette_base = 0u;
    }
}

VertexConstants random_vertex_constants(std::mt19937& rng) {
    VertexConstants v{};
    for (auto& a : v.chan_ambient) for (auto& x : a) x = static_cast<float>(rng() % 256u) / 255.0f;
    for (auto& m : v.chan_material) for (auto& x : m) x = static_cast<float>(rng() % 256u) / 255.0f;
    v.num_chans = rng() % 3u;
    v.flags = static_cast<std::uint32_t>(rng()) & 0xFFFFFF90u;
    set_light_rows(v);
    v.sky_up = static_cast<std::uint32_t>(rng());
    v.sky_up_known = rng() % 2u;
    for (auto& c : v.uber_channel) {
        KeyChannel ch{};
        ch.material_vertex = static_cast<std::uint8_t>(rng() % 2u);
        ch.lighting = static_cast<std::uint8_t>(rng() % 2u);
        ch.light_mask = static_cast<std::uint8_t>(rng());
        ch.ambient_vertex = static_cast<std::uint8_t>(rng() % 2u);
        ch.diffuse = static_cast<std::uint8_t>(rng() % 3u);
        ch.attenuation = static_cast<std::uint8_t>(rng() % 4u);
        c = channel_bits(ch);
    }
    return v;
}

#if !defined(OLD_GENERATOR)
std::string g_material_source;
#endif

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    std::FILE* out = std::fopen(argv[1], "wb");
    if (out == nullptr) return 3;
#if !defined(OLD_GENERATOR)
    if (argc < 3) return 2;
    {
        std::ifstream in(argv[2], std::ios::binary);
        std::stringstream ss;
        ss << in.rdbuf();
        g_material_source = ss.str();
    }
#else
    const ShaderGenerator gen;
#endif
    std::mt19937 rng(20261030u);
    if (std::getenv("HARNESS_DEBUG") != nullptr) {
        ComPtr<ID3D12Debug> debug;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) debug->EnableDebugLayer();
    }
    Device d = make_device(rng);
    std::map<std::pair<bool, bool>, ComPtr<ID3DBlob>> vertex_shaders;
    std::map<unsigned, ComPtr<ID3DBlob>> pixel_variants;
    ComPtr<ID3D12Resource> constants;
    check(d.device->CreateCommittedResource(keep(heap(D3D12_HEAP_TYPE_UPLOAD)), D3D12_HEAP_FLAG_NONE, keep(buffer(4096)),
                                            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&constants)),
          "constants");
    for (unsigned n = 0; n < kKeys; ++n) {
        const PixelShaderKey key = random_key(rng);
        PixelConstants pc = random_constants(rng, key);
        const VertexConstants vc = random_vertex_constants(rng);
        // HARNESS_ONLY=n: only key n is drawn (the others' random values are still drawn, so key n is the same).
        if (const char* only = std::getenv("HARNESS_ONLY"); only != nullptr && std::strtoul(only, nullptr, 10) != n) {
            continue;
        }
        const bool motion = (key.reserved & kPsKeyMotion) != 0u;
        const bool reactive = !motion && (key.reserved & kPsKeyReactive) != 0u;
        const bool layer = reactive && (key.reserved & kPsKeyLayer) != 0u;
        const bool light = (key.reserved & (kPsKeyPerPixelLighting | kPsKeyEnhancedLighting)) != 0u;
        const bool export_depth = (key.flags & 0x02u) != 0u && (key.flags & 0x01u) != 0u;
        const bool dual = !motion && !reactive && (key.flags & 0x80u) != 0u;
        ComPtr<ID3DBlob>& vs = vertex_shaders[{light, motion}];
        if (!vs) vs = compile(vertex_shader(light, motion), "vs_5_1");
        ComPtr<ID3DBlob> ps;
#if defined(OLD_GENERATOR)
        ps = compile(gen.generate_ps(key), "ps_5_1");
        // HARNESS_DUMP=n: key n's generated source beside the output.
        if (const char* dump = std::getenv("HARNESS_DUMP"); dump != nullptr && std::strtoul(dump, nullptr, 10) == n) {
            std::ofstream(std::string(argv[1]) + ".key.hlsl", std::ios::binary) << gen.generate_ps(key);
        }
#else
        const unsigned variant = material_ps_variant(key);
        ComPtr<ID3DBlob>& cached = pixel_variants[variant];
        if (!cached) {
            const MaterialPsVariant v = material_ps_variant_form(variant);
            std::string src;
            const auto def = [&](const char* name, bool on) { src += std::string("#define ") + name + (on ? " 1\n" : " 0\n"); };
            def("MAT_PPL", v.ppl); def("MAT_ENH", v.enhanced); def("MAT_MOTION", v.motion); def("MAT_REACTIVE", v.reactive);
            def("MAT_LAYER", v.layer); def("MAT_DEPTH", v.depth); def("MAT_DUAL", v.dual); def("MAT_EARLY", v.early);
            cached = compile(src + g_material_source, "ps_5_1");
        }
        ps = cached;
        const std::uint32_t light_word = pc.uber_ps_header[3];
        pack_material_program(key, pc);
        pc.uber_ps_header[3] = light_word;
#endif
        if (!vs || !ps) {
            std::fputc('X', out);
            continue;
        }
        const unsigned target_count = motion ? 2u : layer ? 4u : reactive ? 2u : 1u;
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
        pd.pRootSignature = d.root.Get();
        pd.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
        pd.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
        pd.BlendState.IndependentBlendEnable = FALSE;
        for (auto& rt : pd.BlendState.RenderTarget) {
            rt.SrcBlend = rt.SrcBlendAlpha = D3D12_BLEND_ONE;
            rt.DestBlend = rt.DestBlendAlpha = D3D12_BLEND_ZERO;
            rt.BlendOp = rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
            rt.LogicOp = D3D12_LOGIC_OP_NOOP;
            rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        }
        if (dual) {
            // Dual-source: the second colour's alpha as the destination factor shows it in the picture.
            D3D12_RENDER_TARGET_BLEND_DESC& rt = pd.BlendState.RenderTarget[0];
            rt.BlendEnable = TRUE;
            rt.SrcBlend = D3D12_BLEND_SRC_ALPHA;
            rt.DestBlend = D3D12_BLEND_SRC1_ALPHA;
            rt.SrcBlendAlpha = D3D12_BLEND_ONE;
            rt.DestBlendAlpha = D3D12_BLEND_SRC1_ALPHA;
        }
        pd.SampleMask = 0xFFFFFFFFu;
        pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        pd.RasterizerState.DepthClipEnable = TRUE;
        pd.DepthStencilState.DepthEnable = TRUE;
        pd.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        pd.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pd.NumRenderTargets = target_count;
        for (unsigned t = 0; t < target_count; ++t) {
            pd.RTVFormats[t] = motion && t == 1u ? DXGI_FORMAT_R32G32B32A32_FLOAT
                               : reactive && t == 1u ? DXGI_FORMAT_R32G32B32A32_FLOAT
                                                    : DXGI_FORMAT_R8G8B8A8_UNORM;
        }
        pd.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        pd.SampleDesc.Count = 1;
        ComPtr<ID3D12PipelineState> pso;
        if (const HRESULT hr = d.device->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&pso)); FAILED(hr)) {
            static int logged = 0;
            if (logged++ < 3) {
                std::fprintf(stderr, "key %u: pipeline refused 0x%08lx (light %d motion %d reactive %d layer %d depth %d dual %d)\n",
                             n, static_cast<unsigned long>(hr), light, motion, reactive, layer, export_depth, dual);
                // Which part refuses: the same pipeline with a minimal pixel shader.
                const auto simple = compile("float4 main(float4 p : SV_Position) : SV_Target0 { return p; }", "ps_5_1");
                D3D12_GRAPHICS_PIPELINE_STATE_DESC probe = pd;
                probe.PS = {simple->GetBufferPointer(), simple->GetBufferSize()};
                ComPtr<ID3D12PipelineState> p2;
                std::fprintf(stderr, "  minimal pixel shader: 0x%08lx, pixel bytes %zu\n",
                             static_cast<unsigned long>(d.device->CreateGraphicsPipelineState(&probe, IID_PPV_ARGS(&p2))),
                             static_cast<std::size_t>(ps->GetBufferSize()));
                ComPtr<ID3D12InfoQueue> info;
                if (SUCCEEDED(d.device.As(&info))) {
                    for (UINT64 i = 0; i < info->GetNumStoredMessages(); ++i) {
                        SIZE_T len = 0;
                        info->GetMessage(i, nullptr, &len);
                        std::vector<char> buf(len);
                        auto* msg = reinterpret_cast<D3D12_MESSAGE*>(buf.data());
                        info->GetMessage(i, msg, &len);
                        std::fprintf(stderr, "  %s\n", msg->pDescription);
                    }
                    info->ClearStoredMessages();
                }
            }
            std::fputc('Y', out);
            continue;
        }
        void* m = nullptr;
        check(constants->Map(0, nullptr, &m), "map constants");
        std::memcpy(static_cast<std::uint8_t*>(m), &vc, sizeof(vc));
        std::memcpy(static_cast<std::uint8_t*>(m) + 2048, &pc, sizeof(pc));
        constants->Unmap(0, nullptr);

        check(d.alloc->Reset(), "reset");
        check(d.list->Reset(d.alloc.Get(), pso.Get()), "list reset");
        D3D12_CPU_DESCRIPTOR_HANDLE rtvs[4];
        for (unsigned t = 0; t < 4; ++t) {
            rtvs[t] = d.rtv_heap->GetCPUDescriptorHandleForHeapStart();
            rtvs[t].ptr += t * d.rtv_size;
        }
        // Targets 1 hold float formats; their views were made for the first formats (motion / reactive share the
        // float target).
        const float clear[4] = {0.25f, 0.5f, 0.75f, 1.0f};
        for (unsigned t = 0; t < 4; ++t) d.list->ClearRenderTargetView(rtvs[t], clear, 0, nullptr);
        const D3D12_CPU_DESCRIPTOR_HANDLE dsv = d.dsv_heap->GetCPUDescriptorHandleForHeapStart();
        d.list->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 0.0f, 0, 0, nullptr);
        d.list->OMSetRenderTargets(target_count, rtvs, FALSE, &dsv);
        D3D12_VIEWPORT vp{0, 0, static_cast<float>(kSize), static_cast<float>(kSize), 0, 1};
        D3D12_RECT sc{0, 0, static_cast<LONG>(kSize), static_cast<LONG>(kSize)};
        d.list->RSSetViewports(1, &vp);
        d.list->RSSetScissorRects(1, &sc);
        d.list->SetGraphicsRootSignature(d.root.Get());
        ID3D12DescriptorHeap* heaps[] = {d.srv_heap.Get(), d.sampler_heap.Get()};
        d.list->SetDescriptorHeaps(2, heaps);
        d.list->SetGraphicsRootDescriptorTable(5, d.sampler_heap->GetGPUDescriptorHandleForHeapStart());
        d.list->SetGraphicsRootConstantBufferView(0, constants->GetGPUVirtualAddress());
        d.list->SetGraphicsRootConstantBufferView(1, constants->GetGPUVirtualAddress() + 2048);
        d.list->SetGraphicsRootDescriptorTable(2, d.srv_heap->GetGPUDescriptorHandleForHeapStart());
        d.list->SetGraphicsRootShaderResourceView(3, d.light_table->GetGPUVirtualAddress());
        d.list->SetGraphicsRootShaderResourceView(4, d.light_rows->GetGPUVirtualAddress());
        d.list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        d.list->DrawInstanced(6u * 64u, 1, 0, 0);
        UINT64 offset = 0;
        const auto copy_out = [&](ID3D12Resource* r, DXGI_FORMAT f, UINT bpp, D3D12_RESOURCE_STATES state) {
            barrier(d.list.Get(), r, state, D3D12_RESOURCE_STATE_COPY_SOURCE);
            D3D12_TEXTURE_COPY_LOCATION dst{d.readback.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, {}};
            dst.PlacedFootprint.Offset = offset;
            dst.PlacedFootprint.Footprint = {f, kSize, kSize, 1, kSize * bpp};
            D3D12_TEXTURE_COPY_LOCATION src{r, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, {}};
            d.list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
            barrier(d.list.Get(), r, D3D12_RESOURCE_STATE_COPY_SOURCE, state);
            offset += kSize * kSize * bpp;
        };
        copy_out(d.targets[0].Get(), DXGI_FORMAT_R8G8B8A8_UNORM, 4u, D3D12_RESOURCE_STATE_RENDER_TARGET);
        for (unsigned t = 1; t < target_count; ++t) {
            copy_out(d.targets[t].Get(), kTargetFormats[t], t == 1u ? 16u : 4u, D3D12_RESOURCE_STATE_RENDER_TARGET);
        }
        if (export_depth) copy_out(d.depth.Get(), DXGI_FORMAT_D32_FLOAT, 4u, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        check(d.list->Close(), "close");
        ID3D12CommandList* lists[] = {d.list.Get()};
        d.queue->ExecuteCommandLists(1, lists);
        d.wait();
        void* rb = nullptr;
        check(d.readback->Map(0, nullptr, &rb), "map readback");
        const std::uint32_t size = static_cast<std::uint32_t>(offset);
        std::fputc('K', out);
        std::fwrite(&n, 4, 1, out);
        std::fwrite(&key, sizeof(key), 1, out);
        std::fwrite(&size, 4, 1, out);
        std::fwrite(rb, 1, size, out);
        d.readback->Unmap(0, nullptr);
        if (n % 500u == 0u) std::fprintf(stderr, "%u keys\n", n);
    }
    std::fclose(out);
    return 0;
}
