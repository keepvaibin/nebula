// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/gx/ray_tracing.h"
#include "galaxy/gx_d3d12.h"

#include "rt_capture_cs.h"
#include "rt_composite_ps.h"
#include "rt_composite_vs.h"
#include "rt_spatial_a_cs.h"
#include "rt_spatial_b_cs.h"
#include "rt_temporal_cs.h"
#include "rt_trace_cs.h"
#include "rt_geometry_cs.h"
#include "rt_validate_cs.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <mutex>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>

namespace galaxy::gx {
namespace {

using Microsoft::WRL::ComPtr;

// Mirrors cbuffer RtFrame in ray_tracing.hlsl.
struct RtFrameConstants {
    std::uint32_t draw_count;
    std::uint32_t triangle_count;
    std::uint32_t vertex_ring_limit;
    std::uint32_t resident_limit;
    std::uint32_t index_ring_limit;
    std::uint32_t constant_ring_limit;
    std::uint32_t matrix_ring_limit;
    std::uint32_t frame_flags;
    float projection[4];
    float depth[4];
    float viewport[4];
    std::uint32_t target_size[2];
    float near_plane;
    std::uint32_t triangle_base;
};
static_assert(sizeof(RtFrameConstants) == 96u);

// Mirrors cbuffer RtLight in rt_lighting.hlsl.
struct RtLightConstants {
    float projection[4];
    float depth[4];
    float viewport[4];
    std::uint32_t target_size[2];
    std::uint32_t sample_size[2];
    float near_plane;
    std::uint32_t frame;
    float range;
    float strength;
    std::uint32_t rays;
    std::uint32_t factor;
    std::uint32_t flags;
    std::uint32_t history_frames;
    float reproject[3][4];
    float picture[3][4];
    float contact;
    float contact_range;
    float sky_saturation;
    float fallback;
    float sun[4];
    float sun_light[4];
};
static_assert(sizeof(RtLightConstants) == 240u);

// Mirrors cbuffer RtCapture in rt_lighting.hlsl (root constants).
struct RtCaptureConstants {
    float depth[4];
    float viewport[4];
    std::uint32_t target_size[2];
    std::uint32_t sample_size[2];
    std::uint32_t factor;
    float emissive;
    std::uint32_t divide;
    float strength;
};
static_assert(sizeof(RtCaptureConstants) == 64u);

// Mirrors cbuffer RtComposite in rt_lighting.hlsl (root constants).
struct RtCompositeConstants {
    float projection[4];
    float depth[4];
    float viewport[4];
    std::uint32_t sample_size[2];
    std::uint32_t factor;
    std::uint32_t flags;
    std::uint32_t slot;
    float near_plane;
    std::uint32_t pad[2];
};
static_assert(sizeof(RtCompositeConstants) == 80u);

enum LightParameter : UINT {
    kLightFrame = 0,
    kLightScene,
    kLightDepth,
    kLightPrevZ,
    kLightPrevHistory,
    kLightHistoryIn,
    kLightPicture,
    kLightPictureSums,
    kLightZ,
    kLightNormal,
    kLightRaw,
    kLightHistory,
    kLightFinal,
    kLightCount,
};

enum CaptureParameter : UINT {
    kCaptureConstants = 0,
    kCaptureSources,
    kCaptureFinal,
    kCapturePicture,
    kCaptureSums,
    kCaptureCount,
};

// A result slot's state: read by the composite (pixel) and the picture capture (compute).
constexpr D3D12_RESOURCE_STATES kLightRead =
    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
constexpr UINT64 kPictureSumsBytes = 32u;

constexpr unsigned kReprojectEntries = 256u;
constexpr UINT64 kReprojectEntryBytes = 256u;

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

// Inverse of a 3x4 affine matrix; false when its 3x3 part is singular.
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

constexpr std::array<float, 12> kIdentity34{1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};

// GALAXY_RT_* "a,b,c" triples.
std::array<float, 3> env_triple(const char* name, std::array<float, 3> fallback) {
    char value[96]{};
    std::size_t length = 0u;
    if (getenv_s(&length, value, sizeof(value), name) != 0 || length <= 1u) {
        return fallback;
    }
    std::array<float, 3> out = fallback;
    char* cursor = value;
    for (float& v : out) {
        char* end = nullptr;
        const float parsed = std::strtof(cursor, &end);
        if (end == cursor) {
            return fallback;
        }
        v = parsed;
        cursor = *end == ',' ? end + 1 : end;
    }
    return out;
}

float env_float(const char* name, float fallback) {
    char value[32]{};
    std::size_t length = 0u;
    return getenv_s(&length, value, sizeof(value), name) == 0 && length > 1u ? std::strtof(value, nullptr)
                                                                              : fallback;
}

// Near plane: depth 1 (reversed Z) is ndc 1, z = -[2][3] / (1 + [2][2]).
float near_plane(const RtView& view) {
    const float denominator = 1.0f + view.z_scale;
    return denominator != 0.0f ? std::max(0.0f, view.z_offset / denominator) : 0.0f;
}

enum RootParameter : UINT {
    kRootFrame = 0,
    kRootDraws,
    kRootVertexRing,
    kRootResident,
    kRootIndexRing,
    kRootConstantRing,
    kRootMatrixRing,
    kRootPositions,
    kRootScene,
    kRootDepth,
    kRootStats,
    kRootDump,
    kRootCount,
};

constexpr std::uint32_t kVsConstantsBytes = 1280u;
constexpr std::uint32_t kPaletteMatrixBytes = 48u;
constexpr UINT kTimestampsPerSlot = 4u;
constexpr UINT64 kReadbackSlotBytes = 128u;

// Highest readable byte offset for a `size`-byte load (0 for an empty or too small buffer).
std::uint32_t read_limit(std::uint64_t bytes, std::uint32_t size) {
    return bytes > size ? static_cast<std::uint32_t>(std::min<std::uint64_t>(bytes - size, 0xFFFFFFF0u)) : 0u;
}

D3D12_RESOURCE_BARRIER transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES before,
                                  D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    return barrier;
}

D3D12_RESOURCE_BARRIER uav_barrier(ID3D12Resource* resource) {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = resource;
    return barrier;
}

std::mutex g_game_sun_mutex;
std::array<float, 3> g_game_sun{};
std::chrono::steady_clock::time_point g_game_sun_time{};
bool g_game_sun_known = false;

}  // namespace

void set_game_sun(const std::array<float, 3>& direction) noexcept {
    const std::lock_guard<std::mutex> lock(g_game_sun_mutex);
    g_game_sun = direction;
    g_game_sun_time = std::chrono::steady_clock::now();
    g_game_sun_known = true;
}

bool game_sun(std::array<float, 3>& direction, std::uint64_t max_age_ms) noexcept {
    const std::lock_guard<std::mutex> lock(g_game_sun_mutex);
    if (!g_game_sun_known ||
        std::chrono::steady_clock::now() - g_game_sun_time > std::chrono::milliseconds(max_age_ms)) {
        return false;
    }
    direction = g_game_sun;
    return true;
}

bool SceneRayTracer::initialize(ID3D12Device* device, std::uint64_t timestamp_frequency, bool validate,
                                DXGI_FORMAT color_format) {
    shutdown();
    if (device == nullptr || FAILED(device->QueryInterface(IID_PPV_ARGS(&device_)))) {
        device_.Reset();
        return false;
    }
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5{};
    D3D12_FEATURE_DATA_SHADER_MODEL shader_model{D3D_SHADER_MODEL_6_5};
    if (FAILED(device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5))) ||
        options5.RaytracingTier < D3D12_RAYTRACING_TIER_1_1 ||
        FAILED(device_->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shader_model, sizeof(shader_model))) ||
        shader_model.HighestShaderModel < D3D_SHADER_MODEL_6_5) {
        std::cerr << "[rt] unavailable: DXR 1.1 with shader model 6.5 required (tier "
                  << static_cast<int>(options5.RaytracingTier) << ")\n";
        device_.Reset();
        return false;
    }

    D3D12_DESCRIPTOR_RANGE depth_range{};
    depth_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    depth_range.NumDescriptors = 1u;
    depth_range.BaseShaderRegister = 7u;
    depth_range.OffsetInDescriptorsFromTableStart = 0u;
    D3D12_ROOT_PARAMETER params[kRootCount]{};
    params[kRootFrame].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[kRootFrame].Descriptor.ShaderRegister = 0u;
    for (UINT i = kRootDraws; i <= kRootMatrixRing; ++i) {
        params[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[i].Descriptor.ShaderRegister = i - kRootDraws;
    }
    params[kRootPositions].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    params[kRootPositions].Descriptor.ShaderRegister = 0u;
    params[kRootScene].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[kRootScene].Descriptor.ShaderRegister = 6u;
    params[kRootDepth].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[kRootDepth].DescriptorTable.NumDescriptorRanges = 1u;
    params[kRootDepth].DescriptorTable.pDescriptorRanges = &depth_range;
    params[kRootStats].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    params[kRootStats].Descriptor.ShaderRegister = 1u;
    params[kRootDump].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    params[kRootDump].Descriptor.ShaderRegister = 2u;
    for (auto& param : params) {
        param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
    D3D12_ROOT_SIGNATURE_DESC rsd{};
    rsd.NumParameters = kRootCount;
    rsd.pParameters = params;
    ComPtr<ID3DBlob> blob, error;
    if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error)) ||
        FAILED(device_->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                            IID_PPV_ARGS(&root_)))) {
        std::cerr << "[rt] root signature failed\n";
        shutdown();
        return false;
    }
    D3D12_COMPUTE_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature = root_.Get();
    pso.CS = {g_rt_geometry_cs, sizeof(g_rt_geometry_cs)};
    if (FAILED(device_->CreateComputePipelineState(&pso, IID_PPV_ARGS(&geometry_pso_)))) {
        std::cerr << "[rt] geometry pipeline failed\n";
        shutdown();
        return false;
    }
    pso.CS = {g_rt_validate_cs, sizeof(g_rt_validate_cs)};
    if (FAILED(device_->CreateComputePipelineState(&pso, IID_PPV_ARGS(&validate_pso_)))) {
        std::cerr << "[rt] validation pipeline failed\n";
        shutdown();
        return false;
    }

    if (!create_buffer(kStatsWords * 4u, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST,
                       D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, stats_, L"rt stats") ||
        !create_buffer(kStatsWords * 4u, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ,
                       D3D12_RESOURCE_FLAG_NONE, stats_zero_, L"rt stats zero") ||
        !create_buffer(kResultSlots * kReadbackSlotBytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST,
                       D3D12_RESOURCE_FLAG_NONE, readback_, L"rt readback") ||
        !create_buffer(sizeof(D3D12_RAYTRACING_INSTANCE_DESC), D3D12_HEAP_TYPE_UPLOAD,
                       D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_FLAG_NONE, instance_, L"rt instance")) {
        shutdown();
        return false;
    }
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
    if (timestamp_frequency != 0u && FAILED(device_->CreateQueryHeap(&query, IID_PPV_ARGS(&timestamps_)))) {
        timestamps_.Reset();
    }
    timestamp_frequency_ = timestamp_frequency;
    validate_ = validate;
    {
        char value[32]{};
        std::size_t length = 0u;
        dump_scene_ = getenv_s(&length, value, sizeof(value), "GALAXY_RT_DUMP") == 0 && length > 1u
            ? std::strtoull(value, nullptr, 10) : 0u;
        dump_every_ = static_cast<std::uint64_t>(std::max(0.0f, env_float("GALAXY_RT_DUMP_EVERY", 0.0f)));
        dump_remaining_ = static_cast<std::uint64_t>(std::max(1.0f, env_float("GALAXY_RT_DUMP_COUNT", 1.0f)));
    }
    lighting_enabled_ = env_float("GALAXY_RT_LIGHT", 1.0f) != 0.0f;
    rays_ = static_cast<std::uint32_t>(std::clamp(env_float("GALAXY_RT_RAYS", 2.0f), 1.0f, 16.0f));
    range_ = std::clamp(env_float("GALAXY_RT_RANGE", 1600.0f), 10.0f, 100000.0f);
    strength_ = std::clamp(env_float("GALAXY_RT_STRENGTH", 0.75f), 0.0f, 1.0f);
    history_frames_ = static_cast<std::uint32_t>(std::clamp(env_float("GALAXY_RT_HISTORY", 16.0f), 1.0f, 64.0f));
    factor_override_ = static_cast<std::uint32_t>(std::clamp(env_float("GALAXY_RT_FACTOR", 0.0f), 0.0f, 4.0f));
    contact_ = std::clamp(env_float("GALAXY_RT_CONTACT", 0.3f), 0.0f, 1.0f);
    contact_range_ = std::clamp(env_float("GALAXY_RT_CONTACT_RANGE", 250.0f), 1.0f, 100000.0f);
    sky_saturation_ = std::clamp(env_float("GALAXY_RT_SKY", 0.35f), 0.0f, 1.0f);
    fallback_ = std::clamp(env_float("GALAXY_RT_FALLBACK", 0.55f), 0.0f, 2.0f);
    emissive_ = std::clamp(env_float("GALAXY_RT_EMISSIVE", 3.0f), 0.0f, 16.0f);
    show_lighting_ = env_float("GALAXY_RT_VIEW", 0.0f) != 0.0f;
    show_term_ = static_cast<std::uint32_t>(std::clamp(env_float("GALAXY_RT_VIEW", 0.0f), 0.0f, 3.0f));
    {
        char value[96]{};
        std::size_t length = 0u;
        sun_override_ = getenv_s(&length, value, sizeof(value), "GALAXY_RT_SUN") == 0 && length > 1u;
    }
    sun_direction_ = env_triple("GALAXY_RT_SUN", sun_direction_);
    sun_color_ = env_triple("GALAXY_RT_SUN_COLOR", sun_color_);
    sun_size_ = std::clamp(env_float("GALAXY_RT_SUN_SIZE", 2.5f), 0.0f, 30.0f);
    sun_light_ = std::clamp(env_float("GALAXY_RT_SUN_LIGHT", 0.55f), 0.0f, 4.0f);
    sky_light_ = std::clamp(env_float("GALAXY_RT_SKY_LIGHT", 0.65f), 0.0f, 4.0f);
    if (lighting_enabled_ && !create_lighting_pipelines(color_format)) {
        std::cerr << "[rt] lighting unavailable\n";
        lighting_enabled_ = false;
    }
    ready_ = true;
    std::cerr << "[rt] ready tier=1.1 validate=" << (validate ? 1 : 0) << " lighting=" << (lighting_enabled_ ? 1 : 0)
              << " rays=" << rays_ << " range=" << range_ << " strength=" << strength_
              << " history=" << history_frames_ << " factor=" << factor_override_ << " contact=" << contact_
              << " sky=" << sky_saturation_ << " emissive=" << emissive_ << " sun=" << sun_direction_[0] << ','
              << sun_direction_[1] << ',' << sun_direction_[2] << " sun-light=" << sun_light_
              << " sky-light=" << sky_light_ << '\n';
    return true;
}

void SceneRayTracer::shutdown() noexcept {
    ready_ = false;
    scene_open_ = false;
    draws_.clear();
    retired_.clear();
    slots_ = {};
    window_ = {};
    positions_.Reset();
    blas_.Reset();
    tlas_.Reset();
    scratch_.Reset();
    instance_.Reset();
    stats_.Reset();
    stats_zero_.Reset();
    readback_.Reset();
    timestamps_.Reset();
    dump_.Reset();
    dump_readback_.Reset();
    dump_pending_ = false;
    geometry_pso_.Reset();
    validate_pso_.Reset();
    root_.Reset();
    trace_pso_.Reset();
    temporal_pso_.Reset();
    spatial_a_pso_.Reset();
    spatial_b_pso_.Reset();
    light_root_.Reset();
    capture_pso_.Reset();
    capture_root_.Reset();
    composite_pso_.Reset();
    composite_root_.Reset();
    for (auto& buffer : light_z_) buffer.Reset();
    light_normal_.Reset();
    light_raw_.Reset();
    for (auto& buffer : light_history_) buffer.Reset();
    for (auto& buffer : light_final_) buffer.Reset();
    picture_.Reset();
    picture_sums_.Reset();
    reproject_.Reset();
    reproject_upload_.Reset();
    reproject_mapped_ = nullptr;
    light_slots_ = {};
    light_samples_ = 0u;
    light_slot_ = -1;
    history_valid_ = picture_valid_ = false;
    camera_known_ = history_camera_known_ = picture_camera_known_ = false;
    device_.Reset();
    capacity_ = 0u;
    triangles_ = 0u;
    positions_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
}

bool SceneRayTracer::create_buffer(std::uint64_t bytes, D3D12_HEAP_TYPE heap, D3D12_RESOURCE_STATES state,
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
        std::cerr << "[rt] buffer allocation failed bytes=" << bytes << '\n';
        out.Reset();
        return false;
    }
    out->SetName(name);
    return true;
}

void SceneRayTracer::retire(ComPtr<ID3D12Resource>& resource) {
    if (resource) {
        retired_.push_back({std::move(resource), frame_index_});
    }
}

bool SceneRayTracer::ensure_capacity(ID3D12GraphicsCommandList* list, std::uint32_t triangles,
                                     std::uint32_t preserved) {
    if (triangles <= capacity_ && blas_ && tlas_) {
        return true;
    }
    ComPtr<ID3D12Resource> previous_positions = positions_;
    const D3D12_RESOURCE_STATES previous_state = positions_state_;
    if (triangles > kMaxTriangles) {
        return false;
    }
    std::uint32_t capacity = std::max<std::uint32_t>(capacity_, 128u * 1024u);
    while (capacity < triangles) {
        capacity *= 2u;
    }
    capacity = std::min(capacity, kMaxTriangles);

    D3D12_RAYTRACING_GEOMETRY_DESC geometry{};
    geometry.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    geometry.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
    geometry.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
    geometry.Triangles.VertexCount = capacity * 3u;
    geometry.Triangles.VertexBuffer.StrideInBytes = 12u;
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS blas_inputs{};
    blas_inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    blas_inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;
    blas_inputs.NumDescs = 1u;
    blas_inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    blas_inputs.pGeometryDescs = &geometry;
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO blas_info{};
    device_->GetRaytracingAccelerationStructurePrebuildInfo(&blas_inputs, &blas_info);
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS tlas_inputs{};
    tlas_inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    tlas_inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    tlas_inputs.NumDescs = 1u;
    tlas_inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO tlas_info{};
    device_->GetRaytracingAccelerationStructurePrebuildInfo(&tlas_inputs, &tlas_info);
    if (blas_info.ResultDataMaxSizeInBytes == 0u || tlas_info.ResultDataMaxSizeInBytes == 0u) {
        return false;
    }
    const auto align = [](std::uint64_t value) {
        return (value + D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT - 1u) &
               ~std::uint64_t{D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT - 1u};
    };
    const std::uint64_t scratch_tlas_offset = align(blas_info.ScratchDataSizeInBytes);
    const std::uint64_t scratch_bytes = scratch_tlas_offset + align(tlas_info.ScratchDataSizeInBytes);

    retire(positions_);
    retire(blas_);
    retire(tlas_);
    retire(scratch_);
    capacity_ = 0u;
    if (!create_buffer(static_cast<std::uint64_t>(capacity) * 36u, D3D12_HEAP_TYPE_DEFAULT,
                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, positions_,
                       L"rt positions") ||
        !create_buffer(blas_info.ResultDataMaxSizeInBytes, D3D12_HEAP_TYPE_DEFAULT,
                       D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
                       D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, blas_, L"rt blas") ||
        !create_buffer(tlas_info.ResultDataMaxSizeInBytes, D3D12_HEAP_TYPE_DEFAULT,
                       D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
                       D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, tlas_, L"rt tlas") ||
        !create_buffer(scratch_bytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                       D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, scratch_, L"rt scratch")) {
        positions_.Reset();
        blas_.Reset();
        tlas_.Reset();
        scratch_.Reset();
        return false;
    }
    positions_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    if (previous_positions && preserved != 0u && list != nullptr) {
        // Triangles flushed by earlier chunks of this scene move to the larger buffer.
        D3D12_RESOURCE_BARRIER barriers[2] = {
            transition(previous_positions.Get(), previous_state, D3D12_RESOURCE_STATE_COPY_SOURCE),
            transition(positions_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST)};
        list->ResourceBarrier(2u, barriers);
        list->CopyBufferRegion(positions_.Get(), 0u, previous_positions.Get(), 0u,
                               static_cast<std::uint64_t>(preserved) * 36u);
        barriers[0] = transition(previous_positions.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, previous_state);
        barriers[1] = transition(positions_.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        list->ResourceBarrier(2u, barriers);
    }
    blas_bytes_ = blas_info.ResultDataMaxSizeInBytes;
    tlas_bytes_ = tlas_info.ResultDataMaxSizeInBytes;
    scratch_bytes_ = scratch_tlas_offset;

    // One instance: the frame's eye-space geometry, untransformed. Written while no frame
    // references the new BLAS; the previous instance buffer retires with the old BLAS.
    retire(instance_);
    if (!create_buffer(sizeof(D3D12_RAYTRACING_INSTANCE_DESC), D3D12_HEAP_TYPE_UPLOAD,
                       D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_FLAG_NONE, instance_, L"rt instance")) {
        return false;
    }
    D3D12_RAYTRACING_INSTANCE_DESC instance{};
    instance.Transform[0][0] = 1.0f;
    instance.Transform[1][1] = 1.0f;
    instance.Transform[2][2] = 1.0f;
    instance.InstanceMask = 0xFFu;
    instance.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE |
                     D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_OPAQUE;
    instance.AccelerationStructure = blas_->GetGPUVirtualAddress();
    void* mapped = nullptr;
    const D3D12_RANGE no_read{0, 0};
    if (FAILED(instance_->Map(0, &no_read, &mapped))) {
        return false;
    }
    std::memcpy(mapped, &instance, sizeof(instance));
    instance_->Unmap(0, nullptr);
    capacity_ = capacity;
    std::cerr << "[rt] capacity triangles=" << capacity << " blas-mb=" << (blas_bytes_ >> 20u)
              << " scratch-mb=" << (scratch_bytes >> 20u) << " positions-mb="
              << ((static_cast<std::uint64_t>(capacity) * 36u) >> 20u) << '\n';
    return true;
}

void SceneRayTracer::begin_frame(std::uint64_t frame_index) {
    frame_index_ = frame_index;
    if (!ready_) {
        return;
    }
    std::erase_if(retired_, [&](const Retired& entry) { return entry.frame + kResultDelay <= frame_index; });
    for (unsigned slot = 0; slot < kResultSlots; ++slot) {
        ResultSlot& result = slots_[slot];
        if (!result.pending || result.frame + kResultDelay > frame_index) {
            continue;
        }
        result.pending = false;
        const D3D12_RANGE range{slot * kReadbackSlotBytes, (slot + 1u) * kReadbackSlotBytes};
        void* mapped = nullptr;
        if (FAILED(readback_->Map(0, &range, &mapped))) {
            continue;
        }
        const auto* base = static_cast<const std::byte*>(mapped) + range.Begin;
        if (result.validated) {
            std::uint32_t stats[kStatsWords]{};
            std::memcpy(stats, base, sizeof(stats));
            for (unsigned i = 0; i < kStatsWords; ++i) {
                window_.stats[i] += stats[i];
            }
            ++window_.validated;
        }
        if (timestamps_ && timestamp_frequency_ != 0u) {
            std::uint64_t ticks[kTimestampsPerSlot]{};
            std::memcpy(ticks, base + 64u, sizeof(ticks));
            const double to_ms = 1000.0 / static_cast<double>(timestamp_frequency_);
            if (ticks[1] >= ticks[0] && ticks[1] - ticks[0] < timestamp_frequency_) {
                window_.build_ms += static_cast<double>(ticks[1] - ticks[0]) * to_ms;
                ++window_.timed;
                if (result.validated && ticks[2] >= ticks[1]) {
                    window_.validate_ms += static_cast<double>(ticks[2] - ticks[1]) * to_ms;
                }
                if (ticks[3] >= ticks[2] && ticks[3] - ticks[2] < timestamp_frequency_) {
                    window_.light_ms += static_cast<double>(ticks[3] - ticks[2]) * to_ms;
                }
            }
        }
        const D3D12_RANGE no_write{0, 0};
        readback_->Unmap(0, &no_write);
    }
    if (dump_pending_ && dump_frame_ + kResultDelay <= frame_index) {
        dump_pending_ = false;
        write_dump();
    }
    if (window_.scenes >= 240u) {
        report();
    }
}

void SceneRayTracer::write_dump() {
    const std::uint64_t pixels = static_cast<std::uint64_t>(dump_width_) * dump_height_;
    const D3D12_RANGE range{0, static_cast<SIZE_T>(pixels * 16u)};
    void* mapped = nullptr;
    if (!dump_readback_ || FAILED(dump_readback_->Map(0, &range, &mapped))) {
        return;
    }
    const auto* texels = static_cast<const float*>(mapped);
    std::string directory = ".";
    {
        char value[MAX_PATH]{};
        std::size_t length = 0u;
        if (getenv_s(&length, value, sizeof(value), "GALAXY_RT_DUMP_DIR") == 0 && length > 1u) {
            directory = value;
        }
    }
    static constexpr std::uint8_t kColors[7][3] = {
        {0, 0, 0}, {0, 255, 255}, {0, 200, 0}, {255, 255, 0}, {255, 0, 0}, {0, 0, 255}, {255, 0, 255}};
    const auto shade = [](float z) {
        // log10 of the eye distance over 1 .. 1e6.
        const float d = std::max(-z, 1.0f);
        return static_cast<std::uint8_t>(std::clamp(std::log10(d) / 6.0f, 0.0f, 1.0f) * 255.0f);
    };
    const char* names[3] = {"class", "raster", "traced"};
    for (unsigned image = 0; image < 3u; ++image) {
        const std::string path = directory + "\\rt_" + names[image] + '_' + std::to_string(dump_scene_) + ".ppm";
        std::ofstream file(path, std::ios::binary);
        file << "P6\n" << dump_width_ << ' ' << dump_height_ << "\n255\n";
        std::vector<std::uint8_t> row(static_cast<std::size_t>(dump_width_) * 3u);
        for (std::uint32_t y = 0; y < dump_height_; ++y) {
            for (std::uint32_t x = 0; x < dump_width_; ++x) {
                const float* texel = texels + (static_cast<std::size_t>(y) * dump_width_ + x) * 4u;
                std::uint8_t* out = row.data() + x * 3u;
                if (image == 0u) {
                    const unsigned kind = std::min(static_cast<unsigned>(texel[0]), 6u);
                    std::memcpy(out, kColors[kind], 3u);
                } else {
                    const bool background = texel[0] < 1.5f;
                    const std::uint8_t v = image == 1u ? (background ? 0u : shade(texel[1]))
                                                       : (texel[2] != 0.0f ? shade(texel[2]) : 0u);
                    out[0] = out[1] = out[2] = v;
                }
            }
            file.write(reinterpret_cast<const char*>(row.data()), static_cast<std::streamsize>(row.size()));
        }
    }
    // Pixels per class for every draw (by the traced triangle), then the draws in table order.
    std::vector<std::array<std::uint32_t, 8>> per_draw(dump_draws_.size());
    for (std::uint64_t i = 0; i < pixels; ++i) {
        const float* texel = texels + i * 4u;
        std::uint32_t triangle = 0u;
        std::memcpy(&triangle, texel + 3, 4u);
        if (triangle == 0xFFFFFFFFu || dump_draws_.empty()) {
            continue;
        }
        const auto it = std::upper_bound(dump_draws_.begin(), dump_draws_.end(), triangle,
                                         [](std::uint32_t t, const RtDraw& d) { return t < d.first_triangle; });
        const std::size_t index = static_cast<std::size_t>(std::distance(dump_draws_.begin(), it)) - 1u;
        if (index < per_draw.size()) {
            ++per_draw[index][std::min(static_cast<unsigned>(texel[0]), 7u)];
        }
    }
    std::ofstream report(directory + "\\rt_draws_" + std::to_string(dump_scene_) + ".txt");
    report << "camera";
    if (dump_camera_known_) {
        for (float v : dump_camera_) report << ' ' << v;
    } else {
        report << " unknown";
    }
    report << "\ndraw first triangles flags vertices proj(A B C D E F) depth(min max) cull phase | "
              "bg bg-hit match0.1 match1 nearer farther miss outside | channel ambient material "
              "light(index px py pz dx dy dz color) x2\n";
    for (std::size_t d = 0; d < dump_draws_.size(); ++d) {
        const RtDraw& draw = dump_draws_[d];
        report << d << ' ' << draw.first_triangle << ' ' << draw.triangle_count << ' ' << draw.flags << ' '
               << draw.vertex_count;
        if (d < dump_infos_.size()) {
            const RtDrawInfo& info = dump_infos_[d];
            for (float p : info.projection) report << ' ' << p;
            report << ' ' << info.min_depth << ' ' << info.max_depth << ' ' << info.cull << ' ' << info.phase;
        }
        report << " |";
        for (std::uint32_t n : per_draw[d]) report << ' ' << n;
        if (d < dump_infos_.size()) {
            const RtDrawInfo& info = dump_infos_[d];
            char words[64];
            std::snprintf(words, sizeof(words), " | %08x %08x %08x", info.channel, info.ambient, info.material);
            report << words;
            for (unsigned l = 0; l < 2u; ++l) {
                report << ' ' << info.light_index[l];
                for (float v : info.light[l]) report << ' ' << v;
                std::snprintf(words, sizeof(words), " %08x", info.light_color[l]);
                report << words;
            }
        }
        report << '\n';
    }
    const D3D12_RANGE no_write{0, 0};
    dump_readback_->Unmap(0, &no_write);
    std::cerr << "[rt] dump scene=" << dump_scene_ << " " << dump_width_ << 'x' << dump_height_ << " dir=" << directory
              << '\n';
    if (dump_remaining_ > 1u && dump_every_ != 0u) {
        --dump_remaining_;
        dump_scene_ += dump_every_;
    }
}

void SceneRayTracer::report() {
    const auto per = [](double value, std::uint64_t n) { return n != 0u ? value / static_cast<double>(n) : 0.0; };
    const auto pct = [&](unsigned word, unsigned of) {
        return window_.stats[of] != 0u
            ? 100.0 * static_cast<double>(window_.stats[word]) / static_cast<double>(window_.stats[of])
            : 0.0;
    };
    char line[768];
    std::snprintf(line, sizeof(line),
                  "[rt-stats] scenes=%llu skipped=%llu draws-per-scene=%.1f triangles-per-scene=%.0f "
                  "max-triangles=%llu capacity=%u build-ms=%.3f validate-ms=%.3f light-ms=%.3f composites=%llu captures=%llu validated=%llu "
                  "geometry-pixels=%.2f%% match-0.1%%=%.3f%% match-1%%=%.3f%% hit-nearer=%.3f%% hit-farther=%.3f%% "
                  "miss=%.3f%% background-hit=%.3f%%\n",
                  static_cast<unsigned long long>(window_.scenes), static_cast<unsigned long long>(window_.skipped),
                  per(static_cast<double>(window_.draws), window_.scenes),
                  per(static_cast<double>(window_.triangles), window_.scenes),
                  static_cast<unsigned long long>(window_.max_triangles), capacity_,
                  per(window_.build_ms, window_.timed), per(window_.validate_ms, window_.validated),
                  per(window_.light_ms, window_.timed), static_cast<unsigned long long>(window_.composites),
                  static_cast<unsigned long long>(window_.captures),
                  static_cast<unsigned long long>(window_.validated), pct(2u, 0u), pct(3u, 2u), pct(4u, 2u),
                  pct(5u, 2u), pct(6u, 2u), pct(7u, 2u), pct(8u, 1u));
    std::cerr << line;
    window_ = {};
}

void SceneRayTracer::begin_scene() noexcept {
    draws_.clear();
    infos_.clear();
    triangles_ = 0u;
    flushed_draws_ = 0u;
    flushed_triangles_ = 0u;
    view_known_ = false;
    scene_open_ = ready_;
    scene_incomplete_ = false;
    light_slot_ = -1;
}

void SceneRayTracer::add_draw(const RtDraw& draw, const RtDrawInfo& info) {
    if (!scene_open_ || draw.triangle_count == 0u || triangles_ + draw.triangle_count > kMaxTriangles) {
        return;
    }
    RtDraw& entry = draws_.emplace_back(draw);
    entry.first_triangle = triangles_;
    triangles_ += draw.triangle_count;
    if (dump_scene_ != 0u && scene_serial_ == dump_scene_) {
        infos_.push_back(info);
    }
}

void SceneRayTracer::set_view(const RtView& view) noexcept {
    view_ = view;
    view_known_ = true;
}

void SceneRayTracer::set_camera(const std::array<float, 12>* view) noexcept {
    camera_known_ = view != nullptr;
    camera_ = view != nullptr ? *view : kIdentity34;
}

std::uint32_t SceneRayTracer::sample_factor(std::uint32_t width, std::uint32_t height) const noexcept {
    if (factor_override_ != 0u) {
        return factor_override_;
    }
    const std::uint64_t pixels = static_cast<std::uint64_t>(width) * height;
    return pixels > 16'000'000u ? 4u : pixels > 6'200'000u ? 3u : 2u;
}

bool SceneRayTracer::create_lighting_pipelines(DXGI_FORMAT color_format) {
    const auto serialize = [&](const D3D12_ROOT_SIGNATURE_DESC& desc, ComPtr<ID3D12RootSignature>& out) {
        ComPtr<ID3DBlob> blob, error;
        return SUCCEEDED(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error)) &&
               SUCCEEDED(device_->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                                      IID_PPV_ARGS(&out)));
    };
    const auto compute = [&](ID3D12RootSignature* root, const void* code, std::size_t bytes,
                             ComPtr<ID3D12PipelineState>& out) {
        D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};
        desc.pRootSignature = root;
        desc.CS = {code, bytes};
        return SUCCEEDED(device_->CreateComputePipelineState(&desc, IID_PPV_ARGS(&out)));
    };

    D3D12_DESCRIPTOR_RANGE depth_range{};
    depth_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    depth_range.NumDescriptors = 1u;
    depth_range.BaseShaderRegister = 7u;
    D3D12_ROOT_PARAMETER params[kLightCount]{};
    params[kLightFrame].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[kLightScene].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[kLightScene].Descriptor.ShaderRegister = 6u;
    params[kLightDepth].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[kLightDepth].DescriptorTable.NumDescriptorRanges = 1u;
    params[kLightDepth].DescriptorTable.pDescriptorRanges = &depth_range;
    for (UINT i = 0; i < 5u; ++i) {
        params[kLightPrevZ + i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[kLightPrevZ + i].Descriptor.ShaderRegister = 8u + i;
        params[kLightZ + i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        params[kLightZ + i].Descriptor.ShaderRegister = i;
    }
    for (auto& param : params) {
        param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
    D3D12_ROOT_SIGNATURE_DESC rsd{};
    rsd.NumParameters = kLightCount;
    rsd.pParameters = params;
    if (!serialize(rsd, light_root_) ||
        !compute(light_root_.Get(), g_rt_trace_cs, sizeof(g_rt_trace_cs), trace_pso_) ||
        !compute(light_root_.Get(), g_rt_temporal_cs, sizeof(g_rt_temporal_cs), temporal_pso_) ||
        !compute(light_root_.Get(), g_rt_spatial_a_cs, sizeof(g_rt_spatial_a_cs), spatial_a_pso_) ||
        !compute(light_root_.Get(), g_rt_spatial_b_cs, sizeof(g_rt_spatial_b_cs), spatial_b_pso_)) {
        return false;
    }

    D3D12_DESCRIPTOR_RANGE sources_range{};
    sources_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    sources_range.NumDescriptors = 2u;
    D3D12_ROOT_PARAMETER capture_params[kCaptureCount]{};
    capture_params[kCaptureConstants].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    capture_params[kCaptureConstants].Constants.Num32BitValues = sizeof(RtCaptureConstants) / 4u;
    capture_params[kCaptureSources].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    capture_params[kCaptureSources].DescriptorTable.NumDescriptorRanges = 1u;
    capture_params[kCaptureSources].DescriptorTable.pDescriptorRanges = &sources_range;
    capture_params[kCaptureFinal].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    capture_params[kCaptureFinal].Descriptor.ShaderRegister = 2u;
    capture_params[kCapturePicture].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    capture_params[kCaptureSums].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    capture_params[kCaptureSums].Descriptor.ShaderRegister = 1u;
    for (auto& param : capture_params) {
        param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
    D3D12_ROOT_SIGNATURE_DESC capture_rsd{};
    capture_rsd.NumParameters = kCaptureCount;
    capture_rsd.pParameters = capture_params;
    if (!serialize(capture_rsd, capture_root_) ||
        !compute(capture_root_.Get(), g_rt_capture_cs, sizeof(g_rt_capture_cs), capture_pso_)) {
        return false;
    }

    D3D12_DESCRIPTOR_RANGE composite_depth{};
    composite_depth.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    composite_depth.NumDescriptors = 1u;
    composite_depth.BaseShaderRegister = 1u;
    D3D12_ROOT_PARAMETER composite_params[4]{};
    composite_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    composite_params[0].Constants.Num32BitValues = sizeof(RtCompositeConstants) / 4u;
    composite_params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    composite_params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    composite_params[2].DescriptorTable.NumDescriptorRanges = 1u;
    composite_params[2].DescriptorTable.pDescriptorRanges = &composite_depth;
    composite_params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    composite_params[3].Descriptor.ShaderRegister = 2u;
    for (auto& param : composite_params) {
        param.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    }
    D3D12_ROOT_SIGNATURE_DESC composite_rsd{};
    composite_rsd.NumParameters = 4u;
    composite_rsd.pParameters = composite_params;
    if (!serialize(composite_rsd, composite_root_)) {
        return false;
    }
    D3D12_GRAPHICS_PIPELINE_STATE_DESC graphics{};
    graphics.pRootSignature = composite_root_.Get();
    graphics.VS = {g_rt_composite_vs, sizeof(g_rt_composite_vs)};
    graphics.PS = {g_rt_composite_ps, sizeof(g_rt_composite_ps)};
    // Colour times the lighting: dst * src0 + dst * src1 (GALAXY_RT_VIEW: the lighting alone).
    // The destination alpha the game may read stays.
    D3D12_RENDER_TARGET_BLEND_DESC& blend = graphics.BlendState.RenderTarget[0];
    blend.BlendEnable = show_lighting_ ? FALSE : TRUE;
    blend.SrcBlend = D3D12_BLEND_DEST_COLOR;
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
    if (FAILED(device_->CreateGraphicsPipelineState(&graphics, IID_PPV_ARGS(&composite_pso_)))) {
        return false;
    }
    if (!create_buffer(kReprojectEntryBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST,
                       D3D12_RESOURCE_FLAG_NONE, reproject_, L"rt light reprojection") ||
        !create_buffer(kReprojectEntries * kReprojectEntryBytes, D3D12_HEAP_TYPE_UPLOAD,
                       D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_FLAG_NONE, reproject_upload_,
                       L"rt light reprojection upload") ||
        !create_buffer(kPictureSumsBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                       D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, picture_sums_, L"rt picture sums")) {
        return false;
    }
    reproject_state_ = D3D12_RESOURCE_STATE_COPY_DEST;
    picture_sums_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    void* mapped = nullptr;
    const D3D12_RANGE no_read{0, 0};
    if (FAILED(reproject_upload_->Map(0, &no_read, &mapped))) {
        return false;
    }
    reproject_mapped_ = static_cast<std::byte*>(mapped);
    return true;
}

bool SceneRayTracer::ensure_lighting_buffers(std::uint32_t samples) {
    if (samples <= light_samples_ && light_raw_) {
        return true;
    }
    for (auto& buffer : light_z_) retire(buffer);
    retire(light_normal_);
    retire(light_raw_);
    for (auto& buffer : light_history_) retire(buffer);
    for (auto& buffer : light_final_) retire(buffer);
    retire(picture_);
    // Results of the old buffers are gone with them.
    light_slots_ = {};
    light_samples_ = 0u;
    history_valid_ = picture_valid_ = false;
    const std::uint64_t n = samples;
    const D3D12_RESOURCE_FLAGS uav = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    const D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    const auto make = [&](std::uint64_t bytes, ComPtr<ID3D12Resource>& out, const wchar_t* name) {
        return create_buffer(bytes, D3D12_HEAP_TYPE_DEFAULT, state, uav, out, name);
    };
    bool ok = n != 0u && make(n * 4u, light_normal_, L"rt light normal") && make(n * 8u, light_raw_, L"rt light raw") &&
              make(n * 8u, picture_, L"rt picture");
    for (unsigned i = 0; ok && i < 2u; ++i) {
        ok = make(n * 4u, light_z_[i], L"rt light eye z") && make(n * 12u, light_history_[i], L"rt light history");
        light_z_state_[i] = state;
        light_history_state_[i] = state;
    }
    for (unsigned i = 0; ok && i < kLightSlots; ++i) {
        ok = make(n * 12u, light_final_[i], L"rt light result");
        light_final_state_[i] = state;
    }
    picture_state_ = state;
    if (!ok) {
        for (auto& buffer : light_z_) buffer.Reset();
        light_normal_.Reset();
        light_raw_.Reset();
        for (auto& buffer : light_history_) buffer.Reset();
        for (auto& buffer : light_final_) buffer.Reset();
        picture_.Reset();
        return false;
    }
    light_samples_ = samples;
    std::cerr << "[rt] light samples=" << samples << " mb=" << ((n * (4u + 8u + 8u + 2u * 4u + 2u * 12u + 12u * kLightSlots)) >> 20u)
              << '\n';
    return true;
}

void SceneRayTracer::to_state(ID3D12GraphicsCommandList* list, ID3D12Resource* resource,
                              D3D12_RESOURCE_STATES& state, D3D12_RESOURCE_STATES wanted) {
    if (state == wanted) {
        return;
    }
    const D3D12_RESOURCE_BARRIER barrier = transition(resource, state, wanted);
    list->ResourceBarrier(1u, &barrier);
    state = wanted;
}

void SceneRayTracer::trace_lighting(ID3D12GraphicsCommandList* list, Allocate allocate, void* allocate_context,
                                    D3D12_GPU_DESCRIPTOR_HANDLE depth_srv, std::uint32_t width,
                                    std::uint32_t height) {
    light_slot_ = -1;
    if (!lighting_enabled_ || !trace_pso_ || !view_known_ || depth_srv.ptr == 0u || width == 0u || height == 0u) {
        return;
    }
    const std::uint32_t factor = sample_factor(width, height);
    const std::uint32_t sample_width = (width + factor - 1u) / factor;
    const std::uint32_t sample_height = (height + factor - 1u) / factor;
    if (!ensure_lighting_buffers(sample_width * sample_height)) {
        return;
    }
    if (sample_width != history_width_ || sample_height != history_height_ || factor != history_factor_) {
        history_valid_ = false;
        history_width_ = sample_width;
        history_height_ = sample_height;
        history_factor_ = factor;
    }
    const RtUpload upload = allocate(allocate_context, sizeof(RtLightConstants), 256u);
    if (upload.cpu == nullptr) {
        return;
    }
    RtLightConstants c{};
    std::copy(view_.projection.begin(), view_.projection.end(), c.projection);
    c.depth[0] = view_.z_scale;
    c.depth[1] = view_.z_offset;
    c.depth[2] = view_.min_depth;
    c.depth[3] = view_.max_depth;
    std::copy(view_.viewport.begin(), view_.viewport.end(), c.viewport);
    c.target_size[0] = width;
    c.target_size[1] = height;
    c.sample_size[0] = sample_width;
    c.sample_size[1] = sample_height;
    c.near_plane = near_plane(view_);
    c.frame = light_frame_++;
    c.range = range_;
    c.strength = strength_;
    c.rays = rays_;
    c.factor = factor;
    c.history_frames = history_frames_;
    c.contact = contact_;
    c.contact_range = contact_range_;
    c.sky_saturation = sky_saturation_;
    c.fallback = fallback_;
    {
        // The game's visible sun (GALAXY_RT_SUN overrides it); without one, no sun light.
        std::array<float, 3> l = sun_direction_;
        bool sun_present = sun_override_;
        if (!sun_override_) {
            sun_present = game_sun(l, 2000u);
        }
        if (camera_known_) {
            l = {camera_[0] * l[0] + camera_[1] * l[1] + camera_[2] * l[2],
                 camera_[4] * l[0] + camera_[5] * l[1] + camera_[6] * l[2],
                 camera_[8] * l[0] + camera_[9] * l[1] + camera_[10] * l[2]};
        }
        const float length = std::sqrt(l[0] * l[0] + l[1] * l[1] + l[2] * l[2]);
        const std::array<float, 3> fallback_sun{0.3f, 0.8f, 0.5f};
        for (unsigned i = 0; i < 3u; ++i) {
            c.sun[i] = length > 1e-6f ? l[i] / length : fallback_sun[i] / std::sqrt(0.98f);
            c.sun_light[i] = sun_present ? sun_color_[i] * sun_light_ : 0.0f;
        }
        c.sun[3] = std::tan(sun_size_ * 3.14159265f / 180.0f);
        // Without a sun the sky and bounce light carry the whole picture.
        c.sun_light[3] = sun_present ? sky_light_ : 1.0f;
    }
    // Earlier frames' eye positions of a point: their camera times the inverse of this one.
    std::array<float, 12> inverse{};
    const bool invertible = camera_known_ && affine_inverse(camera_, inverse);
    const bool history = history_valid_ && invertible && history_camera_known_;
    const bool picture = picture_valid_ && invertible && picture_camera_known_ && picture_width_ == sample_width &&
                         picture_height_ == sample_height && picture_factor_ == factor;
    const std::array<float, 12> reproject = history ? affine_multiply(history_camera_, inverse) : kIdentity34;
    const std::array<float, 12> to_picture = picture ? affine_multiply(picture_camera_, inverse) : kIdentity34;
    c.flags = (history ? 1u : 0u) | (picture ? 2u : 0u) | (show_term_ == 2u ? 4u : 0u) | (show_term_ == 3u ? 8u : 0u);
    std::memcpy(c.reproject, reproject.data(), sizeof(c.reproject));
    std::memcpy(c.picture, to_picture.data(), sizeof(c.picture));
    std::memcpy(upload.cpu, &c, sizeof(c));

    const unsigned current = light_parity_;
    const unsigned previous = current ^ 1u;
    const unsigned slot = next_light_slot_;
    next_light_slot_ = (next_light_slot_ + 1u) % kLightSlots;
    constexpr D3D12_RESOURCE_STATES kUav = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    constexpr D3D12_RESOURCE_STATES kRead = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    to_state(list, light_z_[current].Get(), light_z_state_[current], kUav);
    to_state(list, light_history_[current].Get(), light_history_state_[current], kUav);
    to_state(list, light_z_[previous].Get(), light_z_state_[previous], kRead);
    to_state(list, light_history_[previous].Get(), light_history_state_[previous], kRead);
    to_state(list, light_final_[slot].Get(), light_final_state_[slot], kUav);
    to_state(list, picture_.Get(), picture_state_, kRead);
    to_state(list, picture_sums_.Get(), picture_sums_state_, kRead);

    const UINT groups_x = (sample_width + 7u) / 8u;
    const UINT groups_y = (sample_height + 7u) / 8u;
    list->SetComputeRootSignature(light_root_.Get());
    list->SetComputeRootConstantBufferView(kLightFrame, upload.gpu);
    list->SetComputeRootShaderResourceView(kLightScene, tlas_->GetGPUVirtualAddress());
    list->SetComputeRootDescriptorTable(kLightDepth, depth_srv);
    list->SetComputeRootShaderResourceView(kLightPrevZ, light_z_[previous]->GetGPUVirtualAddress());
    list->SetComputeRootShaderResourceView(kLightPrevHistory, light_history_[previous]->GetGPUVirtualAddress());
    list->SetComputeRootShaderResourceView(kLightHistoryIn, light_history_[previous]->GetGPUVirtualAddress());
    list->SetComputeRootShaderResourceView(kLightPicture, picture_->GetGPUVirtualAddress());
    list->SetComputeRootShaderResourceView(kLightPictureSums, picture_sums_->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(kLightZ, light_z_[current]->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(kLightNormal, light_normal_->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(kLightRaw, light_raw_->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(kLightHistory, light_history_[current]->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(kLightFinal, light_final_[slot]->GetGPUVirtualAddress());
    list->SetPipelineState(trace_pso_.Get());
    list->Dispatch(groups_x, groups_y, 1u);
    const D3D12_RESOURCE_BARRIER all_uav = uav_barrier(nullptr);
    list->ResourceBarrier(1u, &all_uav);
    list->SetPipelineState(temporal_pso_.Get());
    list->Dispatch(groups_x, groups_y, 1u);
    list->ResourceBarrier(1u, &all_uav);
    to_state(list, light_history_[current].Get(), light_history_state_[current], kRead);
    list->SetComputeRootShaderResourceView(kLightHistoryIn, light_history_[current]->GetGPUVirtualAddress());
    // The history slot is read-only from here; its UAV parameter names a buffer the filters do
    // not touch.
    list->SetComputeRootUnorderedAccessView(kLightHistory, light_normal_->GetGPUVirtualAddress());
    list->SetPipelineState(spatial_a_pso_.Get());
    list->Dispatch(groups_x, groups_y, 1u);
    list->ResourceBarrier(1u, &all_uav);
    list->SetPipelineState(spatial_b_pso_.Get());
    list->Dispatch(groups_x, groups_y, 1u);
    to_state(list, light_final_[slot].Get(), light_final_state_[slot], kLightRead);

    LightSlot& info = light_slots_[slot];
    info.view = view_;
    info.camera = camera_;
    info.camera_known = camera_known_;
    info.width = width;
    info.height = height;
    info.sample_width = sample_width;
    info.sample_height = sample_height;
    info.factor = factor;
    info.near_plane = c.near_plane;
    history_camera_ = camera_;
    history_camera_known_ = camera_known_;
    light_parity_ = previous;
    history_valid_ = true;
    light_slot_ = static_cast<int>(slot);
}

void SceneRayTracer::capture_picture(ID3D12GraphicsCommandList* list, D3D12_GPU_DESCRIPTOR_HANDLE color_depth_table,
                                     std::uint32_t width, std::uint32_t height) {
    if (!lighting_active() || !capture_pso_ || list == nullptr || color_depth_table.ptr == 0u || !view_known_ ||
        width == 0u || height == 0u) {
        return;
    }
    const std::uint32_t factor = sample_factor(width, height);
    const std::uint32_t sample_width = (width + factor - 1u) / factor;
    const std::uint32_t sample_height = (height + factor - 1u) / factor;
    if (!ensure_lighting_buffers(sample_width * sample_height)) {
        return;
    }
    // This scene's own lighting is divided out where its result matches the picture.
    const bool divide = light_slot_ >= 0 && light_slots_[static_cast<unsigned>(light_slot_)].width == width &&
                        light_slots_[static_cast<unsigned>(light_slot_)].height == height;
    RtCaptureConstants c{};
    c.depth[0] = view_.z_scale;
    c.depth[1] = view_.z_offset;
    c.depth[2] = view_.min_depth;
    c.depth[3] = view_.max_depth;
    std::copy(view_.viewport.begin(), view_.viewport.end(), c.viewport);
    c.target_size[0] = width;
    c.target_size[1] = height;
    c.sample_size[0] = sample_width;
    c.sample_size[1] = sample_height;
    c.factor = factor;
    c.emissive = emissive_;
    c.divide = divide ? 1u : 0u;
    c.strength = strength_;

    to_state(list, picture_.Get(), picture_state_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    to_state(list, picture_sums_.Get(), picture_sums_state_, D3D12_RESOURCE_STATE_COPY_DEST);
    list->CopyBufferRegion(picture_sums_.Get(), 0u, stats_zero_.Get(), 0u, kPictureSumsBytes);
    to_state(list, picture_sums_.Get(), picture_sums_state_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    list->SetComputeRootSignature(capture_root_.Get());
    list->SetPipelineState(capture_pso_.Get());
    list->SetComputeRoot32BitConstants(kCaptureConstants, sizeof(c) / 4u, &c, 0u);
    list->SetComputeRootDescriptorTable(kCaptureSources, color_depth_table);
    list->SetComputeRootShaderResourceView(
        kCaptureFinal, divide ? light_final_[static_cast<unsigned>(light_slot_)]->GetGPUVirtualAddress()
                              : light_normal_->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(kCapturePicture, picture_->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(kCaptureSums, picture_sums_->GetGPUVirtualAddress());
    list->Dispatch((sample_width + 7u) / 8u, (sample_height + 7u) / 8u, 1u);
    to_state(list, picture_.Get(), picture_state_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    to_state(list, picture_sums_.Get(), picture_sums_state_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    ++window_.captures;
    picture_valid_ = true;
    picture_camera_ = camera_;
    picture_camera_known_ = camera_known_;
    picture_width_ = sample_width;
    picture_height_ = sample_height;
    picture_factor_ = factor;
}

void SceneRayTracer::composite(ID3D12GraphicsCommandList* list, D3D12_CPU_DESCRIPTOR_HANDLE target,
                               D3D12_GPU_DESCRIPTOR_HANDLE depth_srv, std::uint32_t width, std::uint32_t height,
                               int slot, bool reproject) {
    if (list == nullptr || slot < 0 || slot >= static_cast<int>(kLightSlots) || !composite_pso_ ||
        depth_srv.ptr == 0u) {
        return;
    }
    const auto index = static_cast<unsigned>(slot);
    const LightSlot& info = light_slots_[index];
    if (!light_final_[index] || info.sample_width == 0u || info.width != width || info.height != height) {
        return;
    }
    to_state(list, light_final_[index].Get(), light_final_state_[index], kLightRead);
    // Each sub-frame's evaluation list wrote the reprojection before its replay runs.
    reproject = reproject && reproject_;
    RtCompositeConstants c{};
    std::copy(info.view.projection.begin(), info.view.projection.end(), c.projection);
    c.depth[0] = info.view.z_scale;
    c.depth[1] = info.view.z_offset;
    c.depth[2] = info.view.min_depth;
    c.depth[3] = info.view.max_depth;
    std::copy(info.view.viewport.begin(), info.view.viewport.end(), c.viewport);
    c.sample_size[0] = info.sample_width;
    c.sample_size[1] = info.sample_height;
    c.factor = info.factor;
    c.flags = (reproject ? 1u : 0u) | (show_lighting_ ? 2u : 0u);
    c.slot = index;
    c.near_plane = info.near_plane;
    list->OMSetRenderTargets(1u, &target, FALSE, nullptr);
    const D3D12_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f};
    const D3D12_RECT scissor{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    list->RSSetViewports(1u, &viewport);
    list->RSSetScissorRects(1u, &scissor);
    list->SetGraphicsRootSignature(composite_root_.Get());
    list->SetPipelineState(composite_pso_.Get());
    list->SetGraphicsRoot32BitConstants(0u, sizeof(c) / 4u, &c, 0u);
    list->SetGraphicsRootShaderResourceView(1u, light_final_[index]->GetGPUVirtualAddress());
    list->SetGraphicsRootDescriptorTable(2u, depth_srv);
    list->SetGraphicsRootShaderResourceView(3u, reproject ? reproject_->GetGPUVirtualAddress()
                                                          : light_final_[index]->GetGPUVirtualAddress());
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list->DrawInstanced(3u, 1u, 0u, 0u);
    ++window_.composites;
}

void SceneRayTracer::write_reprojection(ID3D12GraphicsCommandList* list, const std::array<float, 12>& camera_t) {
    if (list == nullptr || !lighting_active() || !reproject_ || reproject_mapped_ == nullptr) {
        return;
    }
    std::array<float, 12> inverse{};
    const bool invertible = affine_inverse(camera_t, inverse);
    float rows[kLightSlots][12]{};
    for (unsigned slot = 0; slot < kLightSlots; ++slot) {
        const LightSlot& info = light_slots_[slot];
        const std::array<float, 12> m =
            invertible && info.camera_known ? affine_multiply(info.camera, inverse) : kIdentity34;
        std::memcpy(rows[slot], m.data(), sizeof(rows[slot]));
    }
    static_assert(sizeof(rows) <= kReprojectEntryBytes);
    const UINT64 offset = static_cast<UINT64>(reproject_entry_) * kReprojectEntryBytes;
    reproject_entry_ = (reproject_entry_ + 1u) % kReprojectEntries;
    std::memcpy(reproject_mapped_ + offset, rows, sizeof(rows));
    to_state(list, reproject_.Get(), reproject_state_, D3D12_RESOURCE_STATE_COPY_DEST);
    list->CopyBufferRegion(reproject_.Get(), 0u, reproject_upload_.Get(), offset, sizeof(rows));
    to_state(list, reproject_.Get(), reproject_state_, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

D3D12_GPU_VIRTUAL_ADDRESS SceneRayTracer::scene_tlas() const noexcept {
    return tlas_ ? tlas_->GetGPUVirtualAddress() : 0u;
}

bool SceneRayTracer::fill_constants(const RtSources& sources, Allocate allocate, void* allocate_context,
                                    std::uint32_t draws, std::uint32_t triangle_base, std::uint32_t triangles,
                                    std::uint32_t width, std::uint32_t height, std::uint32_t flags, RtUpload& frame) {
    frame = allocate(allocate_context, sizeof(RtFrameConstants), 256u);
    if (frame.cpu == nullptr) {
        return false;
    }
    RtFrameConstants constants{};
    constants.draw_count = draws;
    constants.triangle_count = triangles;
    constants.vertex_ring_limit = read_limit(sources.vertex_ring.bytes, kRtVertexStride);
    constants.resident_limit = read_limit(sources.resident.bytes, kRtVertexStride);
    constants.index_ring_limit = read_limit(sources.index_ring.bytes, 4u);
    constants.constant_ring_limit = read_limit(sources.constant_ring.bytes, kVsConstantsBytes);
    constants.matrix_ring_limit = read_limit(sources.matrix_ring.bytes, kPaletteMatrixBytes);
    constants.frame_flags = flags;
    std::copy(view_.projection.begin(), view_.projection.end(), constants.projection);
    constants.depth[0] = view_.z_scale;
    constants.depth[1] = view_.z_offset;
    constants.depth[2] = view_.min_depth;
    constants.depth[3] = view_.max_depth;
    std::copy(view_.viewport.begin(), view_.viewport.end(), constants.viewport);
    constants.target_size[0] = width;
    constants.target_size[1] = height;
    constants.near_plane = near_plane(view_);
    constants.triangle_base = triangle_base;
    std::memcpy(frame.cpu, &constants, sizeof(constants));
    return true;
}

void SceneRayTracer::flush_geometry(ID3D12GraphicsCommandList* list, const RtSources& sources, Allocate allocate,
                                    void* allocate_context) {
    if (!ready_ || !scene_open_ || list == nullptr || allocate == nullptr || flushed_draws_ >= draws_.size()) {
        return;
    }
    const std::size_t first = flushed_draws_;
    const std::uint32_t base = flushed_triangles_;
    const std::uint32_t count = triangles_ - base;
    flushed_draws_ = draws_.size();
    flushed_triangles_ = triangles_;
    if (count == 0u || !ensure_capacity(list, triangles_, base)) {
        if (count != 0u) {
            ++window_.skipped;
            scene_incomplete_ = true;
        }
        return;
    }
    const std::size_t table_bytes = (draws_.size() - first) * sizeof(RtDraw);
    const RtUpload table = allocate(allocate_context, table_bytes, 256u);
    RtUpload frame{};
    if (table.cpu == nullptr ||
        !fill_constants(sources, allocate, allocate_context, static_cast<std::uint32_t>(draws_.size() - first), base,
                        count, 0u, 0u, 0u, frame)) {
        ++window_.skipped;
        scene_incomplete_ = true;
        return;
    }
    std::memcpy(table.cpu, draws_.data() + first, table_bytes);
    if (positions_state_ != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        const D3D12_RESOURCE_BARRIER barrier =
            transition(positions_.Get(), positions_state_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        list->ResourceBarrier(1u, &barrier);
        positions_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }
    const D3D12_GPU_VIRTUAL_ADDRESS resident = sources.resident.base != 0u ? sources.resident.base
                                                                           : sources.vertex_ring.base;
    list->SetComputeRootSignature(root_.Get());
    list->SetPipelineState(geometry_pso_.Get());
    list->SetComputeRootConstantBufferView(kRootFrame, frame.gpu);
    list->SetComputeRootShaderResourceView(kRootDraws, table.gpu);
    list->SetComputeRootShaderResourceView(kRootVertexRing, sources.vertex_ring.base);
    list->SetComputeRootShaderResourceView(kRootResident, resident);
    list->SetComputeRootShaderResourceView(kRootIndexRing, sources.index_ring.base);
    list->SetComputeRootShaderResourceView(kRootConstantRing, sources.constant_ring.base);
    list->SetComputeRootShaderResourceView(kRootMatrixRing, sources.matrix_ring.base);
    list->SetComputeRootUnorderedAccessView(kRootPositions, positions_->GetGPUVirtualAddress());
    list->Dispatch((count + 63u) / 64u, 1u, 1u);
    const D3D12_RESOURCE_BARRIER barrier = uav_barrier(positions_.Get());
    list->ResourceBarrier(1u, &barrier);
}

bool SceneRayTracer::trace_scene(ID3D12GraphicsCommandList* list, const RtSources& sources, Allocate allocate,
                                 void* allocate_context, D3D12_GPU_DESCRIPTOR_HANDLE depth_srv,
                                 std::uint32_t width, std::uint32_t height) {
    if (!ready_ || !scene_open_) {
        return false;
    }
    flush_geometry(list, sources, allocate, allocate_context);
    scene_open_ = false;
    if (draws_.empty() || triangles_ == 0u || list == nullptr || allocate == nullptr || !positions_ ||
        capacity_ < triangles_ || scene_incomplete_) {
        return false;
    }
    ++window_.scenes;
    window_.draws += draws_.size();
    window_.triangles += triangles_;
    window_.max_triangles = std::max<std::uint64_t>(window_.max_triangles, triangles_);
    ComPtr<ID3D12GraphicsCommandList4> list4;
    if (FAILED(list->QueryInterface(IID_PPV_ARGS(&list4)))) {
        ++window_.skipped;
        return false;
    }
    const std::uint64_t serial = scene_serial_++;
    const unsigned slot = static_cast<unsigned>(serial % kResultSlots);
    const bool record_results = !slots_[slot].pending;
    const bool validated = validate_ && view_known_ && depth_srv.ptr != 0u && width != 0u && height != 0u &&
                           record_results;
    bool dumping = validated && serial == dump_scene_ && dump_scene_ != 0u && !dump_pending_;
    if (dumping) {
        const std::uint64_t bytes = static_cast<std::uint64_t>(width) * height * 16u;
        dump_.Reset();
        dump_readback_.Reset();
        dumping = create_buffer(bytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, dump_, L"rt dump") &&
                  create_buffer(bytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST,
                                D3D12_RESOURCE_FLAG_NONE, dump_readback_, L"rt dump readback");
    }
    RtUpload frame{};
    if (!fill_constants(sources, allocate, allocate_context, 0u, 0u, triangles_, width, height, dumping ? 1u : 0u,
                        frame)) {
        ++window_.skipped;
        return false;
    }
    if (record_results && timestamps_) {
        list->EndQuery(timestamps_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, slot * kTimestampsPerSlot);
    }
    {
        const D3D12_RESOURCE_BARRIER barrier = transition(
            positions_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        list->ResourceBarrier(1u, &barrier);
        positions_state_ = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    }
    list->SetComputeRootSignature(root_.Get());
    list->SetComputeRootConstantBufferView(kRootFrame, frame.gpu);

    D3D12_RAYTRACING_GEOMETRY_DESC geometry{};
    geometry.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    geometry.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
    geometry.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
    geometry.Triangles.VertexCount = triangles_ * 3u;
    geometry.Triangles.VertexBuffer.StartAddress = positions_->GetGPUVirtualAddress();
    geometry.Triangles.VertexBuffer.StrideInBytes = 12u;
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build{};
    build.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    build.Inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;
    build.Inputs.NumDescs = 1u;
    build.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    build.Inputs.pGeometryDescs = &geometry;
    build.DestAccelerationStructureData = blas_->GetGPUVirtualAddress();
    build.ScratchAccelerationStructureData = scratch_->GetGPUVirtualAddress();
    list4->BuildRaytracingAccelerationStructure(&build, 0u, nullptr);
    {
        const D3D12_RESOURCE_BARRIER barrier = uav_barrier(blas_.Get());
        list->ResourceBarrier(1u, &barrier);
    }
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC top{};
    top.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    top.Inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    top.Inputs.NumDescs = 1u;
    top.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    top.Inputs.InstanceDescs = instance_->GetGPUVirtualAddress();
    top.DestAccelerationStructureData = tlas_->GetGPUVirtualAddress();
    top.ScratchAccelerationStructureData = scratch_->GetGPUVirtualAddress() + scratch_bytes_;
    list4->BuildRaytracingAccelerationStructure(&top, 0u, nullptr);
    {
        const D3D12_RESOURCE_BARRIER barrier = uav_barrier(tlas_.Get());
        list->ResourceBarrier(1u, &barrier);
    }
    if (record_results && timestamps_) {
        list->EndQuery(timestamps_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, slot * kTimestampsPerSlot + 1u);
    }

    if (validated) {
        list->CopyBufferRegion(stats_.Get(), 0u, stats_zero_.Get(), 0u, kStatsWords * 4u);
        D3D12_RESOURCE_BARRIER barrier = transition(stats_.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                                                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        list->ResourceBarrier(1u, &barrier);
        list->SetPipelineState(validate_pso_.Get());
        list->SetComputeRootShaderResourceView(kRootScene, tlas_->GetGPUVirtualAddress());
        list->SetComputeRootDescriptorTable(kRootDepth, depth_srv);
        list->SetComputeRootUnorderedAccessView(kRootStats, stats_->GetGPUVirtualAddress());
        list->SetComputeRootUnorderedAccessView(kRootDump, dumping ? dump_->GetGPUVirtualAddress()
                                                                   : stats_->GetGPUVirtualAddress());
        list->Dispatch((width + 7u) / 8u, (height + 7u) / 8u, 1u);
        if (dumping) {
            D3D12_RESOURCE_BARRIER dump_barrier = transition(dump_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                                             D3D12_RESOURCE_STATE_COPY_SOURCE);
            list->ResourceBarrier(1u, &dump_barrier);
            list->CopyBufferRegion(dump_readback_.Get(), 0u, dump_.Get(), 0u,
                                   static_cast<std::uint64_t>(width) * height * 16u);
            dump_pending_ = true;
            dump_frame_ = frame_index_;
            dump_draws_ = draws_;
            dump_infos_ = infos_;
            dump_camera_ = camera_;
            dump_camera_known_ = camera_known_;
            dump_width_ = width;
            dump_height_ = height;
        }
        barrier = transition(stats_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        list->ResourceBarrier(1u, &barrier);
        list->CopyBufferRegion(readback_.Get(), slot * kReadbackSlotBytes, stats_.Get(), 0u, kStatsWords * 4u);
        barrier = transition(stats_.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
        list->ResourceBarrier(1u, &barrier);
    }
    if (record_results && timestamps_) {
        list->EndQuery(timestamps_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, slot * kTimestampsPerSlot + 2u);
    }
    trace_lighting(list, allocate, allocate_context, depth_srv, width, height);
    if (record_results) {
        if (timestamps_) {
            list->EndQuery(timestamps_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, slot * kTimestampsPerSlot + 3u);
            list->ResolveQueryData(timestamps_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, slot * kTimestampsPerSlot,
                                   kTimestampsPerSlot, readback_.Get(), slot * kReadbackSlotBytes + 64u);
        }
        slots_[slot] = ResultSlot{frame_index_, true, validated, static_cast<std::uint32_t>(draws_.size()),
                                  triangles_};
    }
    return true;
}

}  // namespace galaxy::gx
