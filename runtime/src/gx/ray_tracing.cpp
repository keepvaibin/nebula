// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/gx/ray_tracing.h"
#include "galaxy/gx_d3d12.h"

#include "rt_bloom_blur_cs.h"
#include "rt_bloom_down_cs.h"
#include "rt_bloom_ps.h"
#include "rt_capture_cs.h"
#include "rt_sky_light_map_cs.h"
#include "rt_sky_light_sh_cs.h"
#include "rt_light_texture_ps.h"
#include "rt_post_blur_cs.h"
#include "rt_post_bright_cs.h"
#include "rt_post_copy_cs.h"
#include "rt_post_focus_cs.h"
#include "rt_post_ps.h"
#include "rt_post_shafts_cs.h"
#include "rt_post_vs.h"
#include "rt_emit_merge_cs.h"
#include "rt_glow_base_cs.h"
#include "rt_glow_emit_cs.h"
#include "rt_sky_state_cs.h"
#include "rt_surface_emit_cs.h"
#include "rt_composite_ps.h"
#include "rt_composite_vs.h"
#include "rt_spatial_a_cs.h"
#include "rt_spatial_b_cs.h"
#include "rt_spatial_m_cs.h"
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
#include <intrin.h>
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
    std::uint32_t point_count;
    float point_light;
    float point_size;
    float sun_reach;
    float points[16][4];
    float to_eye[3][4];
    float ambient[4];
    float point_saturation;
    std::uint32_t draw_count;
    float sky_ibl;
    std::uint32_t pad;
};
static_assert(sizeof(RtLightConstants) == 592u);

// Mirrors cbuffer RtPost in rt_post.hlsl (root constants).
struct RtPostConstants {
    float projection[4];
    float depth[4];
    float viewport[4];
    std::uint32_t size[2];
    std::uint32_t quarter[2];
    float core[4];
    float core_light[4];
    float sun[4];
    float time;
    float bloom;
    float threshold;
    float shafts;
    float dof;
    float grade;
    float split;
    std::uint32_t axis;
    float rim;
    std::uint32_t pad[3];
};
static_assert(sizeof(RtPostConstants) == 160u);

// Mirror cbuffer RtBloom and RtBloomComposite in rt_lighting.hlsl (root constants).
struct RtBloomConstants {
    std::uint32_t size[2];
    std::uint32_t sample_size[2];
    float threshold;
    float strength;
    std::uint32_t axis;
    std::uint32_t pad;
};
static_assert(sizeof(RtBloomConstants) == 32u);
struct RtBloomCompositeConstants {
    float scale[2];
    std::uint32_t size[2];
    float strength;
    float split;
    std::uint32_t pad[2];
};
static_assert(sizeof(RtBloomCompositeConstants) == 32u);

// Mirrors cbuffer RtEmit in rt_lighting.hlsl (root constants).
struct RtEmitConstants {
    float projection[4];
    float depth[4];
    float viewport[4];
    std::uint32_t target_size[2];
    std::uint32_t sample_size[2];
    float to_world[3][4];
    float to_eye[3][4];
    float gain;
    float near_plane;
    std::uint32_t reset;
    std::uint32_t factor;
};
static_assert(sizeof(RtEmitConstants) == 176u);
constexpr std::uint32_t kEmitMaxTiles = 1024u;
constexpr std::uint32_t kEmitLights = 256u;
constexpr std::uint32_t kGlowMaxTriangles = 65536u;

// Mirrors cbuffer RtCapture in rt_lighting.hlsl (root constants).
struct RtCaptureConstants {
    float depth[4];
    float viewport[4];
    std::uint32_t target_size[2];
    std::uint32_t sample_size[2];
    std::uint32_t factor;
    float emissive;
    std::uint32_t divide;   // bit 0: this frame's lighting is divided out; bit 1: what the glows added
    float strength;
    float split;
    float surface_gain;
    float sky_distance;
    float exposure_day;
    float projection[4];
    float to_world[3][4];
    float exposure_night;
    float daylight;
    std::uint32_t exposure;
    std::uint32_t pad;
};
static_assert(sizeof(RtCaptureConstants) == 160u);

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
    float split;
    std::uint32_t pad;
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
    kLightPositions,
    kLightSurfaces,
    kLightEmitters,
    kLightGlowBase,
    kLightGlowAdded,
    kLightDraws,
    kLightSky,
    kLightZ,
    kLightNormal,
    kLightRaw,
    kLightHistory,
    kLightFinal,
    kLightPrimary,
    kLightCount,
};

enum CaptureParameter : UINT {
    kCaptureConstants = 0,
    kCaptureSources,
    kCaptureFinal,
    kCaptureGlowBase,
    kCapturePicture,
    kCaptureSums,
    kCaptureGlow,
    kCaptureFound,
    kCaptureSky,
    kCaptureSkyRadiance,
    kCaptureSkySh,
    kCaptureCount,
};

enum EmitParameter : UINT {
    kEmitConstants = 0,
    kEmitSources,
    kEmitDraws,
    kEmitPositions,
    kEmitAdded,
    kEmitTiles,
    kEmitLightsSlot,
    kEmitList,
    kEmitCount,
};

// A result slot's state: read by the composite (pixel) and the picture capture (compute).
constexpr D3D12_RESOURCE_STATES kLightRead =
    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
// Sky r g b n, scene r g b n (cleared per capture); the sky's luminance over time, known (kept);
// the lighting's luminance sum (x 256) and count (cleared); the exposure, known (kept).
constexpr UINT64 kPictureSumsBytes = 64u;
constexpr UINT64 kPictureSumsCleared = 32u;

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
    kRootSurfaces,
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
std::mutex g_star_core_mutex;
std::array<float, 3> g_star_core{};
float g_star_core_scale = 0.0f;
std::chrono::steady_clock::time_point g_star_core_time{};
std::array<float, 3> g_game_sun{};
float g_game_sun_distance = 0.0f;
std::chrono::steady_clock::time_point g_game_sun_time{};
bool g_game_sun_known = false;
std::uint64_t g_game_sun_updates = 0u;

// The game's lights by position (a light that moves takes a new entry; old ones age out).
struct NotedLight {
    GameLight light{};
    std::chrono::steady_clock::time_point seen{};
    std::uint32_t id = 0u;
    bool used = false;
};
std::mutex g_game_lights_mutex;
std::array<NotedLight, 32> g_point_lights{};
std::array<NotedLight, 16> g_world_lights{};

// A light is its guest info (id) when it has one, else its position.
template <std::size_t N>
void note_light(std::array<NotedLight, N>& lights, const std::array<float, 3>& position,
                const std::array<float, 3>& color, float half_distance = 0.0f, std::uint32_t id = 0u) {
    const auto now = std::chrono::steady_clock::now();
    const std::lock_guard<std::mutex> lock(g_game_lights_mutex);
    NotedLight* slot = &lights[0];
    for (NotedLight& entry : lights) {
        const bool same = id != 0u ? entry.id == id
                                   : std::abs(entry.light.position[0] - position[0]) +
                                             std::abs(entry.light.position[1] - position[1]) +
                                             std::abs(entry.light.position[2] - position[2]) < 100.0f;
        if (entry.used && same) {
            slot = &entry;
            break;
        }
        if (!entry.used || (slot->used && entry.seen < slot->seen)) {
            slot = &entry;
        }
    }
    slot->light = {position, color, half_distance};
    slot->id = id;
    slot->seen = now;
    slot->used = true;
}

template <std::size_t N>
std::size_t active_lights(const std::array<NotedLight, N>& lights, std::span<GameLight> out, std::uint64_t max_age_ms) {
    const auto now = std::chrono::steady_clock::now();
    const std::lock_guard<std::mutex> lock(g_game_lights_mutex);
    std::size_t count = 0u;
    for (const NotedLight& entry : lights) {
        const auto& c = entry.light.color;
        if (count < out.size() && entry.used && now - entry.seen <= std::chrono::milliseconds(max_age_ms) &&
            c[0] + c[1] + c[2] > 0.01f) {
            out[count++] = entry.light;
        }
    }
    return count;
}

}  // namespace

void set_star_core(const std::array<float, 3>& position, float scale) noexcept {
    const std::lock_guard<std::mutex> lock(g_star_core_mutex);
    g_star_core = position;
    g_star_core_scale = scale;
    g_star_core_time = std::chrono::steady_clock::now();
}

bool star_core(std::array<float, 3>& position, float& scale, std::uint64_t max_age_ms) noexcept {
    const std::lock_guard<std::mutex> lock(g_star_core_mutex);
    if (g_star_core_scale <= 0.0f ||
        std::chrono::steady_clock::now() - g_star_core_time > std::chrono::milliseconds(max_age_ms)) {
        return false;
    }
    position = g_star_core;
    scale = g_star_core_scale;
    return true;
}

void set_game_sun(const std::array<float, 3>& direction, float distance) noexcept {
    const std::lock_guard<std::mutex> lock(g_game_sun_mutex);
    g_game_sun = direction;
    g_game_sun_distance = distance;
    g_game_sun_time = std::chrono::steady_clock::now();
    g_game_sun_known = true;
    ++g_game_sun_updates;
}

std::uint64_t game_sun_updates() noexcept {
    const std::lock_guard<std::mutex> lock(g_game_sun_mutex);
    return g_game_sun_updates;
}

void note_game_point_light(std::uint32_t info, const std::array<float, 3>& position,
                           const std::array<float, 3>& color, float half_distance) noexcept {
    note_light(g_point_lights, position, color, half_distance, info);
}

void note_game_world_light(const std::array<float, 3>& position, const std::array<float, 3>& color) noexcept {
    note_light(g_world_lights, position, color);
}

std::size_t game_point_lights(std::span<GameLight> out, std::uint64_t max_age_ms) noexcept {
    return active_lights(g_point_lights, out, max_age_ms);
}

std::size_t game_world_lights(std::span<GameLight> out, std::uint64_t max_age_ms) noexcept {
    return active_lights(g_world_lights, out, max_age_ms);
}

bool game_sun(std::array<float, 3>& direction, std::uint64_t max_age_ms, float* distance) noexcept {
    const std::lock_guard<std::mutex> lock(g_game_sun_mutex);
    if (!g_game_sun_known ||
        std::chrono::steady_clock::now() - g_game_sun_time > std::chrono::milliseconds(max_age_ms)) {
        return false;
    }
    direction = g_game_sun;
    if (distance != nullptr) {
        *distance = g_game_sun_distance;
    }
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
    params[kRootSurfaces].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    params[kRootSurfaces].Descriptor.ShaderRegister = 3u;
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
    constexpr UINT64 kSkyBytes = kSkyBins * kSkyBinWords * 4u;
    if (!create_buffer(kSkyBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST,
                       D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, sky_bins_, L"rt sky bins") ||
        !create_buffer(kSkyBytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_FLAG_NONE,
                       sky_zero_, L"rt sky zero") ||
        !create_buffer(kSkyRing * kSkyBytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST,
                       D3D12_RESOURCE_FLAG_NONE, sky_readback_, L"rt sky readback") ||
        !create_buffer(kSkyBins * 16u, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                       D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, sky_radiance_, L"rt sky radiance") ||
        !create_buffer(128u, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                       D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, sky_sh_, L"rt sky light") ||
        FAILED(sky_zero_->Map(0, &no_read, &zero))) {
        shutdown();
        return false;
    }
    std::memset(zero, 0, kSkyBytes);
    sky_zero_->Unmap(0, nullptr);
    sky_bins_state_ = D3D12_RESOURCE_STATE_COPY_DEST;
    sky_radiance_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    sky_sh_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    sky_radiance_generation_ = ~0u;
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
    strength_ = std::clamp(env_float("GALAXY_RT_STRENGTH", 0.9f), 0.0f, 1.0f);
    history_frames_ = static_cast<std::uint32_t>(std::clamp(env_float("GALAXY_RT_HISTORY", 32.0f), 1.0f, 64.0f));
    factor_override_ = static_cast<std::uint32_t>(std::clamp(env_float("GALAXY_RT_FACTOR", 0.0f), 0.0f, 4.0f));
    contact_ = std::clamp(env_float("GALAXY_RT_CONTACT", 0.3f), 0.0f, 1.0f);
    contact_range_ = std::clamp(env_float("GALAXY_RT_CONTACT_RANGE", 250.0f), 1.0f, 100000.0f);
    sky_saturation_ = std::clamp(env_float("GALAXY_RT_SKY", 0.12f), 0.0f, 1.0f);
    fallback_ = std::clamp(env_float("GALAXY_RT_FALLBACK", 0.55f), 0.0f, 2.0f);
    emissive_ = std::clamp(env_float("GALAXY_RT_EMISSIVE", 3.0f), 0.0f, 16.0f);
    show_lighting_ = env_float("GALAXY_RT_VIEW", 0.0f) != 0.0f;
    show_term_ = static_cast<std::uint32_t>(std::clamp(env_float("GALAXY_RT_VIEW", 0.0f), 0.0f, 6.0f));
    point_radius_ = std::clamp(env_float("GALAXY_RT_POINT_RADIUS", 800.0f), 10.0f, 1.0e6f);
    point_size_ = std::clamp(env_float("GALAXY_RT_POINT_SIZE", 600.0f), 0.0f, 1.0e5f);
    point_light_ = std::clamp(env_float("GALAXY_RT_POINT_LIGHT", 2.0f), 0.0f, 8.0f);
    point_saturation_ = std::clamp(env_float("GALAXY_RT_POINT_SATURATION", 0.4f), 0.0f, 1.0f);
    split_ = std::clamp(env_float("GALAXY_RT_SPLIT", 0.0f), 0.0f, 1.0f);
    emission_ = std::clamp(env_float("GALAXY_RT_EMISSION", 24.0f), 0.0f, 256.0f);
    night_ = std::clamp(env_float("GALAXY_RT_NIGHT", 1.0f), 0.0f, 1.0f);
    daylight_ = std::clamp(env_float("GALAXY_RT_DAYLIGHT", 0.35f), 0.01f, 1.0f);
    glow_boost_ = std::clamp(env_float("GALAXY_RT_GLOW_BOOST", 0.4f), 0.0f, 1.0f);
    glow_exposure_ = std::clamp(env_float("GALAXY_RT_GLOW_EXPOSURE", 1.7f), 1.0f, 6.0f);
    glow_spill_ = std::clamp(env_float("GALAXY_RT_GLOW_SPILL", 0.7f), 0.0f, 2.0f);
    glow_model_exposure_ = std::clamp(env_float("GALAXY_RT_GLOW_MODEL_EXPOSURE", 3.2f), 1.0f, 8.0f);
    glow_model_spill_ = std::clamp(env_float("GALAXY_RT_GLOW_MODEL_SPILL", 1.0f), 0.0f, 2.0f);
    sky_distance_ = std::clamp(env_float("GALAXY_RT_SKY_DISTANCE", 60000.0f), 1000.0f, 1.0e7f);
    sky_ibl_ = std::clamp(env_float("GALAXY_RT_SKY_IBL", 0.8f), 0.0f, 1.0f);
    post_ = env_float("GALAXY_RT_POST", 1.0f) != 0.0f;
    post_bloom_ = std::clamp(env_float("GALAXY_RT_POST_BLOOM", 0.3f), 0.0f, 4.0f);
    post_threshold_ = std::clamp(env_float("GALAXY_RT_POST_THRESHOLD", 0.97f), 0.0f, 0.99f);
    post_shafts_ = std::clamp(env_float("GALAXY_RT_POST_SHAFTS", 0.6f), 0.0f, 4.0f);
    post_dof_ = std::clamp(env_float("GALAXY_RT_POST_DOF", 3.0f), 0.0f, 32.0f);
    post_grade_ = std::clamp(env_float("GALAXY_RT_POST_GRADE", 1.0f), 0.0f, 1.0f);
    post_rim_ = std::clamp(env_float("GALAXY_RT_POST_RIM", 0.35f), 0.0f, 2.0f);
    post_view_ = env_float("GALAXY_RT_POST_VIEW", 0.0f) != 0.0f;
    gi_ = env_float("GALAXY_RT_GI", 1.0f) != 0.0f;
    core_radius_ = std::clamp(env_float("GALAXY_RT_CORE_RADIUS", 400.0f), 1.0f, 100000.0f);
    core_light_ = std::clamp(env_float("GALAXY_RT_CORE_LIGHT", 2.5f), 0.0f, 16.0f);
    bloom_strength_ = std::clamp(env_float("GALAXY_RT_BLOOM", 0.8f), 0.0f, 4.0f);
    bloom_threshold_ = std::clamp(env_float("GALAXY_RT_BLOOM_THRESHOLD", 0.15f), 0.0f, 4.0f);
    exposure_ = env_float("GALAXY_RT_EXPOSURE", 1.0f) != 0.0f;
    exposure_day_ = std::clamp(env_float("GALAXY_RT_EXPOSURE_DAY", 0.95f), 0.3f, 1.5f);
    exposure_night_ = std::clamp(env_float("GALAXY_RT_EXPOSURE_NIGHT", 0.95f), 0.3f, 1.5f);
    surface_emission_ = std::clamp(env_float("GALAXY_RT_SURFACE_EMISSION", 3.0f), 0.0f, 256.0f);
    {
        char value[96]{};
        std::size_t length = 0u;
        sun_override_ = getenv_s(&length, value, sizeof(value), "GALAXY_RT_SUN") == 0 && length > 1u;
    }
    sun_direction_ = env_triple("GALAXY_RT_SUN", sun_direction_);
    sun_color_ = env_triple("GALAXY_RT_SUN_COLOR", sun_color_);
    sun_size_ = std::clamp(env_float("GALAXY_RT_SUN_SIZE", 2.5f), 0.0f, 30.0f);
    key_size_ = std::clamp(env_float("GALAXY_RT_KEY_SIZE", 6.0f), 0.0f, 30.0f);
    sun_light_ = std::clamp(env_float("GALAXY_RT_SUN_LIGHT", 1.0f), 0.0f, 4.0f);
    sky_light_ = std::clamp(env_float("GALAXY_RT_SKY_LIGHT", 0.65f), 0.0f, 4.0f);
    if (lighting_enabled_ && !create_lighting_pipelines(color_format)) {
        std::cerr << "[rt] lighting unavailable\n";
        lighting_enabled_ = false;
    }
    if (lighting_enabled_) {
        static_cast<void>(reflections_.initialize(device_.Get(), timestamp_frequency, color_format));
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
    reflections_.shutdown();
    ready_ = false;
    scene_open_ = false;
    draws_.clear();
    retired_.clear();
    slots_ = {};
    window_ = {};
    positions_.Reset();
    surfaces_.Reset();
    blas_.Reset();
    tlas_.Reset();
    scratch_.Reset();
    instance_.Reset();
    stats_.Reset();
    stats_zero_.Reset();
    readback_.Reset();
    sky_bins_.Reset();
    sky_zero_.Reset();
    sky_readback_.Reset();
    sky_radiance_.Reset();
    sky_sh_.Reset();
    sky_reads_ = {};
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
    spatial_m_pso_.Reset();
    spatial_b_pso_.Reset();
    light_root_.Reset();
    capture_pso_.Reset();
    capture_root_.Reset();
    glow_base_pso_.Reset();
    light_tex_root_.Reset();
    light_tex_pso_.Reset();
    light_tex_.Reset();
    light_tex_rtv_heap_.Reset();
    light_tex_srv_heap_.Reset();
    light_tex_srv_ = {};
    light_tex_width_ = light_tex_height_ = 0u;
    light_tex_ready_ = false;
    post_root_.Reset();
    post_copy_pso_.Reset();
    post_bright_pso_.Reset();
    post_blur_pso_.Reset();
    post_shafts_pso_.Reset();
    post_focus_pso_.Reset();
    post_finish_root_.Reset();
    post_finish_pso_.Reset();
    for (auto& buffer : post_buffers_) buffer.Reset();
    post_pixels_ = post_quarter_ = 0u;
    bloom_root_.Reset();
    bloom_down_pso_.Reset();
    bloom_blur_pso_.Reset();
    bloom_composite_root_.Reset();
    bloom_composite_pso_.Reset();
    bloom_temp_.Reset();
    for (auto& buffer : bloom_) buffer.Reset();
    bloom_entries_ = 0u;
    bloom_slot_ = -1;
    sky_state_pso_.Reset();
    surface_emit_pso_.Reset();
    sky_light_map_pso_.Reset();
    sky_light_sh_pso_.Reset();
    glow_emit_pso_.Reset();
    emit_merge_pso_.Reset();
    emit_root_.Reset();
    glow_base_.Reset();
    glow_added_.Reset();
    glow_positions_.Reset();
    glow_surfaces_.Reset();
    emit_tiles_.Reset();
    emit_lights_.Reset();
    emit_list_.Reset();
    composite_pso_.Reset();
    composite_root_.Reset();
    for (auto& buffer : light_z_) buffer.Reset();
    light_normal_.Reset();
    light_raw_.Reset();
    light_primary_.Reset();
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
    surfaces_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
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
    ComPtr<ID3D12Resource> previous_surfaces = surfaces_;
    const D3D12_RESOURCE_STATES previous_surfaces_state = surfaces_state_;
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
    retire(surfaces_);
    retire(blas_);
    retire(tlas_);
    retire(scratch_);
    capacity_ = 0u;
    if (!create_buffer(static_cast<std::uint64_t>(capacity) * 16u, D3D12_HEAP_TYPE_DEFAULT,
                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, surfaces_,
                       L"rt surfaces") ||
        !create_buffer(static_cast<std::uint64_t>(capacity) * 36u, D3D12_HEAP_TYPE_DEFAULT,
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
        surfaces_.Reset();
        blas_.Reset();
        tlas_.Reset();
        scratch_.Reset();
        return false;
    }
    positions_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    surfaces_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    if (previous_surfaces && preserved != 0u && list != nullptr) {
        D3D12_RESOURCE_BARRIER barriers[2] = {
            transition(previous_surfaces.Get(), previous_surfaces_state, D3D12_RESOURCE_STATE_COPY_SOURCE),
            transition(surfaces_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST)};
        list->ResourceBarrier(2u, barriers);
        list->CopyBufferRegion(surfaces_.Get(), 0u, previous_surfaces.Get(), 0u,
                               static_cast<std::uint64_t>(preserved) * 16u);
        barriers[0] = transition(previous_surfaces.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, previous_surfaces_state);
        barriers[1] = transition(surfaces_.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        list->ResourceBarrier(2u, barriers);
    }
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
              << ((static_cast<std::uint64_t>(capacity) * (36u + 16u)) >> 20u) << '\n';
    return true;
}

void SceneRayTracer::begin_frame(std::uint64_t frame_index) {
    frame_index_ = frame_index;
    if (!ready_) {
        return;
    }
    reflections_.begin_frame(frame_index);
    read_sky();
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
    // Pixels per class for every draw (by the traced triangle), then the draws in table order. The
    // surface image colours each pixel by its triangle's surface class (rt_surfaces.h): diffuse grey,
    // glossy cyan, metal orange, unlit magenta, water blue.
    static constexpr std::uint8_t kSurfaceColors[6][3] = {
        {70, 70, 70}, {0, 220, 255}, {255, 170, 0}, {255, 0, 255}, {0, 80, 255}, {255, 255, 255}};
    std::vector<std::uint8_t> surface_image(static_cast<std::size_t>(pixels) * 3u, 0u);
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
            const unsigned surface = std::min((dump_draws_[index].flags & kRtDrawClassMask) >> kRtDrawClassShift, 5u);
            std::memcpy(surface_image.data() + i * 3u, kSurfaceColors[surface], 3u);
        }
    }
    {
        std::ofstream file(directory + "\\rt_surface_" + std::to_string(dump_scene_) + ".ppm", std::ios::binary);
        file << "P6\n" << dump_width_ << ' ' << dump_height_ << "\n255\n";
        file.write(reinterpret_cast<const char*>(surface_image.data()),
                   static_cast<std::streamsize>(surface_image.size()));
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
              "light(index px py pz dx dy dz color) x2 | surface(class features) ind/texgens/stages "
              "env-stage/combiner chan0 chan1\n";
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
            std::snprintf(words, sizeof(words), " | %u %02x %06x %08x %08x %08x",
                          (info.surface & kRtDrawClassMask) >> kRtDrawClassShift, info.shading[0] >> 24u,
                          info.shading[0] & 0xFFFFFFu, info.shading[1], info.shading[2], info.shading[3]);
            report << words;
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
                  "max-triangles=%llu capacity=%u build-ms=%.3f validate-ms=%.3f light-ms=%.3f point-lights=%.2f composites=%llu captures=%llu validated=%llu "
                  "geometry-pixels=%.2f%% match-0.1%%=%.3f%% match-1%%=%.3f%% hit-nearer=%.3f%% hit-farther=%.3f%% "
                  "miss=%.3f%% background-hit=%.3f%% glow-draws=%.1f glow-triangles=%.0f\n",
                  static_cast<unsigned long long>(window_.scenes), static_cast<unsigned long long>(window_.skipped),
                  per(static_cast<double>(window_.draws), window_.scenes),
                  per(static_cast<double>(window_.triangles), window_.scenes),
                  static_cast<unsigned long long>(window_.max_triangles), capacity_,
                  per(window_.build_ms, window_.timed), per(window_.validate_ms, window_.validated),
                  per(window_.light_ms, window_.timed), per(static_cast<double>(window_.point_lights), window_.scenes),
                  static_cast<unsigned long long>(window_.composites),
                  static_cast<unsigned long long>(window_.captures),
                  static_cast<unsigned long long>(window_.validated), pct(2u, 0u), pct(3u, 2u), pct(4u, 2u),
                  pct(5u, 2u), pct(6u, 2u), pct(7u, 2u), pct(8u, 1u),
                  per(static_cast<double>(window_.glow_draws), window_.captures),
                  per(static_cast<double>(window_.glow_triangles), window_.captures));
    std::cerr << line;
    window_ = {};
}

void SceneRayTracer::begin_scene() noexcept {
    light_tex_ready_ = false;
    draws_.clear();
    glow_draws_.clear();
    glow_triangles_ = 0u;
    glow_open_ = false;
    glow_base_valid_ = false;
    draw_colors_.clear();
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
    if ((info.surface & kRtDrawGlow) != 0u) {
        if (glow_open_ && draw.triangle_count != 0u && glow_triangles_ + draw.triangle_count <= kGlowMaxTriangles) {
            RtDraw& entry = glow_draws_.emplace_back(draw);
            entry.first_triangle = glow_triangles_;
            glow_triangles_ += draw.triangle_count;
        }
        return;
    }
    if (!scene_open_ || draw.triangle_count == 0u || triangles_ + draw.triangle_count > kMaxTriangles) {
        return;
    }
    RtDraw& entry = draws_.emplace_back(draw);
    entry.first_triangle = triangles_;
    entry.flags = (entry.flags & 0xFFFFu) |
                  (static_cast<std::uint32_t>(std::min<std::size_t>(draws_.size() - 1u, 0xFFFFu)) << 16u);
    triangles_ += draw.triangle_count;
    draw_colors_.push_back({info.ambient, info.light_index[0] == 0u ? info.light_color[0] : 0u, 0u, 0u});
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

bool SceneRayTracer::sun_direction(std::array<float, 3>& world, float* distance) const noexcept {
    if (sun_override_) {
        world = sun_direction_;
        if (distance != nullptr) {
            *distance = 0.0f;
        }
        return true;
    }
    return game_sun(world, sun_held_ ? 86'400'000u : 2000u, distance);
}

void SceneRayTracer::set_key_light(bool valid, const std::array<float, 3>& world, const std::array<float, 3>& color,
                                   float strength, bool shown) noexcept {
    key_valid_ = valid;
    key_shown_ = shown;
    key_direction_ = world;
    // Colour at its peak 1, as the sun's.
    const float peak = std::max({color[0], color[1], color[2]});
    key_color_ = peak > 1e-3f ? std::array<float, 3>{color[0] / peak, color[1] / peak, color[2] / peak}
                              : std::array<float, 3>{1.0f, 1.0f, 1.0f};
    key_strength_ = std::clamp(strength, 0.0f, 1.0f);
}

bool SceneRayTracer::sky_sun(std::array<float, 3>& world, std::array<float, 3>& color) const noexcept {
    if (!sky_sun_.valid) {
        return false;
    }
    world = sky_sun_.direction;
    color = sky_sun_.color;
    return true;
}

bool SceneRayTracer::sky_key(std::array<float, 3>& world, float& strength) const noexcept {
    static const bool enabled = env_float("GALAXY_RT_SKY_KEY", 1.0f) != 0.0f;
    if (!enabled || !sky_key_.valid) {
        return false;
    }
    world = sky_key_.direction;
    strength = sky_key_.strength;
    return true;
}

void SceneRayTracer::reset_sky() noexcept {
    sky_map_.reset();
    sky_sun_ = {};
    sky_key_ = {};
    ++sky_generation_;
}

// The captures' sky bins once the GPU has written them: into the map, then the sun it shows
// (every fourth read). GALAXY_TRACE_LIGHTS: the search every 120th read and each change of the
// sun, with the TSC cycles of all reads so far; GALAXY_RT_SKY_DUMP=<dir>: the map every 600th read
// as an equirectangular image (+y up, unseen bins dark red, the sun a cross).
void SceneRayTracer::read_sky() {
    constexpr UINT64 kSkyBytes = kSkyBins * kSkyBinWords * 4u;
    static const bool trace = env_float("GALAXY_TRACE_LIGHTS", 0.0f) != 0.0f;
    const std::uint64_t start = trace ? __rdtsc() : 0u;
    bool added = false;
    for (unsigned i = 0; i < kSkyRing; ++i) {
        SkyRead& read = sky_reads_[i];
        if (!read.pending || read.frame + kResultDelay > frame_index_) {
            continue;
        }
        read.pending = false;
        const D3D12_RANGE range{i * kSkyBytes, (i + 1u) * kSkyBytes};
        void* mapped = nullptr;
        if (read.generation != sky_generation_ || FAILED(sky_readback_->Map(0, &range, &mapped))) {
            continue;
        }
        sky_map_.add(reinterpret_cast<const std::uint32_t*>(static_cast<const std::byte*>(mapped) + range.Begin));
        const D3D12_RANGE no_write{0, 0};
        sky_readback_->Unmap(0, &no_write);
        added = true;
    }
    if (!added) {
        return;
    }
    if (++sky_reads_done_ % 4u != 0u) {
        sky_cycles_ += start != 0u ? __rdtsc() - start : 0u;
        return;
    }
    const SkySun before = sky_sun_;
    sky_map_.update(sky_sun_);
    sky_key_ = sky_map_.key();
    sky_cycles_ += start != 0u ? __rdtsc() - start : 0u;
    const bool changed = sky_sun_.valid != before.valid ||
                         (sky_sun_.valid && sky_sun_.direction[0] * before.direction[0] +
                                                    sky_sun_.direction[1] * before.direction[1] +
                                                    sky_sun_.direction[2] * before.direction[2] < 0.9994f);
    if (trace && (changed || sky_reads_done_ % 120u == 0u)) [[unlikely]] {
        const SkySun now = sky_map_.find();
        std::fprintf(stderr,
                     "[rt-sky] reads=%llu cycles=%llu bins=%u peak=%.3f distant=%.3f rest=%.3f found=%d "
                     "sun=%d dir=(%.3f %.3f %.3f) color=(%.2f %.2f %.2f)%s\n",
                     static_cast<unsigned long long>(sky_reads_done_), static_cast<unsigned long long>(sky_cycles_),
                     now.bins, now.peak, now.distant, now.rest, now.valid ? 1 : 0, sky_sun_.valid ? 1 : 0,
                     sky_sun_.direction[0], sky_sun_.direction[1],
                     sky_sun_.direction[2], sky_sun_.color[0], sky_sun_.color[1], sky_sun_.color[2],
                     changed ? " changed" : "");
    }
    static const std::string dump = [] {
        char value[MAX_PATH]{};
        std::size_t length = 0u;
        return getenv_s(&length, value, sizeof(value), "GALAXY_RT_SKY_DUMP") == 0 && length > 1u ? std::string(value)
                                                                                                : std::string();
    }();
    if (!dump.empty() && sky_reads_done_ % 600u == 0u) [[unlikely]] {
        constexpr unsigned kWidth = 256u;
        constexpr unsigned kHeight = 128u;
        constexpr float kPi = 3.14159265f;
        std::vector<std::uint8_t> image(kWidth * kHeight * 3u, 0u);
        for (unsigned y = 0; y < kHeight; ++y) {
            const float lat = kPi * 0.5f - (static_cast<float>(y) + 0.5f) / kHeight * kPi;
            for (unsigned x = 0; x < kWidth; ++x) {
                const float lon = (static_cast<float>(x) + 0.5f) / kWidth * 2.0f * kPi - kPi;
                const std::array<float, 3> w{std::cos(lat) * std::sin(lon), std::sin(lat), -std::cos(lat) * std::cos(lon)};
                const unsigned bin = sky_bin(w);
                std::uint8_t* px = image.data() + (y * kWidth + x) * 3u;
                if (!sky_map_.seen(bin)) {
                    px[0] = 60u;
                    continue;
                }
                for (unsigned i = 0; i < 3u; ++i) {
                    px[i] = static_cast<std::uint8_t>(std::clamp(sky_map_.color(bin)[i], 0.0f, 1.0f) * 255.0f + 0.5f);
                }
            }
        }
        if (sky_sun_.valid) {
            const auto& d = sky_sun_.direction;
            const float lat = std::asin(std::clamp(d[1], -1.0f, 1.0f));
            const float lon = std::atan2(d[0], -d[2]);
            const int cx = static_cast<int>((lon + kPi) / (2.0f * kPi) * kWidth);
            const int cy = static_cast<int>((kPi * 0.5f - lat) / kPi * kHeight);
            for (int k = -6; k <= 6; ++k) {
                for (const auto& [x, y] : {std::pair<int, int>{cx + k, cy}, std::pair<int, int>{cx, cy + k}}) {
                    if (x >= 0 && y >= 0 && x < static_cast<int>(kWidth) && y < static_cast<int>(kHeight)) {
                        std::uint8_t* px = image.data() + (static_cast<unsigned>(y) * kWidth + static_cast<unsigned>(x)) * 3u;
                        px[0] = 255u;
                        px[1] = 0u;
                        px[2] = 0u;
                    }
                }
            }
        }
        std::ofstream file(dump + "\\sky_" + std::to_string(frame_index_) + ".ppm", std::ios::binary);
        file << "P6\n" << kWidth << ' ' << kHeight << "\n255\n";
        file.write(reinterpret_cast<const char*>(image.data()), static_cast<std::streamsize>(image.size()));
    }
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
    for (UINT i = 0; i <= kLightSky - kLightPrevZ; ++i) {
        params[kLightPrevZ + i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[kLightPrevZ + i].Descriptor.ShaderRegister = 8u + i;
    }
    for (UINT i = 0; i <= kLightPrimary - kLightZ; ++i) {
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
        !compute(light_root_.Get(), g_rt_spatial_m_cs, sizeof(g_rt_spatial_m_cs), spatial_m_pso_) ||
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
    capture_params[kCaptureGlowBase].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    capture_params[kCaptureGlowBase].Descriptor.ShaderRegister = 3u;
    capture_params[kCapturePicture].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    capture_params[kCaptureSums].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    capture_params[kCaptureSums].Descriptor.ShaderRegister = 1u;
    capture_params[kCaptureGlow].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    capture_params[kCaptureGlow].Descriptor.ShaderRegister = 2u;
    capture_params[kCaptureFound].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    capture_params[kCaptureFound].Descriptor.ShaderRegister = 3u;
    capture_params[kCaptureSky].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    capture_params[kCaptureSky].Descriptor.ShaderRegister = 4u;
    capture_params[kCaptureSkyRadiance].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    capture_params[kCaptureSkyRadiance].Descriptor.ShaderRegister = 5u;
    capture_params[kCaptureSkySh].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    capture_params[kCaptureSkySh].Descriptor.ShaderRegister = 6u;
    for (auto& param : capture_params) {
        param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
    D3D12_ROOT_SIGNATURE_DESC capture_rsd{};
    capture_rsd.NumParameters = kCaptureCount;
    capture_rsd.pParameters = capture_params;
    if (!serialize(capture_rsd, capture_root_) ||
        !compute(capture_root_.Get(), g_rt_capture_cs, sizeof(g_rt_capture_cs), capture_pso_) ||
        !compute(capture_root_.Get(), g_rt_glow_base_cs, sizeof(g_rt_glow_base_cs), glow_base_pso_) ||
        !compute(capture_root_.Get(), g_rt_sky_state_cs, sizeof(g_rt_sky_state_cs), sky_state_pso_) ||
        !compute(capture_root_.Get(), g_rt_surface_emit_cs, sizeof(g_rt_surface_emit_cs), surface_emit_pso_) ||
        !compute(capture_root_.Get(), g_rt_sky_light_map_cs, sizeof(g_rt_sky_light_map_cs), sky_light_map_pso_) ||
        !compute(capture_root_.Get(), g_rt_sky_light_sh_cs, sizeof(g_rt_sky_light_sh_cs), sky_light_sh_pso_)) {
        return false;
    }

    D3D12_ROOT_PARAMETER emit_params[kEmitCount]{};
    emit_params[kEmitConstants].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    emit_params[kEmitConstants].Constants.Num32BitValues = sizeof(RtEmitConstants) / 4u;
    emit_params[kEmitSources].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    emit_params[kEmitSources].DescriptorTable.NumDescriptorRanges = 1u;
    emit_params[kEmitSources].DescriptorTable.pDescriptorRanges = &sources_range;
    for (UINT i = 0; i < 3u; ++i) {
        emit_params[kEmitDraws + i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        emit_params[kEmitDraws + i].Descriptor.ShaderRegister = 2u + i;
        emit_params[kEmitTiles + i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        emit_params[kEmitTiles + i].Descriptor.ShaderRegister = i;
    }
    for (auto& param : emit_params) {
        param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
    D3D12_ROOT_SIGNATURE_DESC emit_rsd{};
    emit_rsd.NumParameters = kEmitCount;
    emit_rsd.pParameters = emit_params;
    if (!serialize(emit_rsd, emit_root_) ||
        !compute(emit_root_.Get(), g_rt_glow_emit_cs, sizeof(g_rt_glow_emit_cs), glow_emit_pso_) ||
        !compute(emit_root_.Get(), g_rt_emit_merge_cs, sizeof(g_rt_emit_merge_cs), emit_merge_pso_)) {
        return false;
    }

    {
        D3D12_ROOT_PARAMETER bloom_params[4]{};
        bloom_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        bloom_params[0].Constants.Num32BitValues = sizeof(RtBloomConstants) / 4u;
        bloom_params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        bloom_params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        bloom_params[2].Descriptor.ShaderRegister = 1u;
        bloom_params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        for (auto& param : bloom_params) {
            param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        }
        D3D12_ROOT_SIGNATURE_DESC bloom_rsd{};
        bloom_rsd.NumParameters = 4u;
        bloom_rsd.pParameters = bloom_params;
        if (!serialize(bloom_rsd, bloom_root_) ||
            !compute(bloom_root_.Get(), g_rt_bloom_down_cs, sizeof(g_rt_bloom_down_cs), bloom_down_pso_) ||
            !compute(bloom_root_.Get(), g_rt_bloom_blur_cs, sizeof(g_rt_bloom_blur_cs), bloom_blur_pso_)) {
            return false;
        }
        D3D12_ROOT_PARAMETER composite_bloom_params[2]{};
        composite_bloom_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        composite_bloom_params[0].Constants.Num32BitValues = sizeof(RtBloomCompositeConstants) / 4u;
        composite_bloom_params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        for (auto& param : composite_bloom_params) {
            param.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        }
        D3D12_ROOT_SIGNATURE_DESC composite_bloom_rsd{};
        composite_bloom_rsd.NumParameters = 2u;
        composite_bloom_rsd.pParameters = composite_bloom_params;
        if (!serialize(composite_bloom_rsd, bloom_composite_root_)) {
            return false;
        }
        D3D12_GRAPHICS_PIPELINE_STATE_DESC additive{};
        additive.pRootSignature = bloom_composite_root_.Get();
        additive.VS = {g_rt_composite_vs, sizeof(g_rt_composite_vs)};
        additive.PS = {g_rt_bloom_ps, sizeof(g_rt_bloom_ps)};
        D3D12_RENDER_TARGET_BLEND_DESC& add = additive.BlendState.RenderTarget[0];
        add.BlendEnable = TRUE;
        add.SrcBlend = D3D12_BLEND_ONE;
        add.DestBlend = D3D12_BLEND_ONE;
        add.BlendOp = D3D12_BLEND_OP_ADD;
        add.SrcBlendAlpha = D3D12_BLEND_ZERO;
        add.DestBlendAlpha = D3D12_BLEND_ONE;
        add.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        add.LogicOp = D3D12_LOGIC_OP_NOOP;
        add.RenderTargetWriteMask =
            D3D12_COLOR_WRITE_ENABLE_RED | D3D12_COLOR_WRITE_ENABLE_GREEN | D3D12_COLOR_WRITE_ENABLE_BLUE;
        additive.SampleMask = UINT_MAX;
        additive.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        additive.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        additive.RasterizerState.DepthClipEnable = TRUE;
        additive.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        additive.NumRenderTargets = 1u;
        additive.RTVFormats[0] = color_format;
        additive.SampleDesc.Count = 1u;
        if (FAILED(device_->CreateGraphicsPipelineState(&additive, IID_PPV_ARGS(&bloom_composite_pso_)))) {
            return false;
        }
    }

    {
        D3D12_ROOT_PARAMETER post_params[5]{};
        post_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        post_params[0].Constants.Num32BitValues = sizeof(RtPostConstants) / 4u;
        post_params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        post_params[1].DescriptorTable.NumDescriptorRanges = 1u;
        post_params[1].DescriptorTable.pDescriptorRanges = &sources_range;
        post_params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        post_params[2].Descriptor.ShaderRegister = 2u;
        post_params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        post_params[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        post_params[4].Descriptor.ShaderRegister = 1u;
        for (auto& param : post_params) {
            param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        }
        D3D12_ROOT_SIGNATURE_DESC post_rsd{};
        post_rsd.NumParameters = 5u;
        post_rsd.pParameters = post_params;
        if (!serialize(post_rsd, post_root_) ||
            !compute(post_root_.Get(), g_rt_post_copy_cs, sizeof(g_rt_post_copy_cs), post_copy_pso_) ||
            !compute(post_root_.Get(), g_rt_post_bright_cs, sizeof(g_rt_post_bright_cs), post_bright_pso_) ||
            !compute(post_root_.Get(), g_rt_post_blur_cs, sizeof(g_rt_post_blur_cs), post_blur_pso_) ||
            !compute(post_root_.Get(), g_rt_post_shafts_cs, sizeof(g_rt_post_shafts_cs), post_shafts_pso_) ||
            !compute(post_root_.Get(), g_rt_post_focus_cs, sizeof(g_rt_post_focus_cs), post_focus_pso_)) {
            return false;
        }
        D3D12_DESCRIPTOR_RANGE finish_depth{};
        finish_depth.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        finish_depth.NumDescriptors = 1u;
        finish_depth.BaseShaderRegister = 1u;
        D3D12_ROOT_PARAMETER finish_params[6]{};
        finish_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        finish_params[0].Constants.Num32BitValues = sizeof(RtPostConstants) / 4u;
        finish_params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        finish_params[1].DescriptorTable.NumDescriptorRanges = 1u;
        finish_params[1].DescriptorTable.pDescriptorRanges = &finish_depth;
        for (UINT i = 0; i < 4u; ++i) {
            finish_params[2 + i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
            finish_params[2 + i].Descriptor.ShaderRegister = 2u + i;
        }
        for (auto& param : finish_params) {
            param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        }
        D3D12_ROOT_SIGNATURE_DESC finish_rsd{};
        finish_rsd.NumParameters = 6u;
        finish_rsd.pParameters = finish_params;
        if (!serialize(finish_rsd, post_finish_root_)) {
            return false;
        }
        D3D12_GRAPHICS_PIPELINE_STATE_DESC finish{};
        finish.pRootSignature = post_finish_root_.Get();
        finish.VS = {g_rt_post_vs, sizeof(g_rt_post_vs)};
        finish.PS = {g_rt_post_ps, sizeof(g_rt_post_ps)};
        finish.BlendState.RenderTarget[0].RenderTargetWriteMask =
            D3D12_COLOR_WRITE_ENABLE_RED | D3D12_COLOR_WRITE_ENABLE_GREEN | D3D12_COLOR_WRITE_ENABLE_BLUE;
        finish.SampleMask = UINT_MAX;
        finish.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        finish.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        finish.RasterizerState.DepthClipEnable = TRUE;
        finish.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        finish.NumRenderTargets = 1u;
        finish.RTVFormats[0] = color_format;
        finish.SampleDesc.Count = 1u;
        if (FAILED(device_->CreateGraphicsPipelineState(&finish, IID_PPV_ARGS(&post_finish_pso_)))) {
            return false;
        }
    }

    {
        D3D12_ROOT_PARAMETER light_tex_params[2]{};
        light_tex_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        light_tex_params[0].Constants.Num32BitValues = 4u;
        light_tex_params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        for (auto& param : light_tex_params) {
            param.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        }
        D3D12_ROOT_SIGNATURE_DESC light_tex_rsd{};
        light_tex_rsd.NumParameters = 2u;
        light_tex_rsd.pParameters = light_tex_params;
        if (!serialize(light_tex_rsd, light_tex_root_)) {
            return false;
        }
        D3D12_GRAPHICS_PIPELINE_STATE_DESC light_tex{};
        light_tex.pRootSignature = light_tex_root_.Get();
        light_tex.VS = {g_rt_composite_vs, sizeof(g_rt_composite_vs)};
        light_tex.PS = {g_rt_light_texture_ps, sizeof(g_rt_light_texture_ps)};
        light_tex.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        light_tex.SampleMask = UINT_MAX;
        light_tex.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        light_tex.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        light_tex.RasterizerState.DepthClipEnable = TRUE;
        light_tex.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        light_tex.NumRenderTargets = 1u;
        light_tex.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
        light_tex.SampleDesc.Count = 1u;
        if (FAILED(device_->CreateGraphicsPipelineState(&light_tex, IID_PPV_ARGS(&light_tex_pso_)))) {
            return false;
        }
        D3D12_DESCRIPTOR_HEAP_DESC rtv_heap{};
        rtv_heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtv_heap.NumDescriptors = 1u;
        D3D12_DESCRIPTOR_HEAP_DESC srv_heap{};
        srv_heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srv_heap.NumDescriptors = 1u;
        if (FAILED(device_->CreateDescriptorHeap(&rtv_heap, IID_PPV_ARGS(&light_tex_rtv_heap_))) ||
            FAILED(device_->CreateDescriptorHeap(&srv_heap, IID_PPV_ARGS(&light_tex_srv_heap_)))) {
            return false;
        }
        light_tex_srv_ = light_tex_srv_heap_->GetCPUDescriptorHandleForHeapStart();
    }

    D3D12_DESCRIPTOR_RANGE composite_depth{};
    composite_depth.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    composite_depth.NumDescriptors = 1u;
    composite_depth.BaseShaderRegister = 1u;
    D3D12_DESCRIPTOR_RANGE composite_color{};
    composite_color.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    composite_color.NumDescriptors = 1u;
    composite_color.BaseShaderRegister = 3u;
    D3D12_ROOT_PARAMETER composite_params[5]{};
    composite_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    composite_params[0].Constants.Num32BitValues = sizeof(RtCompositeConstants) / 4u;
    composite_params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    composite_params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    composite_params[2].DescriptorTable.NumDescriptorRanges = 1u;
    composite_params[2].DescriptorTable.pDescriptorRanges = &composite_depth;
    composite_params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    composite_params[3].Descriptor.ShaderRegister = 2u;
    composite_params[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    composite_params[4].DescriptorTable.NumDescriptorRanges = 1u;
    composite_params[4].DescriptorTable.pDescriptorRanges = &composite_color;
    for (auto& param : composite_params) {
        param.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    }
    D3D12_ROOT_SIGNATURE_DESC composite_rsd{};
    composite_rsd.NumParameters = 5u;
    composite_rsd.pParameters = composite_params;
    if (!serialize(composite_rsd, composite_root_)) {
        return false;
    }
    D3D12_GRAPHICS_PIPELINE_STATE_DESC graphics{};
    graphics.pRootSignature = composite_root_.Get();
    graphics.VS = {g_rt_composite_vs, sizeof(g_rt_composite_vs)};
    graphics.PS = {g_rt_composite_ps, sizeof(g_rt_composite_ps)};
    // The lit colour over the EFB's (read from a copy); the destination alpha the game may read
    // stays.
    D3D12_RENDER_TARGET_BLEND_DESC& blend = graphics.BlendState.RenderTarget[0];
    blend.BlendEnable = FALSE;
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
                       D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, picture_sums_, L"rt picture sums") ||
        !create_buffer(static_cast<std::uint64_t>(kGlowMaxTriangles) * 36u, D3D12_HEAP_TYPE_DEFAULT,
                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                       glow_positions_, L"rt glow triangles") ||
        !create_buffer(static_cast<std::uint64_t>(kGlowMaxTriangles) * 16u, D3D12_HEAP_TYPE_DEFAULT,
                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                       glow_surfaces_, L"rt glow surfaces") ||
        !create_buffer(16u + kEmitMaxTiles * 32u, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                       D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, emit_tiles_, L"rt glow lights") ||
        !create_buffer(kEmitLights * 32u, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                       D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, emit_lights_, L"rt emitters") ||
        !create_buffer(16u + kEmitLights * 32u, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                       D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, emit_list_, L"rt emitter list")) {
        return false;
    }
    glow_positions_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    emit_tiles_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    emit_list_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    emit_seen_ = false;
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
    retire(light_primary_);
    for (auto& buffer : light_history_) retire(buffer);
    for (auto& buffer : light_final_) retire(buffer);
    retire(picture_);
    retire(glow_base_);
    retire(glow_added_);
    glow_base_valid_ = false;
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
    bool ok = n != 0u && make(n * 4u, light_normal_, L"rt light normal") && make(n * 16u, light_raw_, L"rt light raw") &&
              make(n * 8u, light_primary_, L"rt light primary") && make(n * 8u, picture_, L"rt picture") &&
              make(n * 4u, glow_base_, L"rt glow base") && make(n * 4u, glow_added_, L"rt glow added");
    glow_base_state_ = glow_added_state_ = state;
    for (unsigned i = 0; ok && i < 2u; ++i) {
        ok = make(n * 4u, light_z_[i], L"rt light eye z") && make(n * 16u, light_history_[i], L"rt light history");
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
        light_primary_.Reset();
        for (auto& buffer : light_history_) buffer.Reset();
        for (auto& buffer : light_final_) buffer.Reset();
        picture_.Reset();
        glow_base_.Reset();
        glow_added_.Reset();
        return false;
    }
    light_samples_ = samples;
    std::cerr << "[rt] light samples=" << samples << " mb=" << ((n * (4u + 16u + 8u + 8u + 2u * 4u + 2u * 16u + 12u * kLightSlots)) >> 20u)
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
    // Per draw (RtDraw.flags bits 16-31): its ambient and GX light 0 colours (RGBA8), 2 spare words.
    const std::size_t draw_count = std::max<std::size_t>(draw_colors_.size(), 1u);
    const RtUpload draw_table = allocate(allocate_context, draw_count * sizeof(draw_colors_[0]), 256u);
    if (upload.cpu == nullptr || draw_table.cpu == nullptr) {
        return;
    }
    if (draw_colors_.empty()) {
        std::memset(draw_table.cpu, 0, sizeof(draw_colors_[0]));
    } else {
        std::memcpy(draw_table.cpu, draw_colors_.data(), draw_colors_.size() * sizeof(draw_colors_[0]));
    }
    RtLightConstants c{};
    c.draw_count = static_cast<std::uint32_t>(draw_count);
    c.sky_ibl = sky_ibl_;
    c.point_saturation = point_saturation_;
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
        // The game's visible sun (GALAXY_RT_SUN overrides it), else the scene's key light (the sun
        // its sky shows or the world-fixed light its lit draws take, set_key_light); without
        // either, no sun light. The
        // sun's colour: the game's world-fixed light from the same direction, else
        // GALAXY_RT_SUN_COLOR; the key's: its own.
        std::array<float, 3> l = sun_direction_;
        float sun_distance = 0.0f;
        bool sun_present = sun_direction(l, &sun_distance);
        const bool keyed = !sun_present && key_valid_;
        const float strength = keyed ? key_strength_ : 1.0f;
        if (keyed) {
            l = key_direction_;
            sun_present = true;
            // Halfway to where a BrightSun model would stand (100000 units out): the scene's
            // background sits farther than that.
            sun_distance = 100000.0f;
        }
        // Sun rays end halfway to the game's sun model (BrightSun, drawn that far out along the
        // sun's direction): it would shadow everything near the camera.
        c.sun_reach = sun_distance > 0.0f ? 0.5f * sun_distance : 1.0e6f;
        std::array<float, 3> sun_color = keyed ? key_color_ : sun_color_;
        if (sun_present && !keyed) {
            std::array<GameLight, 16> world{};
            const std::size_t count = game_world_lights(world, 2000u);
            const float sun_length = std::sqrt(l[0] * l[0] + l[1] * l[1] + l[2] * l[2]);
            float best = 0.9f;
            for (std::size_t i = 0; i < count && sun_length > 1e-6f; ++i) {
                const auto& p = world[i].position;
                const float length = std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
                const float alignment = length > 1e-3f ? (p[0] * l[0] + p[1] * l[1] + p[2] * l[2]) / (length * sun_length) : 0.0f;
                const auto& rgb = world[i].color;
                const float peak = std::max({rgb[0], rgb[1], rgb[2]});
                if (alignment > best && peak > 0.05f) {
                    best = alignment;
                    sun_color = {rgb[0] / peak, rgb[1] / peak, rgb[2] / peak};
                }
            }
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
            c.sun_light[i] = sun_present ? sun_color[i] * sun_light_ * strength : 0.0f;
        }
        // A key light the sky does not show has no disc: its shadows are soft.
        c.sun[3] = std::tan((keyed && !key_shown_ ? key_size_ : sun_size_) * 3.14159265f / 180.0f);
        // Without a sun the sky and bounce light carry the whole picture.
        c.sun_light[3] = sun_present ? 1.0f + (sky_light_ - 1.0f) * strength : 1.0f;
    }
    {
        // The game's point lights nearest in light to the camera, in eye space.
        std::array<GameLight, 32> lights{};
        const std::size_t count = game_point_lights(lights, 500u);
        struct Candidate {
            float importance;
            std::array<float, 3> eye;
            std::array<float, 3> color;
            float half;
        };
        std::array<Candidate, 32> candidates{};
        std::size_t used = 0u;
        for (std::size_t i = 0; i < count; ++i) {
            const auto& w = lights[i].position;
            std::array<float, 3> e = w;
            if (camera_known_) {
                for (unsigned r = 0; r < 3u; ++r) {
                    e[r] = camera_[r * 4u] * w[0] + camera_[r * 4u + 1u] * w[1] + camera_[r * 4u + 2u] * w[2] +
                           camera_[r * 4u + 3u];
                }
            } else {
                continue;
            }
            // Its colour less saturated (light mixes towards white on what it reaches); its
            // falloff the game's own (halved at half_distance), else GALAXY_RT_POINT_RADIUS.
            const auto& c0 = lights[i].color;
            const float l0 = 0.2126f * c0[0] + 0.7152f * c0[1] + 0.0722f * c0[2];
            std::array<float, 3> rgb{};
            for (unsigned k = 0; k < 3u; ++k) {
                rgb[k] = l0 + (c0[k] - l0) * point_saturation_;
            }
            const float half = lights[i].half_distance > 1.0f ? lights[i].half_distance : point_radius_;
            const float distance2 = e[0] * e[0] + e[1] * e[1] + e[2] * e[2];
            const float importance = l0 / (1.0f + distance2 / (144.0f * half * half));
            candidates[used++] = {importance, e, rgb, half};
        }
        // The star core: the brightest light of the Comet Observatory, growing with it.
        {
            std::array<float, 3> core{};
            float scale = 0.0f;
            if (camera_known_ && core_light_ > 0.0f && used < candidates.size() && star_core(core, scale, 500u)) {
                std::array<float, 3> e{};
                for (unsigned r = 0; r < 3u; ++r) {
                    e[r] = camera_[r * 4u] * core[0] + camera_[r * 4u + 1u] * core[1] + camera_[r * 4u + 2u] * core[2] +
                           camera_[r * 4u + 3u];
                }
                const float grow = std::clamp(0.4f + 0.35f * scale, 0.5f, 2.5f);
                const float l = core_light_ * grow;
                candidates[used++] = {1.0e6f, e, {1.0f * l, 0.72f * l, 0.38f * l}, core_radius_ * scale * 2.5f};
            }
        }
        std::sort(candidates.begin(), candidates.begin() + static_cast<std::ptrdiff_t>(used),
                  [](const Candidate& a, const Candidate& b) { return a.importance > b.importance; });
        c.point_count = static_cast<std::uint32_t>(std::min<std::size_t>(used, 8u));
        c.point_light = point_light_;
        c.point_size = point_size_;
        for (std::uint32_t i = 0; i < c.point_count; ++i) {
            for (unsigned k = 0; k < 3u; ++k) {
                c.points[2u * i][k] = candidates[i].eye[k];
                c.points[2u * i + 1u][k] = candidates[i].color[k];
            }
            // Reach: twelve half distances (1/145 of the light, windowed to 0).
            c.points[2u * i][3] = 12.0f * candidates[i].half;
            c.points[2u * i + 1u][3] = 1.0f / (candidates[i].half * candidates[i].half);
        }
        window_.point_lights += c.point_count;
        // GALAXY_TRACE_LIGHTS=1: the lights of every 240th traced scene (diagnostics).
        static const bool trace_lights = env_float("GALAXY_TRACE_LIGHTS", 0.0f) != 0.0f;
        if (trace_lights && light_frame_ % 240u == 1u) {
            for (std::uint32_t i = 0; i < c.point_count; ++i) {
                std::fprintf(stderr,
                             "[rt-light] point %u eye=(%.0f %.0f %.0f) color=(%.2f %.2f %.2f) half=%.0f importance=%.4f\n",
                             i, candidates[i].eye[0], candidates[i].eye[1], candidates[i].eye[2], candidates[i].color[0],
                             candidates[i].color[1], candidates[i].color[2], candidates[i].half,
                             candidates[i].importance);
            }
            std::array<GameLight, 16> world{};
            const std::size_t world_count = game_world_lights(world, 2000u);
            for (std::size_t i = 0; i < world_count; ++i) {
                std::fprintf(stderr, "[rt-light] world %zu position=(%.0f %.0f %.0f) color=(%.2f %.2f %.2f)\n", i,
                             world[i].position[0], world[i].position[1], world[i].position[2], world[i].color[0],
                             world[i].color[1], world[i].color[2]);
            }
            std::fprintf(stderr, "[rt-light] sun present=%d eye=(%.3f %.3f %.3f) light=(%.2f %.2f %.2f)\n",
                         c.sun_light[0] + c.sun_light[1] + c.sun_light[2] > 0.0f ? 1 : 0, c.sun[0], c.sun[1], c.sun[2],
                         c.sun_light[0], c.sun_light[1], c.sun_light[2]);
        }
    }
    // Earlier frames' eye positions of a point: their camera times the inverse of this one.
    std::array<float, 12> inverse{};
    const bool invertible = camera_known_ && affine_inverse(camera_, inverse);
    const bool history = history_valid_ && invertible && history_camera_known_;
    const bool picture = picture_valid_ && invertible && picture_camera_known_ && picture_width_ == sample_width &&
                         picture_height_ == sample_height && picture_factor_ == factor;
    const std::array<float, 12> reproject = history ? affine_multiply(history_camera_, inverse) : kIdentity34;
    const std::array<float, 12> to_picture = picture ? affine_multiply(picture_camera_, inverse) : kIdentity34;
    c.flags = (history ? 1u : 0u) | (picture ? 2u : 0u) | (show_term_ == 2u ? 4u : 0u) | (show_term_ == 3u ? 8u : 0u) |
              (show_term_ == 4u ? 16u : 0u) | (show_term_ == 5u ? 32u : 0u) |
              (show_term_ == 6u ? 64u : 0u) | (gi_ ? 0u : 128u);
    std::memcpy(c.reproject, reproject.data(), sizeof(c.reproject));
    std::memcpy(c.picture, to_picture.data(), sizeof(c.picture));
    std::memcpy(c.to_eye, camera_.data(), sizeof(c.to_eye));
    c.ambient[0] = night_;
    c.ambient[1] = daylight_;
    c.ambient[2] = glow_boost_;
    c.ambient[3] = sky_distance_;
    std::memcpy(upload.cpu, &c, sizeof(c));

    const unsigned current = light_parity_;
    const unsigned previous = current ^ 1u;
    const unsigned slot = next_light_slot_;
    next_light_slot_ = (next_light_slot_ + 1u) % kLightSlots;
    reflections_.invalidate(slot);
    constexpr D3D12_RESOURCE_STATES kUav = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    constexpr D3D12_RESOURCE_STATES kRead = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    to_state(list, light_z_[current].Get(), light_z_state_[current], kUav);
    to_state(list, light_history_[current].Get(), light_history_state_[current], kUav);
    to_state(list, light_z_[previous].Get(), light_z_state_[previous], kRead);
    to_state(list, light_history_[previous].Get(), light_history_state_[previous], kRead);
    to_state(list, light_final_[slot].Get(), light_final_state_[slot], kUav);
    to_state(list, picture_.Get(), picture_state_, kRead);
    to_state(list, picture_sums_.Get(), picture_sums_state_, kRead);
    to_state(list, positions_.Get(), positions_state_, kRead);
    to_state(list, surfaces_.Get(), surfaces_state_, kRead);
    to_state(list, emit_list_.Get(), emit_list_state_, kRead);
    to_state(list, glow_base_.Get(), glow_base_state_, kRead);
    to_state(list, glow_added_.Get(), glow_added_state_, kRead);
    to_state(list, sky_sh_.Get(), sky_sh_state_, kRead);

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
    list->SetComputeRootShaderResourceView(kLightPositions, positions_->GetGPUVirtualAddress());
    list->SetComputeRootShaderResourceView(kLightSurfaces, surfaces_->GetGPUVirtualAddress());
    list->SetComputeRootShaderResourceView(kLightEmitters, emit_list_->GetGPUVirtualAddress());
    list->SetComputeRootShaderResourceView(kLightGlowBase, glow_base_->GetGPUVirtualAddress());
    list->SetComputeRootShaderResourceView(kLightGlowAdded, glow_added_->GetGPUVirtualAddress());
    list->SetComputeRootShaderResourceView(kLightDraws, draw_table.gpu);
    list->SetComputeRootShaderResourceView(kLightSky, sky_sh_->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(kLightZ, light_z_[current]->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(kLightNormal, light_normal_->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(kLightRaw, light_raw_->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(kLightHistory, light_history_[current]->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(kLightFinal, light_final_[slot]->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(kLightPrimary, light_primary_->GetGPUVirtualAddress());
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
    // The previous frame's history is free after the temporal pass: the middle filter's output.
    to_state(list, light_history_[previous].Get(), light_history_state_[previous], kUav);
    list->SetComputeRootUnorderedAccessView(kLightHistory, light_history_[previous]->GetGPUVirtualAddress());
    list->SetPipelineState(spatial_m_pso_.Get());
    list->Dispatch(groups_x, groups_y, 1u);
    to_state(list, light_history_[previous].Get(), light_history_state_[previous], kRead);
    list->SetComputeRootShaderResourceView(kLightHistoryIn, light_history_[previous]->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(kLightHistory, light_normal_->GetGPUVirtualAddress());
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

void SceneRayTracer::snapshot_glow_base(ID3D12GraphicsCommandList* list, D3D12_GPU_DESCRIPTOR_HANDLE color_depth_table,
                                        std::uint32_t width, std::uint32_t height) {
    if (!glow_open_ || !glow_base_pso_ || list == nullptr || color_depth_table.ptr == 0u || !view_known_ ||
        width == 0u || height == 0u) {
        return;
    }
    const std::uint32_t factor = sample_factor(width, height);
    const std::uint32_t sample_width = (width + factor - 1u) / factor;
    const std::uint32_t sample_height = (height + factor - 1u) / factor;
    if (!ensure_lighting_buffers(sample_width * sample_height)) {
        return;
    }
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
    to_state(list, glow_base_.Get(), glow_base_state_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    list->SetComputeRootSignature(capture_root_.Get());
    list->SetPipelineState(glow_base_pso_.Get());
    list->SetComputeRoot32BitConstants(kCaptureConstants, sizeof(c) / 4u, &c, 0u);
    list->SetComputeRootDescriptorTable(kCaptureSources, color_depth_table);
    list->SetComputeRootUnorderedAccessView(kCaptureGlow, glow_base_->GetGPUVirtualAddress());
    list->Dispatch((sample_width + 7u) / 8u, (sample_height + 7u) / 8u, 1u);
    to_state(list, glow_base_.Get(), glow_base_state_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    glow_base_valid_ = true;
}

void SceneRayTracer::capture_picture(ID3D12GraphicsCommandList* list, const RtSources& sources, Allocate allocate,
                                     void* allocate_context, D3D12_GPU_DESCRIPTOR_HANDLE color_depth_table,
                                     std::uint32_t width, std::uint32_t height) {
    const bool glow = glow_open_ && glow_base_valid_;
    glow_open_ = false;
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
    c.divide = (divide ? 1u : 0u) | (glow ? 2u : 0u);
    c.strength = strength_;
    const bool emit = glow && emitters_active() && emit_merge_pso_ && camera_known_;
    std::array<float, 12> to_world{};
    const bool inverted = camera_known_ && affine_inverse(camera_, to_world);
    const bool surfaces = emit && surface_emission_ > 0.0f && inverted;
    // The sky's colour by world direction, into a free readback.
    SkyRead& sky_read = sky_reads_[sky_next_];
    const bool sky = inverted && !sky_read.pending;
    if (sky) {
        c.divide |= 4u;
        to_state(list, sky_bins_.Get(), sky_bins_state_, D3D12_RESOURCE_STATE_COPY_DEST);
        list->CopyBufferRegion(sky_bins_.Get(), 0u, sky_zero_.Get(), 0u, kSkyBins * kSkyBinWords * 4u);
    }
    to_state(list, sky_bins_.Get(), sky_bins_state_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if (sky_radiance_generation_ != sky_generation_) {
        // A new stage: its sky is unknown until it is seen.
        sky_radiance_generation_ = sky_generation_;
        to_state(list, sky_radiance_.Get(), sky_radiance_state_, D3D12_RESOURCE_STATE_COPY_DEST);
        to_state(list, sky_sh_.Get(), sky_sh_state_, D3D12_RESOURCE_STATE_COPY_DEST);
        list->CopyBufferRegion(sky_radiance_.Get(), 0u, sky_zero_.Get(), 0u, kSkyBins * 16u);
        list->CopyBufferRegion(sky_sh_.Get(), 0u, sky_zero_.Get(), 0u, 128u);
    }
    to_state(list, sky_radiance_.Get(), sky_radiance_state_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    to_state(list, sky_sh_.Get(), sky_sh_state_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    c.surface_gain = surface_emission_;
    c.sky_distance = sky_distance_;
    c.exposure_day = exposure_day_;
    c.exposure_night = exposure_night_;
    c.daylight = daylight_;
    c.exposure = exposure_ ? 1u : 0u;
    std::copy(view_.projection.begin(), view_.projection.end(), c.projection);
    std::memcpy(c.to_world, to_world.data(), sizeof(c.to_world));
    to_state(list, emit_tiles_.Get(), emit_tiles_state_, D3D12_RESOURCE_STATE_COPY_DEST);
    list->CopyBufferRegion(emit_tiles_.Get(), 0u, stats_zero_.Get(), 0u, 16u);
    to_state(list, emit_tiles_.Get(), emit_tiles_state_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    c.split = view_.viewport[0] + split_ * view_.viewport[2];

    to_state(list, picture_.Get(), picture_state_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    to_state(list, picture_sums_.Get(), picture_sums_state_, D3D12_RESOURCE_STATE_COPY_DEST);
    list->CopyBufferRegion(picture_sums_.Get(), 0u, stats_zero_.Get(), 0u, kPictureSumsCleared);
    list->CopyBufferRegion(picture_sums_.Get(), 40u, stats_zero_.Get(), 0u, 8u);
    to_state(list, picture_sums_.Get(), picture_sums_state_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    list->SetComputeRootSignature(capture_root_.Get());
    list->SetPipelineState(capture_pso_.Get());
    list->SetComputeRoot32BitConstants(kCaptureConstants, sizeof(c) / 4u, &c, 0u);
    list->SetComputeRootDescriptorTable(kCaptureSources, color_depth_table);
    to_state(list, glow_base_.Get(), glow_base_state_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    to_state(list, glow_added_.Get(), glow_added_state_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    list->SetComputeRootShaderResourceView(
        kCaptureFinal, divide ? light_final_[static_cast<unsigned>(light_slot_)]->GetGPUVirtualAddress()
                              : light_normal_->GetGPUVirtualAddress());
    list->SetComputeRootShaderResourceView(kCaptureGlowBase, glow_base_->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(kCapturePicture, picture_->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(kCaptureSums, picture_sums_->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(kCaptureGlow, glow_added_->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(kCaptureFound, emit_tiles_->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(kCaptureSky, sky_bins_->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(kCaptureSkyRadiance, sky_radiance_->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(kCaptureSkySh, sky_sh_->GetGPUVirtualAddress());
    list->Dispatch((sample_width + 7u) / 8u, (sample_height + 7u) / 8u, 1u);
    if (sky) {
        // The sky light: the bins into the map over time, then its spherical harmonics.
        const D3D12_RESOURCE_BARRIER barrier = uav_barrier(nullptr);
        list->ResourceBarrier(1u, &barrier);
        list->SetPipelineState(sky_light_map_pso_.Get());
        list->Dispatch(kSkyBins / 64u, 1u, 1u);
        list->ResourceBarrier(1u, &barrier);
        list->SetPipelineState(sky_light_sh_pso_.Get());
        list->Dispatch(1u, 1u, 1u);
        list->ResourceBarrier(1u, &barrier);
        list->SetPipelineState(capture_pso_.Get());
        constexpr UINT64 kSkyBytes = kSkyBins * kSkyBinWords * 4u;
        to_state(list, sky_bins_.Get(), sky_bins_state_, D3D12_RESOURCE_STATE_COPY_SOURCE);
        list->CopyBufferRegion(sky_readback_.Get(), sky_next_ * kSkyBytes, sky_bins_.Get(), 0u, kSkyBytes);
        sky_read = {frame_index_, sky_generation_, true};
        sky_next_ = (sky_next_ + 1u) % kSkyRing;
    }
    if (surfaces) {
        // Glowing surfaces (bright, under their own glow): lights at their own depth.
        const D3D12_RESOURCE_BARRIER barrier = uav_barrier(nullptr);
        list->ResourceBarrier(1u, &barrier);
        list->SetPipelineState(surface_emit_pso_.Get());
        list->Dispatch((sample_width + 7u) / 8u, (sample_height + 7u) / 8u, 1u);
    }
    {
        const D3D12_RESOURCE_BARRIER barrier = uav_barrier(picture_sums_.Get());
        list->ResourceBarrier(1u, &barrier);
    }
    list->SetPipelineState(sky_state_pso_.Get());
    list->Dispatch(1u, 1u, 1u);
    if (emitters_active() && emit_merge_pso_ && camera_known_) {
        capture_glow_lights(list, sources, allocate, allocate_context, color_depth_table, width, height, factor,
                            glow);
    }
    bloom_slot_ = -1;
    if (glow && bloom_strength_ > 0.0f && bloom_blur_pso_ && light_slot_ >= 0) {
        // What the glows added, at a quarter of the samples above the threshold, blurred twice
        // across and down: the slot's bloom for this frame and its sub-frames.
        const auto slot = static_cast<unsigned>(light_slot_);
        const std::uint32_t bw = (sample_width + 1u) / 2u;
        const std::uint32_t bh = (sample_height + 1u) / 2u;
        const std::uint64_t entries = static_cast<std::uint64_t>(bw) * bh;
        bool ready = entries <= bloom_entries_;
        if (!ready) {
            retire(bloom_temp_);
            for (auto& buffer : bloom_) retire(buffer);
            bloom_size_ = {};
            ready = create_buffer(entries * 4u, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                  D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, bloom_temp_, L"rt bloom temp");
            for (unsigned i = 0; ready && i < bloom_.size(); ++i) {
                ready = create_buffer(entries * 4u, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, bloom_[i], L"rt bloom");
                bloom_state_[i] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            }
            bloom_temp_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            bloom_entries_ = ready ? entries : 0u;
        }
        if (ready && slot < bloom_.size()) {
            RtBloomConstants b{};
            b.size[0] = bw;
            b.size[1] = bh;
            b.sample_size[0] = sample_width;
            b.sample_size[1] = sample_height;
            b.threshold = bloom_threshold_;
            b.strength = bloom_strength_;
            to_state(list, glow_added_.Get(), glow_added_state_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            to_state(list, bloom_[slot].Get(), bloom_state_[slot], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            to_state(list, bloom_temp_.Get(), bloom_temp_state_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            const D3D12_RESOURCE_BARRIER all_uav = uav_barrier(nullptr);
            list->SetComputeRootSignature(bloom_root_.Get());
            list->SetComputeRoot32BitConstants(0u, sizeof(b) / 4u, &b, 0u);
            list->SetComputeRootShaderResourceView(1u, glow_added_->GetGPUVirtualAddress());
            list->SetComputeRootShaderResourceView(2u, glow_added_->GetGPUVirtualAddress());
            list->SetComputeRootUnorderedAccessView(3u, bloom_[slot]->GetGPUVirtualAddress());
            list->SetPipelineState(bloom_down_pso_.Get());
            list->Dispatch((bw + 7u) / 8u, (bh + 7u) / 8u, 1u);
            for (unsigned pass = 0; pass < 2u; ++pass) {
                ID3D12Resource* from = pass == 0u ? bloom_[slot].Get() : bloom_temp_.Get();
                ID3D12Resource* to = pass == 0u ? bloom_temp_.Get() : bloom_[slot].Get();
                list->ResourceBarrier(1u, &all_uav);
                to_state(list, from, pass == 0u ? bloom_state_[slot] : bloom_temp_state_,
                         D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                to_state(list, to, pass == 0u ? bloom_temp_state_ : bloom_state_[slot],
                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                b.axis = pass;
                list->SetComputeRoot32BitConstants(0u, sizeof(b) / 4u, &b, 0u);
                list->SetComputeRootShaderResourceView(2u, from->GetGPUVirtualAddress());
                list->SetComputeRootUnorderedAccessView(3u, to->GetGPUVirtualAddress());
                list->SetPipelineState(bloom_blur_pso_.Get());
                list->Dispatch((bw + 7u) / 8u, (bh + 7u) / 8u, 1u);
            }
            to_state(list, bloom_[slot].Get(), bloom_state_[slot], kLightRead);
            bloom_size_[slot] = {bw, bh, width, height};
            bloom_slot_ = light_slot_;
        }
    }
    static_cast<void>(emit);
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

void SceneRayTracer::capture_glow_lights(ID3D12GraphicsCommandList* list, const RtSources& sources,
                                         Allocate allocate, void* allocate_context,
                                         D3D12_GPU_DESCRIPTOR_HANDLE color_depth_table, std::uint32_t width,
                                         std::uint32_t height, std::uint32_t factor, bool glow) {
    std::array<float, 12> to_world{};
    if (!affine_inverse(camera_, to_world)) {
        return;
    }
    // A load (no scene for half a second) or a jump of the camera starts a new set of lights.
    const auto now = std::chrono::steady_clock::now();
    const std::array<float, 3> eye{to_world[3], to_world[7], to_world[11]};
    const float jump = std::abs(eye[0] - emit_last_eye_[0]) + std::abs(eye[1] - emit_last_eye_[1]) +
                       std::abs(eye[2] - emit_last_eye_[2]);
    const bool reset = !emit_seen_ || now - emit_last_time_ > std::chrono::milliseconds(500) || jump > 30000.0f;
    emit_seen_ = true;
    emit_last_time_ = now;
    emit_last_eye_ = eye;
    const std::uint32_t glow_draws = glow ? static_cast<std::uint32_t>(glow_draws_.size()) : 0u;
    // The glow draws' eye-space triangles: the geometry pass into the glow buffers.
    RtUpload table{};
    if (glow_draws != 0u) {
        const std::size_t table_bytes = glow_draws_.size() * sizeof(RtDraw);
        table = allocate(allocate_context, table_bytes, 256u);
        RtUpload frame{};
        if (table.cpu == nullptr || !fill_constants(sources, allocate, allocate_context, glow_draws, 0u,
                                                    glow_triangles_, 0u, 0u, 0u, frame)) {
            return;
        }
        std::memcpy(table.cpu, glow_draws_.data(), table_bytes);
        to_state(list, glow_positions_.Get(), glow_positions_state_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
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
        list->SetComputeRootUnorderedAccessView(kRootPositions, glow_positions_->GetGPUVirtualAddress());
        list->SetComputeRootUnorderedAccessView(kRootSurfaces, glow_surfaces_->GetGPUVirtualAddress());
        list->Dispatch((glow_triangles_ + 63u) / 64u, 1u, 1u);
        to_state(list, glow_positions_.Get(), glow_positions_state_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
    RtEmitConstants c{};
    std::copy(view_.projection.begin(), view_.projection.end(), c.projection);
    c.depth[0] = view_.z_scale;
    c.depth[1] = view_.z_offset;
    c.depth[2] = view_.min_depth;
    c.depth[3] = view_.max_depth;
    std::copy(view_.viewport.begin(), view_.viewport.end(), c.viewport);
    c.target_size[0] = width;
    c.target_size[1] = height;
    c.sample_size[0] = (width + factor - 1u) / factor;
    c.sample_size[1] = (height + factor - 1u) / factor;
    std::memcpy(c.to_world, to_world.data(), sizeof(c.to_world));
    std::memcpy(c.to_eye, camera_.data(), sizeof(c.to_eye));
    c.gain = emission_;
    c.near_plane = near_plane(view_);
    c.reset = reset ? 1u : 0u;
    c.factor = factor;
    // The found list was cleared before the picture capture (glowing surfaces are in it).
    {
        const D3D12_RESOURCE_BARRIER barrier = uav_barrier(emit_tiles_.Get());
        list->ResourceBarrier(1u, &barrier);
    }
    to_state(list, emit_list_.Get(), emit_list_state_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    to_state(list, glow_added_.Get(), glow_added_state_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    list->SetComputeRootSignature(emit_root_.Get());
    list->SetComputeRoot32BitConstants(kEmitConstants, sizeof(c) / 4u, &c, 0u);
    list->SetComputeRootDescriptorTable(kEmitSources, color_depth_table);
    list->SetComputeRootShaderResourceView(kEmitDraws, glow_draws != 0u ? table.gpu
                                                                         : glow_positions_->GetGPUVirtualAddress());
    list->SetComputeRootShaderResourceView(kEmitPositions, glow_positions_->GetGPUVirtualAddress());
    list->SetComputeRootShaderResourceView(kEmitAdded, glow_added_->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(kEmitTiles, emit_tiles_->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(kEmitLightsSlot, emit_lights_->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(kEmitList, emit_list_->GetGPUVirtualAddress());
    const D3D12_RESOURCE_BARRIER all_uav = uav_barrier(nullptr);
    if (glow_draws != 0u) {
        list->SetPipelineState(glow_emit_pso_.Get());
        list->Dispatch(glow_draws, 1u, 1u);
        list->ResourceBarrier(1u, &all_uav);
    }
    list->SetPipelineState(emit_merge_pso_.Get());
    list->Dispatch(1u, 1u, 1u);
    to_state(list, emit_list_.Get(), emit_list_state_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    window_.glow_draws += glow_draws;
    window_.glow_triangles += glow ? glow_triangles_ : 0u;
}

void SceneRayTracer::post_prepare(ID3D12GraphicsCommandList* list, D3D12_GPU_DESCRIPTOR_HANDLE color_depth_table,
                                  std::uint32_t width, std::uint32_t height, bool replay) {
    if (!post_active() || !post_finish_pso_ || list == nullptr || color_depth_table.ptr == 0u || !view_known_ ||
        width == 0u || height == 0u) {
        return;
    }
    const std::uint32_t qw = (width + 3u) / 4u;
    const std::uint32_t qh = (height + 3u) / 4u;
    const std::uint64_t pixels = static_cast<std::uint64_t>(width) * height;
    const std::uint64_t quarter = static_cast<std::uint64_t>(qw) * qh;
    if (pixels > post_pixels_ || quarter > post_quarter_) {
        for (auto& buffer : post_buffers_) retire(buffer);
        post_pixels_ = post_quarter_ = 0u;
        bool ok = true;
        for (unsigned i = 0; ok && i < post_buffers_.size(); ++i) {
            const std::uint64_t bytes = i == 0u ? pixels * 4u : i == 5u ? 16u : quarter * 4u;
            ok = create_buffer(bytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                               D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, post_buffers_[i], L"rt post");
            post_states_[i] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        }
        if (!ok) {
            for (auto& buffer : post_buffers_) buffer.Reset();
            return;
        }
        post_pixels_ = pixels;
        post_quarter_ = quarter;
    }
    const std::array<float, 12>& camera = replay && post_camera_known_ ? post_camera_ : camera_;
    const bool camera_known = camera_known_ || (replay && post_camera_known_);
    RtPostConstants c{};
    std::copy(view_.projection.begin(), view_.projection.end(), c.projection);
    c.depth[0] = view_.z_scale;
    c.depth[1] = view_.z_offset;
    c.depth[2] = view_.min_depth;
    c.depth[3] = view_.max_depth;
    std::copy(view_.viewport.begin(), view_.viewport.end(), c.viewport);
    c.size[0] = width;
    c.size[1] = height;
    c.quarter[0] = qw;
    c.quarter[1] = qh;
    {
        std::array<float, 3> core{};
        float scale = 0.0f;
        if (camera_known && star_core(core, scale, 500u)) {
            for (unsigned r = 0; r < 3u; ++r) {
                c.core[r] = camera[r * 4u] * core[0] + camera[r * 4u + 1u] * core[1] + camera[r * 4u + 2u] * core[2] +
                            camera[r * 4u + 3u];
            }
            c.core[3] = core_radius_ * scale;
            c.core_light[0] = 1.0f;
            c.core_light[1] = 0.72f;
            c.core_light[2] = 0.38f;
            c.core_light[3] = std::clamp(0.4f + 0.35f * scale, 0.5f, 2.5f);
        }
        // Shafts from a sun the sky shows: the game's, else the one the sky map found.
        std::array<float, 3> sun{};
        std::array<float, 3> sky_color{};
        if (camera_known && (game_sun(sun, 2000u) || sky_sun(sun, sky_color))) {
            for (unsigned r = 0; r < 3u; ++r) {
                c.sun[r] = camera[r * 4u] * sun[0] + camera[r * 4u + 1u] * sun[1] + camera[r * 4u + 2u] * sun[2];
            }
            c.sun[3] = 0.6f;
        }
    }
    c.time = std::chrono::duration<float>(std::chrono::steady_clock::now() - post_epoch_).count();
    c.bloom = post_bloom_;
    c.threshold = post_threshold_;
    c.shafts = post_shafts_;
    c.dof = post_dof_ * static_cast<float>(width) / 640.0f;
    c.grade = post_grade_;
    c.rim = post_rim_;
    c.split = split_ > 0.0f ? view_.viewport[0] + split_ * view_.viewport[2] : -1.0f;

    ID3D12Resource* const scene = post_buffers_[0].Get();
    ID3D12Resource* const bright = post_buffers_[1].Get();
    ID3D12Resource* const tmp = post_buffers_[2].Get();
    ID3D12Resource* const bloom = post_buffers_[3].Get();
    ID3D12Resource* const shafts = post_buffers_[4].Get();
    ID3D12Resource* const state = post_buffers_[5].Get();
    for (unsigned i = 0; i < post_buffers_.size(); ++i) {
        to_state(list, post_buffers_[i].Get(), post_states_[i], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    const D3D12_RESOURCE_BARRIER all_uav = uav_barrier(nullptr);
    const auto run = [&](ID3D12PipelineState* pso, ID3D12Resource* in, ID3D12Resource* out, std::uint32_t axis,
                         UINT gx, UINT gy) {
        c.axis = axis;
        list->SetComputeRoot32BitConstants(0u, sizeof(c) / 4u, &c, 0u);
        list->SetComputeRootShaderResourceView(2u, in->GetGPUVirtualAddress());
        list->SetComputeRootUnorderedAccessView(3u, out->GetGPUVirtualAddress());
        list->SetPipelineState(pso);
        list->Dispatch(gx, gy, 1u);
    };
    list->SetComputeRootSignature(post_root_.Get());
    list->SetComputeRootDescriptorTable(1u, color_depth_table);
    list->SetComputeRootUnorderedAccessView(4u, state->GetGPUVirtualAddress());
    // The picture before the finish, its bright part, the focus and core radius.
    run(post_copy_pso_.Get(), state, scene, 0u, (width + 7u) / 8u, (height + 7u) / 8u);
    run(post_bright_pso_.Get(), state, bright, 0u, (qw + 7u) / 8u, (qh + 7u) / 8u);
    run(post_focus_pso_.Get(), state, tmp, 0u, 1u, 1u);
    list->ResourceBarrier(1u, &all_uav);
    // Shafts from the bright part; its blur across, then down.
    to_state(list, bright, post_states_[1], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    run(post_shafts_pso_.Get(), bright, shafts, 0u, (qw + 7u) / 8u, (qh + 7u) / 8u);
    run(post_blur_pso_.Get(), bright, tmp, 0u, (qw + 7u) / 8u, (qh + 7u) / 8u);
    list->ResourceBarrier(1u, &all_uav);
    to_state(list, tmp, post_states_[2], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    run(post_blur_pso_.Get(), tmp, bloom, 1u, (qw + 7u) / 8u, (qh + 7u) / 8u);
    list->ResourceBarrier(1u, &all_uav);
    for (unsigned i = 0; i < post_buffers_.size(); ++i) {
        to_state(list, post_buffers_[i].Get(), post_states_[i], kLightRead);
    }
    c.axis = post_view_ ? 2u : 0u;
    std::memcpy(post_constants_.data(), &c, sizeof(c));
}

void SceneRayTracer::post_finish(ID3D12GraphicsCommandList* list, D3D12_CPU_DESCRIPTOR_HANDLE target,
                                 D3D12_GPU_DESCRIPTOR_HANDLE depth_table, std::uint32_t width, std::uint32_t height) {
    if (!post_active() || !post_finish_pso_ || list == nullptr || depth_table.ptr == 0u || post_pixels_ == 0u ||
        static_cast<std::uint64_t>(width) * height > post_pixels_) {
        return;
    }
    list->OMSetRenderTargets(1u, &target, FALSE, nullptr);
    const D3D12_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f};
    const D3D12_RECT scissor{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    list->RSSetViewports(1u, &viewport);
    list->RSSetScissorRects(1u, &scissor);
    list->SetGraphicsRootSignature(post_finish_root_.Get());
    list->SetPipelineState(post_finish_pso_.Get());
    list->SetGraphicsRoot32BitConstants(0u, static_cast<UINT>(post_constants_.size()), post_constants_.data(), 0u);
    list->SetGraphicsRootDescriptorTable(1u, depth_table);
    list->SetGraphicsRootShaderResourceView(2u, post_buffers_[0]->GetGPUVirtualAddress());
    list->SetGraphicsRootShaderResourceView(3u, post_buffers_[3]->GetGPUVirtualAddress());
    list->SetGraphicsRootShaderResourceView(4u, post_buffers_[4]->GetGPUVirtualAddress());
    list->SetGraphicsRootShaderResourceView(5u, post_buffers_[5]->GetGPUVirtualAddress());
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list->DrawInstanced(3u, 1u, 0u, 0u);
}

void SceneRayTracer::write_light_texture(ID3D12GraphicsCommandList* list, int slot) {
    if (list == nullptr || slot < 0 || slot >= static_cast<int>(kLightSlots) || !light_tex_pso_ ||
        !light_final_[static_cast<unsigned>(slot)]) {
        return;
    }
    const LightSlot& info = light_slots_[static_cast<unsigned>(slot)];
    if (info.sample_width == 0u || info.sample_height == 0u) {
        return;
    }
    if (info.sample_width != light_tex_width_ || info.sample_height != light_tex_height_ || !light_tex_) {
        retire(light_tex_);
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = info.sample_width;
        desc.Height = info.sample_height;
        desc.DepthOrArraySize = 1u;
        desc.MipLevels = 1u;
        desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.SampleDesc.Count = 1u;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr,
                                                    IID_PPV_ARGS(&light_tex_)))) {
            light_tex_width_ = light_tex_height_ = 0u;
            return;
        }
        light_tex_->SetName(L"rt light texture");
        device_->CreateRenderTargetView(light_tex_.Get(), nullptr,
                                        light_tex_rtv_heap_->GetCPUDescriptorHandleForHeapStart());
        device_->CreateShaderResourceView(light_tex_.Get(), nullptr, light_tex_srv_);
        light_tex_state_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        light_tex_width_ = info.sample_width;
        light_tex_height_ = info.sample_height;
    }
    to_state(list, light_final_[static_cast<unsigned>(slot)].Get(), light_final_state_[static_cast<unsigned>(slot)],
             kLightRead);
    to_state(list, light_tex_.Get(), light_tex_state_, D3D12_RESOURCE_STATE_RENDER_TARGET);
    const D3D12_CPU_DESCRIPTOR_HANDLE rtv = light_tex_rtv_heap_->GetCPUDescriptorHandleForHeapStart();
    list->OMSetRenderTargets(1u, &rtv, FALSE, nullptr);
    const D3D12_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(light_tex_width_),
                                  static_cast<float>(light_tex_height_), 0.0f, 1.0f};
    const D3D12_RECT scissor{0, 0, static_cast<LONG>(light_tex_width_), static_cast<LONG>(light_tex_height_)};
    list->RSSetViewports(1u, &viewport);
    list->RSSetScissorRects(1u, &scissor);
    list->SetGraphicsRootSignature(light_tex_root_.Get());
    list->SetPipelineState(light_tex_pso_.Get());
    const std::uint32_t constants[4] = {light_tex_width_, light_tex_height_, 0u, 0u};
    list->SetGraphicsRoot32BitConstants(0u, 4u, constants, 0u);
    list->SetGraphicsRootShaderResourceView(1u, light_final_[static_cast<unsigned>(slot)]->GetGPUVirtualAddress());
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list->DrawInstanced(3u, 1u, 0u, 0u);
    to_state(list, light_tex_.Get(), light_tex_state_, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    light_tex_scale_ = 1.0f / static_cast<float>(std::max(info.factor, 1u));
    light_tex_ready_ = true;
}

void SceneRayTracer::bloom(ID3D12GraphicsCommandList* list, D3D12_CPU_DESCRIPTOR_HANDLE target, std::uint32_t width,
                           std::uint32_t height, int slot) {
    if (list == nullptr || slot < 0 || slot >= static_cast<int>(bloom_.size()) || !bloom_composite_pso_) {
        return;
    }
    const auto index = static_cast<unsigned>(slot);
    const auto& size = bloom_size_[index];
    if (!bloom_[index] || size[0] == 0u || size[2] != width || size[3] != height) {
        return;
    }
    to_state(list, bloom_[index].Get(), bloom_state_[index], kLightRead);
    RtBloomCompositeConstants c{};
    c.scale[0] = static_cast<float>(size[0]) / static_cast<float>(width);
    c.scale[1] = static_cast<float>(size[1]) / static_cast<float>(height);
    c.size[0] = size[0];
    c.size[1] = size[1];
    c.strength = bloom_strength_;
    c.split = split_ > 0.0f ? split_ * static_cast<float>(width) : -1.0f;
    list->OMSetRenderTargets(1u, &target, FALSE, nullptr);
    const D3D12_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f};
    const D3D12_RECT scissor{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    list->RSSetViewports(1u, &viewport);
    list->RSSetScissorRects(1u, &scissor);
    list->SetGraphicsRootSignature(bloom_composite_root_.Get());
    list->SetPipelineState(bloom_composite_pso_.Get());
    list->SetGraphicsRoot32BitConstants(0u, sizeof(c) / 4u, &c, 0u);
    list->SetGraphicsRootShaderResourceView(1u, bloom_[index]->GetGPUVirtualAddress());
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list->DrawInstanced(3u, 1u, 0u, 0u);
}

void SceneRayTracer::composite(ID3D12GraphicsCommandList* list, D3D12_CPU_DESCRIPTOR_HANDLE target,
                               D3D12_GPU_DESCRIPTOR_HANDLE depth_srv, D3D12_GPU_DESCRIPTOR_HANDLE color_srv,
                               std::uint32_t width, std::uint32_t height, int slot, bool reproject) {
    if (list == nullptr || slot < 0 || slot >= static_cast<int>(kLightSlots) || !composite_pso_ ||
        depth_srv.ptr == 0u || color_srv.ptr == 0u) {
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
    c.split = split_ > 0.0f ? info.view.viewport[0] + split_ * info.view.viewport[2] : -1.0f;
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
    list->SetGraphicsRootDescriptorTable(4u, color_srv);
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list->DrawInstanced(3u, 1u, 0u, 0u);
    ++window_.composites;
    if (!show_lighting_) {
        reflections_.composite(list, target, depth_srv, width, height, index,
                               reproject ? reproject_->GetGPUVirtualAddress() : 0u, c.split);
    }
}

void SceneRayTracer::trace_reflections(ID3D12GraphicsCommandList* list, Allocate allocate, void* allocate_context,
                                       D3D12_GPU_DESCRIPTOR_HANDLE color_depth_table, std::uint32_t width,
                                       std::uint32_t height) {
    if (!reflections_active() || light_slot_ < 0 || list == nullptr || allocate == nullptr || !tlas_ ||
        !positions_ || !surfaces_ || !light_primary_) {
        return;
    }
    const auto slot = static_cast<unsigned>(light_slot_);
    const LightSlot& light = light_slots_[slot];
    if (light.width != width || light.height != height || !light_final_[slot]) {
        return;
    }
    const RtUpload upload = allocate(allocate_context, RtReflections::kConstantsBytes, 256u);
    if (upload.cpu == nullptr) {
        return;
    }
    // trace_lighting left the geometry readable and its per-sample primary hits (always UAV) written.
    to_state(list, positions_.Get(), positions_state_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    to_state(list, surfaces_.Get(), surfaces_state_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    RtReflectScene scene{};
    scene.projection = light.view.projection;
    scene.depth = {light.view.z_scale, light.view.z_offset, light.view.min_depth, light.view.max_depth};
    scene.viewport = light.view.viewport;
    scene.near_plane = light.near_plane;
    scene.camera = light.camera;
    scene.camera_known = light.camera_known;
    scene.width = width;
    scene.height = height;
    scene.sample_width = light.sample_width;
    scene.sample_height = light.sample_height;
    scene.factor = light.factor;
    scene.tlas = tlas_->GetGPUVirtualAddress();
    scene.positions = positions_->GetGPUVirtualAddress();
    scene.surfaces = surfaces_->GetGPUVirtualAddress();
    scene.primary = light_primary_->GetGPUVirtualAddress();
    scene.light = light_final_[slot]->GetGPUVirtualAddress();
    scene.sums = picture_valid_ ? picture_sums_->GetGPUVirtualAddress() : 0u;
    reflections_.trace(list, scene, slot, upload.cpu, upload.gpu, color_depth_table);
}

void SceneRayTracer::write_reprojection(ID3D12GraphicsCommandList* list, const std::array<float, 12>& camera_t) {
    post_camera_ = camera_t;
    post_camera_known_ = true;
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
    to_state(list, surfaces_.Get(), surfaces_state_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
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
    list->SetComputeRootUnorderedAccessView(kRootSurfaces, surfaces_->GetGPUVirtualAddress());
    list->Dispatch((count + 63u) / 64u, 1u, 1u);
    const D3D12_RESOURCE_BARRIER barrier = uav_barrier(nullptr);
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
    glow_open_ = emitters_active() && emit_merge_pso_ != nullptr;
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
