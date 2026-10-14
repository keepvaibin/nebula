// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/gx/rt_reflections.h"

#include "rt_reflect_composite_ps.h"
#include "rt_reflect_composite_vs.h"
#include "rt_reflect_filter_a_cs.h"
#include "rt_reflect_filter_cs.h"
#include "rt_reflect_memory_cs.h"
#include "rt_reflect_temporal_cs.h"
#include "rt_reflect_trace_cs.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>

namespace galaxy::gx {
namespace {

using Microsoft::WRL::ComPtr;

// Mirrors cbuffer RtReflect in rt_reflections.hlsl.
struct RtReflectConstants {
    float projection[4];
    float depth[4];
    float viewport[4];
    std::uint32_t target_size[2];
    std::uint32_t sample_size[2];
    float near_plane;
    std::uint32_t frame;
    std::uint32_t factor;
    std::uint32_t flags;
    float reproject[3][4];
    float world_to_eye[3][4];
    float eye_to_world[3][4];
    float classes[8][4];
    float range;
    float lobe;
    std::uint32_t history_frames;
    float fallback;
    float memory_fade;
    std::uint32_t pad[3];
};
static_assert(sizeof(RtReflectConstants) == RtReflections::kConstantsBytes);

// Mirrors cbuffer RtReflectComposite in rt_reflections.hlsl (root constants).
struct RtReflectCompositeConstants {
    float projection[4];
    float depth[4];
    float viewport[4];
    std::uint32_t sample_size[2];
    std::uint32_t factor;
    std::uint32_t flags;
    std::uint32_t slot;
    float near_plane;
    std::uint32_t tile_width;
    float split;
    float classes[5][4];
};
static_assert(sizeof(RtReflectCompositeConstants) == 160u);

enum Parameter : UINT {
    kFrame = 0,
    kScene,
    kPositions,
    kSurfaces,
    kLight,
    kSources,
    kPrevInfo,
    kPrevHistory,
    kSums,
    kHistoryIn,
    kInfo,
    kRaw,
    kHistory,
    kFinal,
    kStats,
    kMemory,
    kPrimary,
    kTiles,
    kParameterCount,
};

enum CompositeParameter : UINT {
    kCompositeConstants = 0,
    kCompositeFinal,
    kCompositeDepth,
    kCompositeReproject,
    kCompositeTiles,
    kCompositeCount,
};

// Per result slot: the trace's stats words, then its timestamps (trace begin, end; composite
// begin, end).
constexpr std::uint64_t kReadbackSlotBytes = 128u;
constexpr std::uint64_t kReadbackTicks = 64u;
constexpr D3D12_RESOURCE_STATES kFinalRead =
    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

constexpr std::array<float, 12> kIdentity34{1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};

// a * b for 3x4 affine matrices (rows; implicit last row 0 0 0 1).
std::array<float, 12> affine_multiply(const std::array<float, 12>& a, const std::array<float, 12>& b) {
    std::array<float, 12> c{};
    for (unsigned r = 0; r < 3u; ++r) {
        for (unsigned k = 0; k < 4u; ++k) {
            c[r * 4u + k] = a[r * 4u] * b[k] + a[r * 4u + 1u] * b[4u + k] + a[r * 4u + 2u] * b[8u + k] +
                            (k == 3u ? a[r * 4u + 3u] : 0.0f);
        }
    }
    return c;
}

bool affine_inverse(const std::array<float, 12>& m, std::array<float, 12>& out) {
    const double a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[6], g = m[8], h = m[9], i = m[10];
    const double det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (!(std::abs(det) > 1e-12)) {
        return false;
    }
    const double inv[9] = {(e * i - f * h) / det, (c * h - b * i) / det, (b * f - c * e) / det,
                           (f * g - d * i) / det, (a * i - c * g) / det, (c * d - a * f) / det,
                           (d * h - e * g) / det, (b * g - a * h) / det, (a * e - b * d) / det};
    for (unsigned r = 0; r < 3u; ++r) {
        out[r * 4u] = static_cast<float>(inv[r * 3u]);
        out[r * 4u + 1u] = static_cast<float>(inv[r * 3u + 1u]);
        out[r * 4u + 2u] = static_cast<float>(inv[r * 3u + 2u]);
        out[r * 4u + 3u] = static_cast<float>(-(inv[r * 3u] * m[3] + inv[r * 3u + 1u] * m[7] +
                                                inv[r * 3u + 2u] * m[11]));
    }
    return true;
}

float env_float(const char* name, float fallback) {
    char value[32]{};
    std::size_t length = 0u;
    return getenv_s(&length, value, sizeof(value), name) == 0 && length > 1u ? std::strtof(value, nullptr)
                                                                              : fallback;
}

D3D12_RESOURCE_BARRIER uav_barrier() {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    return barrier;
}

}  // namespace

bool RtReflections::initialize(ID3D12Device5* device, std::uint64_t timestamp_frequency, DXGI_FORMAT color_format) {
    shutdown();
    if (device == nullptr || env_float("GALAXY_RT_REFLECT", 1.0f) == 0.0f) {
        return false;
    }
    device_ = device;
    // GALAXY_RT_REFLECT_<CLASS>=roughness,F0,strength[,metal[,cover]] per surface class (rt_surfaces.h).
    const auto material = [](const char* name, Material fallback) {
        char value[96]{};
        std::size_t length = 0u;
        if (getenv_s(&length, value, sizeof(value), name) != 0 || length <= 1u) {
            return fallback;
        }
        float v[5] = {fallback.roughness, fallback.f0, fallback.strength, fallback.metal, fallback.cover};
        char* cursor = value;
        for (float& f : v) {
            char* end = nullptr;
            const float parsed = std::strtof(cursor, &end);
            if (end == cursor) {
                break;
            }
            f = parsed;
            cursor = *end == ',' ? end + 1 : end;
        }
        return Material{std::clamp(v[0], 0.0f, 1.0f), std::clamp(v[1], 0.0f, 1.0f), std::clamp(v[2], 0.0f, 1.0f),
                        std::clamp(v[3], 0.0f, 1.0f), std::clamp(v[4], 0.0f, 1.0f)};
    };
    materials_ = {};
    // The game's own shading of its shiny surfaces already carries a reflection (its environment
    // maps): water and glossy reflections mostly add light (low cover); metal replaces half.
    materials_[0] = material("GALAXY_RT_REFLECT_DIFFUSE", {0.6f, 0.04f, 0.0f, 0.0f, 0.0f});
    materials_[1] = material("GALAXY_RT_REFLECT_GLOSSY", {0.3f, 0.06f, 1.0f, 0.0f, 0.15f});
    materials_[2] = material("GALAXY_RT_REFLECT_METAL", {0.15f, 0.9f, 1.0f, 1.0f, 0.5f});
    materials_[3] = material("GALAXY_RT_REFLECT_UNLIT", {0.5f, 0.02f, 0.0f, 0.0f, 0.0f});
    materials_[4] = material("GALAXY_RT_REFLECT_WATER", {0.08f, 0.1f, 1.0f, 0.0f, 0.4f});
    view_ = static_cast<std::uint32_t>(std::clamp(env_float("GALAXY_RT_REFLECT_VIEW", 0.0f), 0.0f, 4.0f));
    history_frames_ =
        static_cast<std::uint32_t>(std::clamp(env_float("GALAXY_RT_REFLECT_HISTORY", 24.0f), 1.0f, 64.0f));
    range_ = std::clamp(env_float("GALAXY_RT_REFLECT_RANGE", 50000.0f), 100.0f, 1.0e6f);
    lobe_ = std::clamp(env_float("GALAXY_RT_REFLECT_LOBE", 0.85f), 0.05f, 1.0f);
    fallback_ = std::clamp(env_float("GALAXY_RT_REFLECT_FALLBACK", 0.0f), 0.0f, 1.0f);
    memory_fade_ = std::clamp(env_float("GALAXY_RT_REFLECT_MEMORY_FADE", 0.99f), 0.0f, 1.0f);
    // Committed resources start zeroed: an empty environment memory.
    if (!create_pipelines(color_format) ||
        !create_buffer(kMemoryTexels * 16u, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                       D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, memory_.resource, L"rt reflect memory") ||
        !create_buffer(kStatsWords * 4u, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                       D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, stats_.resource, L"rt reflect stats") ||
        !create_buffer(kStatsWords * 4u, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ,
                       D3D12_RESOURCE_FLAG_NONE, stats_zero_, L"rt reflect stats zero") ||
        !create_buffer(kResultSlots * kReadbackSlotBytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST,
                       D3D12_RESOURCE_FLAG_NONE, readback_, L"rt reflect readback")) {
        std::cerr << "[rt] reflections unavailable\n";
        shutdown();
        return false;
    }
    stats_.state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    memory_.state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    void* zero = nullptr;
    const D3D12_RANGE no_read{0, 0};
    if (FAILED(stats_zero_->Map(0, &no_read, &zero))) {
        shutdown();
        return false;
    }
    std::memset(zero, 0, kStatsWords * 4u);
    stats_zero_->Unmap(0, nullptr);
    D3D12_QUERY_HEAP_DESC query{};
    query.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    query.Count = kResultSlots * kTimestampsPerSlot;
    if (timestamp_frequency == 0u || FAILED(device_->CreateQueryHeap(&query, IID_PPV_ARGS(&timestamps_)))) {
        timestamps_.Reset();
    }
    timestamp_frequency_ = timestamp_frequency;
    ready_ = true;
    char line[320];
    std::snprintf(line, sizeof(line),
                  "[rt] reflections view=%u history=%u range=%.0f lobe=%.2f fade=%.3f | glossy %.2f %.2f %.2f | "
                  "metal %.2f %.2f %.2f | unlit %.2f %.2f %.2f | water %.2f %.2f %.2f\n",
                  view_, history_frames_, range_, lobe_, memory_fade_, materials_[1].roughness, materials_[1].f0,
                  materials_[1].strength, materials_[2].roughness, materials_[2].f0, materials_[2].strength,
                  materials_[3].roughness, materials_[3].f0, materials_[3].strength, materials_[4].roughness,
                  materials_[4].f0, materials_[4].strength);
    std::cerr << line;
    return true;
}

void RtReflections::shutdown() noexcept {
    ready_ = false;
    root_.Reset();
    trace_pso_.Reset();
    temporal_pso_.Reset();
    filter_a_pso_.Reset();
    filter_pso_.Reset();
    memory_pso_.Reset();
    composite_root_.Reset();
    composite_pso_.Reset();
    for (Buffer& buffer : info_) buffer = {};
    raw_ = {};
    for (Buffer& buffer : history_) buffer = {};
    for (Buffer& buffer : final_) buffer = {};
    for (Buffer& buffer : tiles_) buffer = {};
    memory_ = {};
    stats_ = {};
    stats_zero_.Reset();
    readback_.Reset();
    timestamps_.Reset();
    retired_.clear();
    slots_ = {};
    pending_ = {};
    window_ = {};
    composite_timing_ = -1;
    samples_ = 0u;
    history_valid_ = history_camera_known_ = false;
    device_.Reset();
}

bool RtReflections::create_buffer(std::uint64_t bytes, D3D12_HEAP_TYPE heap, D3D12_RESOURCE_STATES state,
                                  D3D12_RESOURCE_FLAGS flags, ComPtr<ID3D12Resource>& out, const wchar_t* name) {
    D3D12_HEAP_PROPERTIES properties{};
    properties.Type = heap;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = std::max<std::uint64_t>(bytes, 256u);
    desc.Height = 1u;
    desc.DepthOrArraySize = 1u;
    desc.MipLevels = 1u;
    desc.SampleDesc.Count = 1u;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = flags;
    if (FAILED(device_->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr,
                                                IID_PPV_ARGS(&out)))) {
        std::cerr << "[rt] reflection buffer allocation failed bytes=" << bytes << '\n';
        out.Reset();
        return false;
    }
    out->SetName(name);
    return true;
}

void RtReflections::retire(ComPtr<ID3D12Resource>& resource) {
    if (resource) {
        retired_.push_back({std::move(resource), frame_index_});
    }
}

void RtReflections::to_state(ID3D12GraphicsCommandList* list, Buffer& buffer, D3D12_RESOURCE_STATES wanted) {
    if (buffer.state == wanted) {
        return;
    }
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = buffer.resource.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = buffer.state;
    barrier.Transition.StateAfter = wanted;
    list->ResourceBarrier(1u, &barrier);
    buffer.state = wanted;
}

bool RtReflections::create_pipelines(DXGI_FORMAT color_format) {
    const auto serialize = [&](const D3D12_ROOT_SIGNATURE_DESC& desc, ComPtr<ID3D12RootSignature>& out) {
        ComPtr<ID3DBlob> blob, error;
        return SUCCEEDED(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error)) &&
               SUCCEEDED(device_->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                                      IID_PPV_ARGS(&out)));
    };
    const auto compute = [&](const void* code, std::size_t bytes, ComPtr<ID3D12PipelineState>& out) {
        D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};
        desc.pRootSignature = root_.Get();
        desc.CS = {code, bytes};
        return SUCCEEDED(device_->CreateComputePipelineState(&desc, IID_PPV_ARGS(&out)));
    };

    D3D12_DESCRIPTOR_RANGE sources{};
    sources.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    sources.NumDescriptors = 2u;
    sources.BaseShaderRegister = 4u;
    D3D12_ROOT_PARAMETER params[kParameterCount]{};
    params[kFrame].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    for (UINT i = kScene; i <= kLight; ++i) {
        params[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[i].Descriptor.ShaderRegister = i - kScene;
    }
    params[kSources].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[kSources].DescriptorTable.NumDescriptorRanges = 1u;
    params[kSources].DescriptorTable.pDescriptorRanges = &sources;
    for (UINT i = kPrevInfo; i <= kHistoryIn; ++i) {
        params[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[i].Descriptor.ShaderRegister = 6u + (i - kPrevInfo);
    }
    for (UINT i = kInfo; i <= kTiles; ++i) {
        params[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        params[i].Descriptor.ShaderRegister = i - kInfo;
    }
    for (auto& param : params) {
        param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
    D3D12_STATIC_SAMPLER_DESC linear{};
    linear.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    linear.AddressU = linear.AddressV = linear.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    linear.MaxLOD = D3D12_FLOAT32_MAX;
    linear.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_ROOT_SIGNATURE_DESC rsd{};
    rsd.NumParameters = kParameterCount;
    rsd.pParameters = params;
    rsd.NumStaticSamplers = 1u;
    rsd.pStaticSamplers = &linear;
    if (!serialize(rsd, root_) || !compute(g_rt_reflect_trace_cs, sizeof(g_rt_reflect_trace_cs), trace_pso_) ||
        !compute(g_rt_reflect_temporal_cs, sizeof(g_rt_reflect_temporal_cs), temporal_pso_) ||
        !compute(g_rt_reflect_filter_a_cs, sizeof(g_rt_reflect_filter_a_cs), filter_a_pso_) ||
        !compute(g_rt_reflect_filter_cs, sizeof(g_rt_reflect_filter_cs), filter_pso_) ||
        !compute(g_rt_reflect_memory_cs, sizeof(g_rt_reflect_memory_cs), memory_pso_)) {
        return false;
    }

    D3D12_DESCRIPTOR_RANGE depth{};
    depth.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    depth.NumDescriptors = 1u;
    depth.BaseShaderRegister = 1u;
    D3D12_ROOT_PARAMETER composite_params[kCompositeCount]{};
    composite_params[kCompositeConstants].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    composite_params[kCompositeConstants].Constants.Num32BitValues = sizeof(RtReflectCompositeConstants) / 4u;
    composite_params[kCompositeFinal].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    composite_params[kCompositeDepth].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    composite_params[kCompositeDepth].DescriptorTable.NumDescriptorRanges = 1u;
    composite_params[kCompositeDepth].DescriptorTable.pDescriptorRanges = &depth;
    composite_params[kCompositeReproject].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    composite_params[kCompositeReproject].Descriptor.ShaderRegister = 2u;
    composite_params[kCompositeTiles].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    composite_params[kCompositeTiles].Descriptor.ShaderRegister = 3u;
    for (auto& param : composite_params) {
        param.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    }
    D3D12_ROOT_SIGNATURE_DESC composite_rsd{};
    composite_rsd.NumParameters = kCompositeCount;
    composite_rsd.pParameters = composite_params;
    if (!serialize(composite_rsd, composite_root_)) {
        return false;
    }
    D3D12_GRAPHICS_PIPELINE_STATE_DESC graphics{};
    graphics.pRootSignature = composite_root_.Get();
    graphics.VS = {g_rt_reflect_composite_vs, sizeof(g_rt_reflect_composite_vs)};
    graphics.PS = {g_rt_reflect_composite_ps, sizeof(g_rt_reflect_composite_ps)};
    // src0 + dst * src1; the destination alpha the game may read stays.
    D3D12_RENDER_TARGET_BLEND_DESC& blend = graphics.BlendState.RenderTarget[0];
    blend.BlendEnable = TRUE;
    blend.SrcBlend = D3D12_BLEND_ONE;
    blend.DestBlend = D3D12_BLEND_SRC1_COLOR;
    blend.BlendOp = D3D12_BLEND_OP_ADD;
    blend.SrcBlendAlpha = D3D12_BLEND_ZERO;
    blend.DestBlendAlpha = D3D12_BLEND_ONE;
    blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blend.LogicOp = D3D12_LOGIC_OP_NOOP;
    blend.RenderTargetWriteMask =
        D3D12_COLOR_WRITE_ENABLE_RED | D3D12_COLOR_WRITE_ENABLE_GREEN | D3D12_COLOR_WRITE_ENABLE_BLUE;
    graphics.SampleMask = UINT_MAX;
    graphics.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    graphics.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    graphics.RasterizerState.DepthClipEnable = TRUE;
    graphics.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    graphics.NumRenderTargets = 1u;
    graphics.RTVFormats[0] = color_format;
    graphics.SampleDesc.Count = 1u;
    return SUCCEEDED(device_->CreateGraphicsPipelineState(&graphics, IID_PPV_ARGS(&composite_pso_)));
}

bool RtReflections::ensure_buffers(std::uint32_t samples, std::uint32_t tiles) {
    if (samples <= samples_ && tiles <= tiles_capacity_ && raw_.resource) {
        return true;
    }
    for (Buffer& buffer : info_) retire(buffer.resource);
    retire(raw_.resource);
    for (Buffer& buffer : history_) retire(buffer.resource);
    for (Buffer& buffer : final_) retire(buffer.resource);
    for (Buffer& buffer : tiles_) retire(buffer.resource);
    // Results of the old buffers are gone with them.
    slots_ = {};
    samples_ = 0u;
    tiles_capacity_ = 0u;
    history_valid_ = false;
    const std::uint64_t n = samples;
    const auto make = [&](std::uint64_t bytes, Buffer& out, const wchar_t* name) {
        out.state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        return create_buffer(bytes, D3D12_HEAP_TYPE_DEFAULT, out.state, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                             out.resource, name);
    };
    bool ok = n != 0u && make(n * 8u, raw_, L"rt reflect raw");
    for (unsigned i = 0; ok && i < 2u; ++i) {
        ok = make(n * 16u, info_[i], L"rt reflect surface") && make(n * 12u, history_[i], L"rt reflect history");
    }
    for (unsigned i = 0; ok && i < kSlots; ++i) {
        ok = make(n * 16u, final_[i], L"rt reflect result") &&
             make(static_cast<std::uint64_t>(tiles) * 4u, tiles_[i], L"rt reflect tiles");
    }
    if (!ok) {
        for (Buffer& buffer : info_) buffer = {};
        raw_ = {};
        for (Buffer& buffer : history_) buffer = {};
        for (Buffer& buffer : final_) buffer = {};
        for (Buffer& buffer : tiles_) buffer = {};
        return false;
    }
    samples_ = samples;
    tiles_capacity_ = tiles;
    std::cerr << "[rt] reflect samples=" << samples
              << " mb=" << ((n * (8u + 2u * 16u + 2u * 12u + 16u * kSlots)) >> 20u) << '\n';
    return true;
}

void RtReflections::invalidate(unsigned slot) noexcept {
    if (slot < kSlots) {
        slots_[slot].valid = false;
    }
}

void RtReflections::trace(ID3D12GraphicsCommandList* list, const RtReflectScene& scene, unsigned slot,
                          void* constants_cpu, D3D12_GPU_VIRTUAL_ADDRESS constants_gpu,
                          D3D12_GPU_DESCRIPTOR_HANDLE color_depth) {
    if (!ready_ || slot >= kSlots) {
        return;
    }
    slots_[slot].valid = false;
    if (list == nullptr || constants_cpu == nullptr || scene.tlas == 0u || scene.positions == 0u ||
        scene.surfaces == 0u || scene.primary == 0u || scene.light == 0u || color_depth.ptr == 0u ||
        scene.width == 0u || scene.height == 0u || scene.sample_width == 0u || scene.sample_height == 0u ||
        scene.factor == 0u) {
        return;
    }
    const std::uint32_t factor = scene.factor;
    const std::uint32_t sample_width = scene.sample_width;
    const std::uint32_t sample_height = scene.sample_height;
    const UINT groups_x = (sample_width + 7u) / 8u;
    const UINT groups_y = (sample_height + 7u) / 8u;
    if (!ensure_buffers(sample_width * sample_height, groups_x * groups_y)) {
        return;
    }
    if (sample_width != history_width_ || sample_height != history_height_ || factor != history_factor_) {
        history_valid_ = false;
        history_width_ = sample_width;
        history_height_ = sample_height;
        history_factor_ = factor;
    }
    std::array<float, 12> inverse{};
    const bool invertible = scene.camera_known && affine_inverse(scene.camera, inverse);
    const bool history = history_valid_ && invertible && history_camera_known_;
    const std::array<float, 12> reproject = history ? affine_multiply(history_camera_, inverse) : kIdentity34;

    RtReflectConstants c{};
    std::copy(scene.projection.begin(), scene.projection.end(), c.projection);
    std::copy(scene.depth.begin(), scene.depth.end(), c.depth);
    std::copy(scene.viewport.begin(), scene.viewport.end(), c.viewport);
    c.target_size[0] = scene.width;
    c.target_size[1] = scene.height;
    c.sample_size[0] = sample_width;
    c.sample_size[1] = sample_height;
    c.near_plane = scene.near_plane;
    c.frame = frame_++;
    c.factor = factor;
    c.flags = (history ? 1u : 0u) | (scene.sums != 0u ? 4u : 0u) | (invertible ? 8u : 0u);
    std::memcpy(c.reproject, reproject.data(), sizeof(c.reproject));
    // The camera's rotation and its inverse (translations unused: directions only).
    std::memcpy(c.world_to_eye, scene.camera.data(), sizeof(c.world_to_eye));
    std::memcpy(c.eye_to_world, (invertible ? inverse : kIdentity34).data(), sizeof(c.eye_to_world));
    for (unsigned i = 0; i < kClasses; ++i) {
        const Material& m = materials_[i];
        c.classes[i][0] = std::max(m.roughness * m.roughness, 1e-3f);
        c.classes[i][1] = m.strength;
        c.classes[i][2] = m.f0;
        c.classes[i][3] = m.metal;
    }
    c.range = range_;
    c.lobe = lobe_;
    c.history_frames = history_frames_;
    c.fallback = fallback_;
    c.memory_fade = memory_fade_;
    std::memcpy(constants_cpu, &c, sizeof(c));

    const unsigned current = parity_;
    const unsigned previous = current ^ 1u;
    const unsigned result = static_cast<unsigned>(serial_++ % kResultSlots);
    const bool record = !pending_[result].pending;
    if (record && timestamps_) {
        list->EndQuery(timestamps_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, result * kTimestampsPerSlot);
    }
    if (record) {
        to_state(list, stats_, D3D12_RESOURCE_STATE_COPY_DEST);
        list->CopyBufferRegion(stats_.resource.Get(), 0u, stats_zero_.Get(), 0u, kStatsWords * 4u);
    }
    constexpr D3D12_RESOURCE_STATES kUav = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    constexpr D3D12_RESOURCE_STATES kRead = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    to_state(list, stats_, kUav);
    to_state(list, memory_, kUav);
    to_state(list, info_[current], kUav);
    to_state(list, raw_, kUav);
    to_state(list, history_[current], kUav);
    to_state(list, final_[slot], kUav);
    to_state(list, tiles_[slot], kUav);
    to_state(list, info_[previous], kRead);
    to_state(list, history_[previous], kRead);

    const D3D12_GPU_VIRTUAL_ADDRESS previous_info = info_[previous].resource->GetGPUVirtualAddress();
    list->SetComputeRootSignature(root_.Get());
    list->SetComputeRootConstantBufferView(kFrame, constants_gpu);
    list->SetComputeRootShaderResourceView(kScene, scene.tlas);
    list->SetComputeRootShaderResourceView(kPositions, scene.positions);
    list->SetComputeRootShaderResourceView(kSurfaces, scene.surfaces);
    list->SetComputeRootShaderResourceView(kLight, scene.light);
    list->SetComputeRootDescriptorTable(kSources, color_depth);
    list->SetComputeRootShaderResourceView(kPrevInfo, previous_info);
    list->SetComputeRootShaderResourceView(kPrevHistory, history_[previous].resource->GetGPUVirtualAddress());
    list->SetComputeRootShaderResourceView(kSums, scene.sums != 0u ? scene.sums : previous_info);
    list->SetComputeRootShaderResourceView(kHistoryIn, history_[previous].resource->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(kInfo, info_[current].resource->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(kRaw, raw_.resource->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(kHistory, history_[current].resource->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(kFinal, final_[slot].resource->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(kStats, stats_.resource->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(kMemory, memory_.resource->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(kPrimary, scene.primary);
    list->SetComputeRootUnorderedAccessView(kTiles, tiles_[slot].resource->GetGPUVirtualAddress());
    // Also orders the lighting's writes of the primary hits before the trace reads them.
    const D3D12_RESOURCE_BARRIER all_uav = uav_barrier();
    list->SetPipelineState(memory_pso_.Get());
    list->Dispatch(kMemorySize / 8u, kMemorySize / 8u, 1u);
    list->ResourceBarrier(1u, &all_uav);
    list->SetPipelineState(trace_pso_.Get());
    list->Dispatch(groups_x, groups_y, 1u);
    list->ResourceBarrier(1u, &all_uav);
    list->SetPipelineState(temporal_pso_.Get());
    list->Dispatch(groups_x, groups_y, 1u);
    list->ResourceBarrier(1u, &all_uav);
    // The accumulated light is read-only from here; its UAV parameter names a buffer the filters
    // do not touch through it. The first filter writes the raw light's buffer, the second reads it.
    to_state(list, history_[current], kRead);
    list->SetComputeRootShaderResourceView(kHistoryIn, history_[current].resource->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(kHistory, raw_.resource->GetGPUVirtualAddress());
    list->SetPipelineState(filter_a_pso_.Get());
    list->Dispatch(groups_x, groups_y, 1u);
    list->ResourceBarrier(1u, &all_uav);
    list->SetPipelineState(filter_pso_.Get());
    list->Dispatch(groups_x, groups_y, 1u);
    to_state(list, final_[slot], kFinalRead);
    to_state(list, tiles_[slot], kFinalRead);
    if (record) {
        if (timestamps_) {
            list->EndQuery(timestamps_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, result * kTimestampsPerSlot + 1u);
            list->ResolveQueryData(timestamps_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, result * kTimestampsPerSlot, 2u,
                                   readback_.Get(), result * kReadbackSlotBytes + kReadbackTicks);
        }
        to_state(list, stats_, D3D12_RESOURCE_STATE_COPY_SOURCE);
        list->CopyBufferRegion(readback_.Get(), result * kReadbackSlotBytes, stats_.resource.Get(), 0u,
                               kStatsWords * 4u);
        pending_[result] = Pending{frame_index_, true, false};
    }
    composite_timing_ = record && timestamps_ ? static_cast<int>(result) : -1;
    ++window_.traces;

    Slot& info = slots_[slot];
    info.valid = true;
    info.serial = serial_;
    info.projection = scene.projection;
    info.depth = scene.depth;
    info.viewport = scene.viewport;
    info.near_plane = scene.near_plane;
    info.width = scene.width;
    info.height = scene.height;
    info.sample_width = sample_width;
    info.sample_height = sample_height;
    info.factor = factor;
    info.tile_width = groups_x;
    history_camera_ = scene.camera;
    history_camera_known_ = scene.camera_known;
    history_valid_ = true;
    parity_ = previous;
}

void RtReflections::composite(ID3D12GraphicsCommandList* list, D3D12_CPU_DESCRIPTOR_HANDLE target,
                              D3D12_GPU_DESCRIPTOR_HANDLE depth_srv, std::uint32_t width, std::uint32_t height,
                              unsigned slot, D3D12_GPU_VIRTUAL_ADDRESS reproject, float split) {
    if (!ready_ || list == nullptr || slot >= kSlots || depth_srv.ptr == 0u) {
        return;
    }
    const Slot& info = slots_[slot];
    if (!info.valid || info.width != width || info.height != height || !final_[slot].resource) {
        return;
    }
    // GALAXY_RT_REFLECT_VIEW=4: every other traced frame without reflections (consecutive
    // snapshots are A/B pairs).
    if (view_ == 4u && (info.serial & 1u) != 0u) {
        return;
    }
    to_state(list, final_[slot], kFinalRead);
    to_state(list, tiles_[slot], kFinalRead);
    // The traced frame's composite is timed; its sub-frames' cost the same.
    const bool timed = composite_timing_ >= 0 && reproject == 0u;
    const unsigned result = timed ? static_cast<unsigned>(composite_timing_) : 0u;
    if (timed) {
        list->EndQuery(timestamps_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, result * kTimestampsPerSlot + 2u);
    }
    RtReflectCompositeConstants c{};
    std::copy(info.projection.begin(), info.projection.end(), c.projection);
    std::copy(info.depth.begin(), info.depth.end(), c.depth);
    std::copy(info.viewport.begin(), info.viewport.end(), c.viewport);
    c.sample_size[0] = info.sample_width;
    c.sample_size[1] = info.sample_height;
    c.factor = info.factor;
    c.flags = (reproject != 0u ? 1u : 0u) | ((view_ & 3u) << 1u);
    c.slot = slot;
    c.tile_width = info.tile_width;
    c.split = split;
    c.near_plane = info.near_plane;
    for (unsigned i = 0; i < 5u; ++i) {
        const Material& m = materials_[i];
        c.classes[i][0] = m.strength;
        c.classes[i][1] = m.f0;
        c.classes[i][2] = std::max(m.roughness * m.roughness, 1e-3f);
        c.classes[i][3] = m.cover;
    }
    const D3D12_GPU_VIRTUAL_ADDRESS final_address = final_[slot].resource->GetGPUVirtualAddress();
    list->OMSetRenderTargets(1u, &target, FALSE, nullptr);
    const D3D12_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f};
    const D3D12_RECT scissor{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    list->RSSetViewports(1u, &viewport);
    list->RSSetScissorRects(1u, &scissor);
    list->SetGraphicsRootSignature(composite_root_.Get());
    list->SetPipelineState(composite_pso_.Get());
    list->SetGraphicsRoot32BitConstants(kCompositeConstants, sizeof(c) / 4u, &c, 0u);
    list->SetGraphicsRootShaderResourceView(kCompositeFinal, final_address);
    list->SetGraphicsRootDescriptorTable(kCompositeDepth, depth_srv);
    list->SetGraphicsRootShaderResourceView(kCompositeReproject, reproject != 0u ? reproject : final_address);
    list->SetGraphicsRootShaderResourceView(kCompositeTiles, tiles_[slot].resource->GetGPUVirtualAddress());
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list->DrawInstanced(3u, 1u, 0u, 0u);
    if (timed) {
        list->EndQuery(timestamps_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, result * kTimestampsPerSlot + 3u);
        list->ResolveQueryData(timestamps_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, result * kTimestampsPerSlot + 2u, 2u,
                               readback_.Get(), result * kReadbackSlotBytes + kReadbackTicks + 16u);
        pending_[result].composite_timed = true;
        composite_timing_ = -1;
    }
    ++window_.composites;
}

void RtReflections::begin_frame(std::uint64_t frame_index) {
    frame_index_ = frame_index;
    if (!ready_) {
        return;
    }
    std::erase_if(retired_, [&](const Retired& entry) { return entry.frame + kResultDelay <= frame_index; });
    for (unsigned slot = 0; slot < kResultSlots; ++slot) {
        Pending& entry = pending_[slot];
        if (!entry.pending || entry.frame + kResultDelay > frame_index) {
            continue;
        }
        entry.pending = false;
        const D3D12_RANGE range{slot * kReadbackSlotBytes, (slot + 1u) * kReadbackSlotBytes};
        void* mapped = nullptr;
        if (FAILED(readback_->Map(0, &range, &mapped))) {
            continue;
        }
        const auto* base = static_cast<const std::byte*>(mapped) + range.Begin;
        std::uint32_t stats[kStatsWords]{};
        std::memcpy(stats, base, sizeof(stats));
        for (unsigned i = 0; i < kStatsWords; ++i) {
            window_.stats[i] += stats[i];
        }
        ++window_.counted;
        if (timestamps_ && timestamp_frequency_ != 0u) {
            std::uint64_t ticks[kTimestampsPerSlot]{};
            std::memcpy(ticks, base + kReadbackTicks, sizeof(ticks));
            const double to_ms = 1000.0 / static_cast<double>(timestamp_frequency_);
            if (ticks[1] >= ticks[0] && ticks[1] - ticks[0] < timestamp_frequency_) {
                window_.trace_ms += static_cast<double>(ticks[1] - ticks[0]) * to_ms;
                ++window_.timed;
            }
            if (entry.composite_timed && ticks[3] >= ticks[2] && ticks[3] - ticks[2] < timestamp_frequency_) {
                window_.composite_ms += static_cast<double>(ticks[3] - ticks[2]) * to_ms;
                ++window_.composites_timed;
            }
        }
        const D3D12_RANGE no_write{0, 0};
        readback_->Unmap(0, &no_write);
    }
    if (window_.traces >= 240u) {
        report();
    }
}

void RtReflections::report() {
    const auto per = [](double value, std::uint64_t n) { return n != 0u ? value / static_cast<double>(n) : 0.0; };
    const auto pct = [&](unsigned word, unsigned of) {
        return window_.stats[of] != 0u
            ? 100.0 * static_cast<double>(window_.stats[word]) / static_cast<double>(window_.stats[of])
            : 0.0;
    };
    char line[768];
    std::snprintf(line, sizeof(line),
                  "[rt-reflect] traces=%llu trace-ms=%.3f composite-ms=%.3f composites-per-trace=%.2f samples=%u "
                  "geometry-samples=%.0f found=%.2f%% diffuse=%.2f%% glossy=%.2f%% metal=%.2f%% unlit=%.2f%% "
                  "water=%.2f%% rays=%.0f hit-shown=%.2f%% hit-remembered=%.2f%% hit-unknown=%.2f%% "
                  "sky-shown=%.2f%% sky-remembered=%.2f%% sky-average=%.2f%%\n",
                  static_cast<unsigned long long>(window_.traces), per(window_.trace_ms, window_.timed),
                  per(window_.composite_ms, window_.composites_timed),
                  per(static_cast<double>(window_.composites), window_.traces), samples_,
                  per(static_cast<double>(window_.stats[0]), window_.counted), pct(1u, 0u), pct(2u, 1u), pct(3u, 1u),
                  pct(4u, 1u), pct(5u, 1u), pct(6u, 1u), per(static_cast<double>(window_.stats[7]), window_.counted),
                  pct(8u, 7u), pct(9u, 7u), pct(10u, 7u), pct(11u, 7u), pct(12u, 7u), pct(13u, 7u));
    std::cerr << line;
    window_ = {};
}

}  // namespace galaxy::gx
