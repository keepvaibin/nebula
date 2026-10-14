// XF lighting on WARP: the generated and uber vertex shaders run one vertex per case and the lit
// colour channel is read back. Covers a light whose direction the game never initialized (SMG's
// LightFunction::loadLightDiffuse: spotlight attenuation with cos (1,0,0) and a stale direction of
// 6e36, captured from the Honeyhive spring), and checks that finite light attenuation
// (kVsKeyFiniteLightAttn) leaves ordinary lights bit-identical.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#pragma warning(push, 0)
#include <Windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#pragma warning(pop)

#include "galaxy/gx/shader_gen.h"
#include "galaxy/gx/shader_keys.h"
#include "galaxy/gx/uber_constants.h"
#include "galaxy/gx/uber_shaders.h"
#include "galaxy/gx/vertex_loader.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

namespace {

using Microsoft::WRL::ComPtr;
using namespace galaxy::gx;

int g_failures = 0;

void check_hr(HRESULT hr, const char* operation) {
    if (FAILED(hr)) {
        char message[160]{};
        std::snprintf(message, sizeof(message), "%s failed with HRESULT 0x%08X", operation,
                      static_cast<unsigned>(hr));
        throw std::runtime_error(message);
    }
}

ComPtr<ID3DBlob> compile_shader(std::string_view source, const char* target, const char* label, UINT optimization) {
    ComPtr<ID3DBlob> code;
    ComPtr<ID3DBlob> errors;
    const HRESULT hr = D3DCompile(source.data(), source.size(), label, nullptr, nullptr, "main", target,
                                  D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_PACK_MATRIX_ROW_MAJOR | optimization,
                                  0u, &code, &errors);
    if (FAILED(hr)) {
        throw std::runtime_error(std::string{label} + " failed to compile: " +
                                 (errors ? static_cast<const char*>(errors->GetBufferPointer()) : ""));
    }
    return code;
}

D3D12_HEAP_PROPERTIES heap_properties(D3D12_HEAP_TYPE type) {
    D3D12_HEAP_PROPERTIES properties{};
    properties.Type = type;
    properties.CreationNodeMask = 1u;
    properties.VisibleNodeMask = 1u;
    return properties;
}

D3D12_RESOURCE_DESC buffer_desc(UINT64 size) {
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = size;
    desc.Height = 1u;
    desc.DepthOrArraySize = 1u;
    desc.MipLevels = 1u;
    desc.SampleDesc.Count = 1u;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return desc;
}

// The matrix palette of one draw: 0x1A0 float4 elements (XF words 0x000-0x67F).
constexpr unsigned kPaletteElements = 0x1A0u;

struct Light {
    std::uint32_t color = 0u;
    float cos_atten[3]{1.0f, 0.0f, 0.0f};
    float dist_atten[3]{1.0f, 0.0f, 0.0f};
    float position[3]{};
    std::uint32_t direction[3]{};  // raw words: stale stack contents are not floats one would write
};

struct Case {
    const char* label;
    std::uint32_t color_ctrl;
    std::uint32_t alpha_ctrl;
    std::uint32_t vertex_color;
    std::uint32_t ambient;
    float eye_position[3];
    float normal[3];
    std::array<Light, 8> lights;
};

std::uint32_t word(float value) { return std::bit_cast<std::uint32_t>(value); }

class WarpLighting {
public:
    WarpLighting() {
        ComPtr<IDXGIFactory6> factory;
        check_hr(CreateDXGIFactory2(0u, IID_PPV_ARGS(&factory)), "CreateDXGIFactory2");
        ComPtr<IDXGIAdapter> warp;
        check_hr(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)), "EnumWarpAdapter");
        check_hr(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device_)),
                 "D3D12CreateDevice(WARP)");
        D3D12_COMMAND_QUEUE_DESC queue_desc{};
        queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        check_hr(device_->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue_)), "CreateCommandQueue");
        check_hr(device_->CreateFence(0u, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)), "CreateFence");

        // The generated vertex shaders' bindings: b0 constants, t0/space1 matrix palette, b2 vertex
        // blend, t1/space1 previous vertices, t2/space1 blend verdicts.
        D3D12_ROOT_PARAMETER params[5]{};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[0].Descriptor = {0u, 0u};
        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[1].Descriptor = {0u, 1u};
        params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[2].Descriptor = {2u, 0u};
        params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[3].Descriptor = {1u, 1u};
        params[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[4].Descriptor = {2u, 1u};
        for (auto& p : params) p.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        D3D12_ROOT_SIGNATURE_DESC root_desc{};
        root_desc.NumParameters = 5u;
        root_desc.pParameters = params;
        root_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        ComPtr<ID3DBlob> blob;
        ComPtr<ID3DBlob> error;
        check_hr(D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error),
                 "D3D12SerializeRootSignature");
        check_hr(device_->CreateRootSignature(0u, blob->GetBufferPointer(), blob->GetBufferSize(),
                                              IID_PPV_ARGS(&root_)),
                 "CreateRootSignature");
        pixel_ = compile_shader(
            "struct PSIn { float4 pos : SV_Position; float4 col0 : COLOR0; float4 col1 : COLOR1; };\n"
            "float4 main(PSIn i) : SV_Target0 { return i.col0; }\n",
            "ps_5_1", "col0_ps", D3DCOMPILE_OPTIMIZATION_LEVEL1);
    }

    // Runs every case through `vs` with its own key; returns col0 per case.
    std::vector<std::array<float, 4>> run(const std::vector<Case>& cases, const std::vector<VertexShaderKey>& keys,
                                          ID3DBlob* vs) {
        const UINT count = static_cast<UINT>(cases.size());
        static constexpr D3D12_INPUT_ELEMENT_DESC kLayout[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"BINORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 36, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R32_UINT, 0, 48, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"COLOR", 1, DXGI_FORMAT_R32_UINT, 0, 52, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 56, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0, 64, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 2, DXGI_FORMAT_R32G32_FLOAT, 0, 72, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 3, DXGI_FORMAT_R32G32_FLOAT, 0, 80, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 4, DXGI_FORMAT_R32G32_FLOAT, 0, 88, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 5, DXGI_FORMAT_R32G32_FLOAT, 0, 96, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 6, DXGI_FORMAT_R32G32_FLOAT, 0, 104, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 7, DXGI_FORMAT_R32G32_FLOAT, 0, 112, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_UINT, 0, 120, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        };
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc{};
        pso_desc.pRootSignature = root_.Get();
        pso_desc.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
        pso_desc.PS = {pixel_->GetBufferPointer(), pixel_->GetBufferSize()};
        pso_desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        pso_desc.SampleMask = UINT_MAX;
        pso_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        pso_desc.RasterizerState.DepthClipEnable = TRUE;
        pso_desc.InputLayout = {kLayout, static_cast<UINT>(std::size(kLayout))};
        pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
        pso_desc.NumRenderTargets = 1u;
        pso_desc.RTVFormats[0] = DXGI_FORMAT_R32G32B32A32_FLOAT;
        pso_desc.SampleDesc.Count = 1u;
        ComPtr<ID3D12PipelineState> pso;
        check_hr(device_->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&pso)), "CreateGraphicsPipelineState");

        // Per case: constants (1280 bytes, 256-aligned), one palette snapshot and one vertex.
        std::vector<GxVsConstants> constants(count);
        std::vector<std::array<float, 4>> palette(static_cast<std::size_t>(count) * kPaletteElements);
        std::vector<GxVertexOut> vertices(count);
        for (UINT i = 0; i < count; ++i) {
            const Case& c = cases[i];
            GxVsConstants& k = constants[i];
            std::memset(&k, 0, sizeof(k));
            // clip = (0, 0, 0.5, 1) for any eye position; the viewport picks the pixel.
            k.projection[2][3] = 0.5f;
            k.projection[3][3] = 1.0f;
            k.num_chans = 1u;
            k.flags = 2u;
            k.mtx_palette_base = i * kPaletteElements;
            k.inline_pos_matrix[0][0] = k.inline_pos_matrix[1][1] = k.inline_pos_matrix[2][2] = 1.0f;
            for (unsigned ch = 0; ch < 2u; ++ch) {
                for (unsigned b = 0; b < 4u; ++b) {
                    k.chan_ambient[ch][b] = static_cast<float>((c.ambient >> (24u - 8u * b)) & 0xFFu) / 255.0f;
                    k.chan_material[ch][b] = 1.0f;
                }
            }
            apply_uber_vs_key(keys[i], k);

            auto* words = reinterpret_cast<std::uint32_t*>(&palette[static_cast<std::size_t>(i) * kPaletteElements]);
            words[0x400] = words[0x404] = words[0x408] = word(1.0f);  // normal matrix 0: identity
            for (unsigned n = 0; n < 8u; ++n) {
                const Light& l = c.lights[n];
                std::uint32_t* w = words + 0x600u + 16u * n;
                w[3] = l.color;
                for (unsigned j = 0; j < 3u; ++j) {
                    w[4u + j] = word(l.cos_atten[j]);
                    w[7u + j] = word(l.dist_atten[j]);
                    w[10u + j] = word(l.position[j]);
                    w[13u + j] = l.direction[j];
                }
            }
            GxVertexOut& v = vertices[i];
            std::memset(&v, 0, sizeof(v));
            std::memcpy(v.position, c.eye_position, sizeof(v.position));
            std::memcpy(v.normal, c.normal, sizeof(v.normal));
            v.color0 = c.vertex_color;
        }
        const std::array<std::uint32_t, 4> blend_constants{};    // mode 0: the draw's own vertices
        const std::vector<std::uint32_t> verdicts(4u * 8194u, 0u);

        const D3D12_HEAP_PROPERTIES upload = heap_properties(D3D12_HEAP_TYPE_UPLOAD);
        const auto make_upload = [&](const void* data, std::size_t size, const char* what) {
            ComPtr<ID3D12Resource> buffer;
            const D3D12_RESOURCE_DESC desc = buffer_desc(std::max<std::size_t>(size, 256u));
            check_hr(device_->CreateCommittedResource(&upload, D3D12_HEAP_FLAG_NONE, &desc,
                                                      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                      IID_PPV_ARGS(&buffer)),
                     what);
            void* mapped = nullptr;
            const D3D12_RANGE none{0u, 0u};
            check_hr(buffer->Map(0u, &none, &mapped), what);
            std::memcpy(mapped, data, size);
            buffer->Unmap(0u, nullptr);
            return buffer;
        };
        const auto constant_buffer = make_upload(constants.data(), constants.size() * sizeof(GxVsConstants), "constants");
        const auto palette_buffer = make_upload(palette.data(), palette.size() * sizeof(palette[0]), "palette");
        const auto vertex_buffer = make_upload(vertices.data(), vertices.size() * sizeof(GxVertexOut), "vertices");
        const auto blend_buffer = make_upload(blend_constants.data(), sizeof(blend_constants), "blend");
        const auto verdict_buffer = make_upload(verdicts.data(), verdicts.size() * sizeof(verdicts[0]), "verdicts");

        D3D12_RESOURCE_DESC target_desc{};
        target_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        target_desc.Width = count;
        target_desc.Height = 1u;
        target_desc.DepthOrArraySize = 1u;
        target_desc.MipLevels = 1u;
        target_desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        target_desc.SampleDesc.Count = 1u;
        target_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        const D3D12_HEAP_PROPERTIES default_heap = heap_properties(D3D12_HEAP_TYPE_DEFAULT);
        ComPtr<ID3D12Resource> target;
        check_hr(device_->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE, &target_desc,
                                                  D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(&target)),
                 "target");
        D3D12_DESCRIPTOR_HEAP_DESC rtv_desc{};
        rtv_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtv_desc.NumDescriptors = 1u;
        ComPtr<ID3D12DescriptorHeap> rtv_heap;
        check_hr(device_->CreateDescriptorHeap(&rtv_desc, IID_PPV_ARGS(&rtv_heap)), "RTV heap");
        const D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
        device_->CreateRenderTargetView(target.Get(), nullptr, rtv);

        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        UINT64 readback_size = 0u;
        device_->GetCopyableFootprints(&target_desc, 0u, 1u, 0u, &footprint, nullptr, nullptr, &readback_size);
        const D3D12_HEAP_PROPERTIES readback_heap = heap_properties(D3D12_HEAP_TYPE_READBACK);
        const D3D12_RESOURCE_DESC readback_desc = buffer_desc(readback_size);
        ComPtr<ID3D12Resource> readback;
        check_hr(device_->CreateCommittedResource(&readback_heap, D3D12_HEAP_FLAG_NONE, &readback_desc,
                                                  D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)),
                 "readback");

        ComPtr<ID3D12CommandAllocator> allocator;
        check_hr(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)), "allocator");
        ComPtr<ID3D12GraphicsCommandList> list;
        check_hr(device_->CreateCommandList(0u, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), pso.Get(),
                                            IID_PPV_ARGS(&list)),
                 "command list");
        const float clear[4]{-1.0f, -1.0f, -1.0f, -1.0f};
        list->ClearRenderTargetView(rtv, clear, 0u, nullptr);
        list->OMSetRenderTargets(1u, &rtv, FALSE, nullptr);
        list->SetGraphicsRootSignature(root_.Get());
        list->SetGraphicsRootShaderResourceView(1u, palette_buffer->GetGPUVirtualAddress());
        list->SetGraphicsRootConstantBufferView(2u, blend_buffer->GetGPUVirtualAddress());
        list->SetGraphicsRootShaderResourceView(3u, vertex_buffer->GetGPUVirtualAddress());
        list->SetGraphicsRootShaderResourceView(4u, verdict_buffer->GetGPUVirtualAddress());
        list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
        const D3D12_VERTEX_BUFFER_VIEW view{vertex_buffer->GetGPUVirtualAddress(),
                                            static_cast<UINT>(vertices.size() * sizeof(GxVertexOut)),
                                            static_cast<UINT>(sizeof(GxVertexOut))};
        list->IASetVertexBuffers(0u, 1u, &view);
        for (UINT i = 0; i < count; ++i) {
            const D3D12_VIEWPORT viewport{static_cast<float>(i), 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
            const D3D12_RECT scissor{static_cast<LONG>(i), 0, static_cast<LONG>(i + 1u), 1};
            list->RSSetViewports(1u, &viewport);
            list->RSSetScissorRects(1u, &scissor);
            list->SetGraphicsRootConstantBufferView(
                0u, constant_buffer->GetGPUVirtualAddress() + static_cast<UINT64>(i) * sizeof(GxVsConstants));
            list->DrawInstanced(1u, 1u, i, 0u);
        }
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = target.Get();
        barrier.Transition.Subresource = 0u;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        list->ResourceBarrier(1u, &barrier);
        D3D12_TEXTURE_COPY_LOCATION dst{};
        dst.pResource = readback.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint = footprint;
        D3D12_TEXTURE_COPY_LOCATION src{};
        src.pResource = target.Get();
        src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        list->CopyTextureRegion(&dst, 0u, 0u, 0u, &src, nullptr);
        check_hr(list->Close(), "Close");
        ID3D12CommandList* lists[] = {list.Get()};
        queue_->ExecuteCommandLists(1u, lists);
        check_hr(queue_->Signal(fence_.Get(), ++fence_value_), "Signal");
        while (fence_->GetCompletedValue() < fence_value_) {
            check_hr(fence_->SetEventOnCompletion(fence_value_, nullptr), "SetEventOnCompletion");
        }
        check_hr(device_->GetDeviceRemovedReason(), "device removed");

        std::vector<std::array<float, 4>> out(count);
        void* mapped = nullptr;
        const D3D12_RANGE all{0u, static_cast<SIZE_T>(readback_size)};
        check_hr(readback->Map(0u, &all, &mapped), "Map(readback)");
        std::memcpy(out.data(), mapped, out.size() * sizeof(out[0]));
        const D3D12_RANGE none{0u, 0u};
        readback->Unmap(0u, &none);
        return out;
    }

private:
    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12CommandQueue> queue_;
    ComPtr<ID3D12Fence> fence_;
    UINT64 fence_value_ = 0u;
    ComPtr<ID3D12RootSignature> root_;
    ComPtr<ID3DBlob> pixel_;
};

VertexShaderKey key_for(const Case& c, bool finite) {
    VertexShaderKey key{};
    std::memset(&key, 0, sizeof(key));
    key.num_chans = 1u;
    key.flags = static_cast<std::uint8_t>(0x02u | (finite ? kVsKeyFiniteLightAttn : 0u));
    key.channel[0] = c.color_ctrl;
    key.channel[2] = c.alpha_ctrl;
    return key;
}

std::string rgba(const std::array<float, 4>& v) {
    char text[64]{};
    std::snprintf(text, sizeof(text), "%d,%d,%d,%d", static_cast<int>(v[0] * 255.0f + 0.5f),
                  static_cast<int>(v[1] * 255.0f + 0.5f), static_cast<int>(v[2] * 255.0f + 0.5f),
                  static_cast<int>(v[3] * 255.0f + 0.5f));
    return text;
}

void expect_same(const std::array<float, 4>& a, const std::array<float, 4>& b, const std::string& label) {
    if (std::memcmp(a.data(), b.data(), sizeof(a)) != 0) {
        ++g_failures;
        std::printf("FAILED: %s (%s vs %s)\n", label.c_str(), rgba(a).c_str(), rgba(b).c_str());
    } else {
        std::printf("ok: %s (%s)\n", label.c_str(), rgba(a).c_str());
    }
}

std::vector<Case> make_cases() {
    // The Honeyhive spring's lights (GX capture smoke\rf-hh3, chunk 17254, draw 15559): key light 0
    // and fill light 1 at eye-space positions, light 2 the alpha light at the camera; spotlight
    // attenuation with cos (1,0,0), dist (1,0,0); light 0 and 1 directions never initialized.
    Case spring{};
    spring.label = "spring top, stale light direction 6.17e36";
    spring.color_ctrl = 0x70Fu;  // vertex material, lights 0-1, clamp diffuse, spotlight
    spring.alpha_ctrl = 0x692u;  // register material, light 2, signed diffuse, spotlight
    spring.vertex_color = 0xAEAEAEFFu;
    spring.ambient = 0x28284682u;
    const float eye[3]{-50.171974f, -381.99222f, -2586.8904f};
    const float normal[3]{0.07090292f, 0.99733806f, -0.01701726f};
    std::memcpy(spring.eye_position, eye, sizeof(eye));
    std::memcpy(spring.normal, normal, sizeof(normal));
    spring.lights[0].color = 0xE3CB8D00u;
    spring.lights[0].position[0] = 100000.0f;
    spring.lights[0].position[1] = 71200.0f;
    spring.lights[0].position[2] = 76400.0f;
    spring.lights[0].direction[0] = 0x80000000u;
    spring.lights[0].direction[2] = 0x7C948900u;  // 6.16991e36: light 1's colour word
    spring.lights[1] = spring.lights[0];
    spring.lights[1].color = 0x7C948900u;
    spring.lights[1].position[0] = -100000.0f;
    spring.lights[1].position[1] = -31884.0f;
    spring.lights[1].position[2] = -4785.0f;
    spring.lights[2].color = 0x000000A5u;
    spring.lights[2].direction[0] = word(-100000.0f);
    spring.lights[2].direction[1] = word(-31884.0f);
    spring.lights[2].direction[2] = word(-4785.0f);

    std::vector<Case> cases{spring};
    Case cleared = spring;
    cleared.label = "spring top, light direction zero";
    cleared.lights[0].direction[2] = 0u;
    cleared.lights[1].direction[2] = 0u;
    cases.push_back(cleared);

    // Ordinary lights: real spotlights and specular lights with finite directions.
    Case spot = spring;
    spot.label = "spotlight, cos (-3, 4, 0), dist (1, 0.001, 0)";
    spot.lights[0].cos_atten[0] = -3.0f;
    spot.lights[0].cos_atten[1] = 4.0f;
    spot.lights[0].dist_atten[1] = 0.001f;
    spot.lights[0].position[0] = 300.0f;
    spot.lights[0].position[1] = 200.0f;
    spot.lights[0].position[2] = -2400.0f;
    spot.lights[0].direction[0] = word(-0.6f);
    spot.lights[0].direction[1] = word(-0.8f);
    spot.lights[0].direction[2] = 0u;
    spot.lights[1].direction[2] = 0u;
    cases.push_back(spot);

    Case spec = spring;
    spec.label = "specular, half-angle direction";
    spec.color_ctrl = 0x30Fu;  // vertex material, lights 0-1, clamp diffuse, specular
    spec.lights[0].cos_atten[0] = 0.0f;
    spec.lights[0].cos_atten[2] = 1.0f;
    spec.lights[0].dist_atten[0] = 0.5f;
    spec.lights[0].dist_atten[2] = 0.5f;
    spec.lights[0].direction[0] = word(0.0f);
    spec.lights[0].direction[1] = word(0.70710678f);
    spec.lights[0].direction[2] = word(0.70710678f);
    spec.lights[1].direction[2] = 0u;
    cases.push_back(spec);

    Case dim = spring;
    dim.label = "spring lights, normal facing away from the key light";
    dim.normal[0] = 0.0f;
    dim.normal[1] = -1.0f;
    dim.normal[2] = 0.0f;
    dim.lights[0].direction[2] = 0u;
    dim.lights[1].direction[2] = 0u;
    cases.push_back(dim);

    // Ordinary lit draws of the same capture (draws 4890, 4935, 5189: their first vertex, eye-space
    // position and transformed normal); their light directions are finite.
    const auto captured = [&spring](const char* label, std::uint32_t alpha_ctrl, std::uint32_t vertex_color,
                                    const float (&e)[3], const float (&n)[3]) {
        Case c = spring;
        c.label = label;
        c.alpha_ctrl = alpha_ctrl;
        c.vertex_color = vertex_color;
        c.ambient = 0x4040505Fu;
        std::memcpy(c.eye_position, e, sizeof(e));
        std::memcpy(c.normal, n, sizeof(n));
        c.lights[0].color = 0xBBBC9E00u;
        c.lights[1].color = 0x80808000u;
        c.lights[1].position[2] = -5000.0f;
        c.lights[2].color = 0x000000D5u;
        c.lights[2].direction[2] = word(-5000.0f);
        for (unsigned l = 0; l < 2u; ++l) {
            c.lights[l].direction[0] = c.lights[l].direction[1] = 0x80000000u;
            c.lights[l].direction[2] = 0u;
        }
        return c;
    };
    cases.push_back(captured("captured draw 4890", 0x713u, 0xEFEFEFFFu, {2324.568f, 10259.232f, -21999.754f},
                             {1.3292686f, -1.2323613f, 0.845108f}));
    cases.push_back(captured("captured draw 4935", 0x692u, 0x898989FFu, {-1727.9652f, 14457.454f, -34550.717f},
                             {1.1327496f, 0.726217f, 1.4796141f}));
    cases.push_back(captured("captured draw 5189", 0x692u, 0xDFDFDFFFu, {-644.39965f, 3730.9041f, -30031.738f},
                             {0.54048967f, 1.223719f, 1.4866571f}));
    return cases;
}

}  // namespace

int main() {
    try {
        WarpLighting warp;
        const std::vector<Case> cases = make_cases();
        const ShaderGenerator gen;
        const std::string_view uber = uber_vs_source();
        const std::array<UINT, 2> levels{D3DCOMPILE_OPTIMIZATION_LEVEL1, D3DCOMPILE_SKIP_OPTIMIZATION};
        for (const UINT optimization : levels) {
            const char* level = optimization == D3DCOMPILE_SKIP_OPTIMIZATION ? "unoptimized" : "O1";
            std::vector<VertexShaderKey> legacy_keys;
            std::vector<VertexShaderKey> finite_keys;
            std::vector<std::array<float, 4>> special_legacy;
            std::vector<std::array<float, 4>> special_finite;
            for (const Case& c : cases) {
                legacy_keys.push_back(key_for(c, false));
                finite_keys.push_back(key_for(c, true));
                const auto vs_legacy = compile_shader(gen.generate_vs(legacy_keys.back()), "vs_5_1", c.label, optimization);
                const auto vs_finite = compile_shader(gen.generate_vs(finite_keys.back()), "vs_5_1", c.label, optimization);
                special_legacy.push_back(warp.run({c}, {legacy_keys.back()}, vs_legacy.Get())[0]);
                special_finite.push_back(warp.run({c}, {finite_keys.back()}, vs_finite.Get())[0]);
            }
            const auto uber_vs = compile_shader(uber, "vs_5_1", "uber_vs", optimization);
            const auto uber_legacy = warp.run(cases, legacy_keys, uber_vs.Get());
            const auto uber_finite = warp.run(cases, finite_keys, uber_vs.Get());
            for (std::size_t i = 0; i < cases.size(); ++i) {
                const std::string label = std::string(level) + ": " + cases[i].label;
                expect_same(uber_finite[i], special_finite[i], label + ": uber = specialized (finite)");
                expect_same(uber_legacy[i], special_legacy[i], label + ": uber = specialized (legacy)");
                if (i != 0u) {
                    expect_same(special_finite[i], special_legacy[i], label + ": finite = legacy");
                }
            }
            // The stale direction keeps its light: the same colour as with the direction cleared.
            expect_same(special_finite[0], special_finite[1], std::string(level) + ": stale direction = cleared direction");
            if (std::memcmp(special_legacy[0].data(), special_legacy[1].data(), sizeof(special_legacy[0])) == 0) {
                std::printf("note: %s: legacy form kept the light on this device\n", level);
            } else {
                std::printf("ok: %s: legacy form drops the key light (%s vs %s)\n", level,
                            rgba(special_legacy[0]).c_str(), rgba(special_legacy[1]).c_str());
            }
        }
    } catch (const std::exception& error) {
        std::printf("FAILED: %s\n", error.what());
        return 1;
    }
    if (g_failures != 0) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("all xf lighting checks passed\n");
    return 0;
}
