// SPDX-License-Identifier: GPL-3.0-only
// RtReflections (rt_reflections.h): the reflection and water passes' pipelines, buffers and dispatches.
#include "galaxy/gx/rt_reflections.h"
#include "galaxy/gx/pso_jobs.h"
#include "galaxy/gx/render_config.h"

#include "rt_reflect_composite_ps.h"
#include "rt_reflect_composite_vs.h"
#include "rt_reflect_draw_clear_cs.h"
#include "rt_reflect_draw_cs.h"
#include "rt_reflect_filter_a_cs.h"
#include "rt_reflect_filter_cs.h"
#include "rt_reflect_memory_cs.h"
#include "rt_reflect_sky_cs.h"
#include "rt_reflect_temporal_cs.h"
#include "rt_reflect_trace_cs.h"
#include "rt_reflect_trace_tex_cs.h"
#include "rt_underwater_ps.h"
#include "rt_water_composite_ps.h"
#include "rt_water_trace_cs.h"
#include "rt_water_trace_tex_cs.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <utility>

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
    float classes[10][4];
    float range;
    float lobe;
    std::uint32_t history_frames;
    float fallback;
    float memory_fade;
    float ripple;
    float probe;
    float sky_distance;
    float sun[4];
    float sun_light[4];
    float points[2u * kRtPointLights][4];
    std::uint32_t point_count;
    float point_light;
    float point_size;
    float shafts;
    float sky_fade;
    float sky_average;
    float metal_sky;
    float local_gain;
    std::uint32_t draw_count;
    float rough_cutoff;
    float metal_f0;
    float waves;
    float draw_trust;
    float sky_trust;
    float metal_cover;
    float pad;
    float hit[4];
    std::uint32_t texture_count;
    float night_lights;
    float night_cap;
    float sun_cap;
    std::uint32_t core_point;
    float sky_fill[3];   // RtReflectScene::sky_fill
    // RtReflectScene::physical
    float sky_radiance[4];
    float full_light[4];
    float up[4];
    float light_fit[4];
    std::uint32_t tev_view;
    std::uint32_t emit_view;
    std::uint32_t sky_view;
    std::uint32_t tev_count;
    std::uint32_t emit_count;
    std::uint32_t emit_scene;
    std::uint32_t physical_pad[2];
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
    std::uint32_t classes[10][2];  // strength, F0, GGX alpha, cover (halves); 10 classes, two per row
    float water[4];
    float sun[4];
    float water2[4];   // wave slope, cover, foam, shallow caustics
};
static_assert(sizeof(RtReflectCompositeConstants) == 208u);

std::uint32_t half_pair(float a, float b) {
    const auto half = [](float v) {
        // Non-negative values below 65504: the rounded bits of a float16.
        const std::uint32_t f = std::bit_cast<std::uint32_t>(std::clamp(v, 0.0f, 65504.0f));
        const int exponent = static_cast<int>((f >> 23u) & 0xFFu) - 127 + 15;
        if (exponent <= 0) {
            return 0u;
        }
        const std::uint32_t mantissa = (f & 0x7FFFFFu) + 0x1000u;
        return static_cast<std::uint32_t>(exponent << 10u) + (mantissa >> 13u);
    };
    return half(a) | (half(b) << 16u);
}

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
    kEmitters,
    kInfo,
    kRaw,
    kHistory,
    kFinal,
    kStats,
    kMemory,
    kPrimary,
    kTiles,
    kWater,
    kWaterPositions,
    kWaterSurfaces,
    kUnder,
    kSkyMap,
    kDraws,
    kSurfaceUv,
    kDrawTextures,
    kSky,
    kDrawLight,
    kWaterUv,
    kWaterWaves,
    kParameterCount,
};

enum CompositeParameter : UINT {
    kCompositeConstants = 0,
    kCompositeFinal,
    kCompositeDepth,
    kCompositeReproject,
    kCompositeTiles,
    kCompositeUnder,
    kCompositeScene,
    kCompositeOpaque,
    kCompositeLayer,
    kCompositeCount,
};

// Per result slot: the stats words (the trace's, the water's), then the timestamps (trace,
// composite, water trace, water composite: begin and end each).
constexpr std::uint64_t kReadbackSlotBytes = 256u;
constexpr std::uint64_t kReadbackTicks = 128u;
constexpr D3D12_RESOURCE_STATES kFinalRead =
    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

// fill_constants: this trace starts its accumulation over (never reaches the shader's flags).
constexpr std::uint32_t kNoHistory = 0x80000000u;

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

bool RtReflections::prepare_pipelines(ID3D12Device5* device, DXGI_FORMAT color_format, unsigned quality) {
    RtReflections other;
    other.device_ = device;
    return device != nullptr && other.create_pipelines(color_format, quality);
}

bool RtReflections::initialize(ID3D12Device5* device, std::uint64_t timestamp_frequency, DXGI_FORMAT color_format,
                               unsigned quality) {
    shutdown();
    if (device == nullptr || env_float("GALAXY_RT_REFLECT", 1.0f) == 0.0f) {
        return false;
    }
    device_ = device;
    if (history_device_ != device_) {
        memory_ = {};
        sky_ = {};
        history_device_ = device_;
    }
    // GALAXY_RT_REFLECT_<CLASS>=roughness,F0,strength[,metal[,cover]] per surface class (rt_surfaces.h);
    // GALAXY_RT_WATER the same for translucent water.
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
    // The game's own shading of its shiny surfaces carries a reflection (its environment maps). Physical light takes it
    // out (EngineGBufferCS, the per-pixel record's share) and the traced reflection screens over what remains at full
    // strength (cover 0); GALAXY_RT_PAINTED_REFLECTION=1 keeps the painted one, the traced one at half strength over
    // it. The table's bare metals (class 8) look like their reflection: the traced one takes the painted one's place
    // (cover 1) where it is known, the game's own colour where it is not (ReflectTraceCS), so the lighting's shade
    // never dulls the chrome. Environment maps the table does not name (penguins, glassy domes) stay screened.
    // Translucent water is a surface of water (F0 0.02): little reflection looking down, most of it
    // at grazing angles.
    materials_[0] = material("GALAXY_RT_REFLECT_DIFFUSE", {0.6f, 0.04f, 0.0f, 0.0f, 0.0f});
    materials_[1] = material("GALAXY_RT_REFLECT_GLOSSY", {0.3f, 0.06f, 1.0f, 0.0f, 0.0f});
    const bool painted = env_float("GALAXY_RT_PAINTED_REFLECTION", 0.0f) != 0.0f;
    materials_[2] = material("GALAXY_RT_REFLECT_METAL", {0.12f, 0.9f, painted ? 0.5f : 1.0f, 1.0f, 0.0f});
    materials_[kBareMetalClass] = material("GALAXY_RT_REFLECT_BARE_METAL", {0.12f, 0.9f, 1.0f, 1.0f, 1.0f});
    materials_[3] = material("GALAXY_RT_REFLECT_UNLIT", {0.5f, 0.02f, 0.0f, 0.0f, 0.0f});
    // Opaque indirect-textured surfaces: ice (Freezeflame's rink and walls), pools, swamps: polished
    // and glossy (F0 above water's, so the aurora and Mario read in the ice); a white surface shows its
    // reflection only where the reflection takes its place.
    materials_[4] = material("GALAXY_RT_REFLECT_WATER", {0.04f, 0.25f, 1.0f, 0.0f, 0.8f});
    // A calm, glassy surface (roughness 0.03) of water, F0 0.02 (n 1.333): its Fresnel reflects 2 % looking straight
    // down, 6 % at 60 degrees from the normal, 21 % at 75, 61 % at 85 and all of it at grazing angles, so the near water
    // shows its body and seabed and the far water mirrors the islands, palms and sky. Cover 1: the reflection takes the
    // place of that share of the water's own light (L = F reflected + (1 - F) body). The sky's part is never brighter
    // than the water (WaterTraceCS), so nothing bleaches. A higher F0 or a partial cover lays the bright surroundings
    // over water seen from above, turning it milky, and the waves' glints (scaled by F0) show as glitter.
    materials_[kWaterClass] = material("GALAXY_RT_WATER", {0.03f, 0.02f, 1.0f, 0.0f, 1.0f});
    materials_[kWaterClass + 1u] = materials_[kWaterClass];
    // Crystals, ice, gems and glass (named in rt_surfaces.cpp): clear dielectric, its reflection white and
    // Fresnel-weighted. Cover 1 is the physical split: the reflection takes F of the colour, the rest is the game's own
    // look of the gem (what passes through it and the light it gathers inside). F0 0.35 is above a diamond's (n 2.42:
    // ((n - 1) / (n + 1))^2 = 0.17), at which glass spheres and crystals read matte; much higher turns crystals into
    // dark mirrors of their surroundings. Its rays that leave the scene keep its painted look (ReflectTraceCS,
    // WaterTraceCS), so a strong mirror never shows a dark one of empty space.
    materials_[7] = material("GALAXY_RT_REFLECT_CRYSTAL", {0.05f, 0.35f, 1.0f, 0.0f, 1.0f});
    // Falling water (kFallingWater): water's Fresnel (F0 0.02) on a turbulent sheet, cover 1. Roughness 0.4 (GGX alpha
    // 0.16): a rough sea's slopes spread about as far (Cox and Munk 1954 at 10 m/s, sigma 0.23: alpha about 0.3), less
    // what the flow's streaks carry (rt_reflections.hlsl wave_slope), which move its reflection and glints down it.
    materials_[9] = material("GALAXY_RT_REFLECT_FALLING_WATER", {0.4f, 0.02f, 1.0f, 0.0f, 1.0f});
    view_ = static_cast<std::uint32_t>(std::clamp(env_float("GALAXY_RT_REFLECT_VIEW", 0.0f), 0.0f, 9.0f));
    history_frames_ =
        static_cast<std::uint32_t>(std::clamp(env_float("GALAXY_RT_REFLECT_HISTORY", 24.0f), 1.0f, 64.0f));
    range_ = std::clamp(env_float("GALAXY_RT_REFLECT_RANGE", 50000.0f), 100.0f, 1.0e6f);
    lobe_ = std::clamp(env_float("GALAXY_RT_REFLECT_LOBE", 0.85f), 0.05f, 1.0f);
    fallback_ = std::clamp(env_float("GALAXY_RT_REFLECT_FALLBACK", 0.0f), 0.0f, 1.0f);
    memory_fade_ = std::clamp(env_float("GALAXY_RT_REFLECT_MEMORY_FADE", 0.99f), 0.0f, 1.0f);
    ripple_ = std::clamp(env_float("GALAXY_RT_WATER_RIPPLE", 0.0f), 0.0f, 4.0f);
    probe_ = std::clamp(env_float("GALAXY_RT_REFLECT_PROBE", 0.6f), 0.0f, 1.0f);
    clear_ = std::clamp(env_float("GALAXY_RT_WATER_CLEAR", 1.0f), 0.0f, 1.0f);
    caustics_ = std::clamp(env_float("GALAXY_RT_WATER_CAUSTICS", 1.0f), 0.0f, 4.0f);
    shafts_ = std::clamp(env_float("GALAXY_RT_WATER_SHAFTS", 1.0f), 0.0f, 8.0f);
    sky_fade_ = std::clamp(env_float("GALAXY_RT_REFLECT_SKY_FADE", 0.9995f), 0.0f, 1.0f);
    sky_average_ = std::clamp(env_float("GALAXY_RT_REFLECT_SKY_AVERAGE", 0.15f), 0.0f, 1.0f);
    // The sky's share in a metal's reflection where the frame and the capture do not show it: half where the painted
    // reflection stays under it (GALAXY_RT_PAINTED_REFLECTION=1), all of it where the traced one replaces it.
    metal_sky_ = std::clamp(env_float("GALAXY_RT_REFLECT_METAL_SKY",
                                      env_float("GALAXY_RT_PAINTED_REFLECTION", 0.0f) != 0.0f ? 0.5f : 1.0f),
                            0.0f, 1.0f);
    local_gain_ = std::clamp(env_float("GALAXY_RT_REFLECT_LOCAL_GAIN", 2.0f), 0.0f, 16.0f);
    rough_cutoff_ = std::clamp(env_float("GALAXY_RT_REFLECT_ROUGH_CUTOFF", 0.65f), 0.05f, 1.2f);
    waves_ = std::clamp(env_float("GALAXY_RT_WAVES", 1.0f), 0.0f, 4.0f);
    draw_trust_ = std::clamp(env_float("GALAXY_RT_REFLECT_DRAW_TRUST", 0.75f), 0.0f, 1.0f);
    sky_trust_ = std::clamp(env_float("GALAXY_RT_REFLECT_SKY_TRUST", 1.0f), 0.0f, 1.0f);
    // The water body (the game's water darkened with its depth) is off by default: it brightens the whole picture,
    // not only the water.
    water_cover_ = std::clamp(env_float("GALAXY_RT_WATER_COVER", 0.0f), 0.0f, 1.0f);
    foam_ = std::clamp(env_float("GALAXY_RT_WATER_FOAM", 0.6f), 0.0f, 2.0f);
    shallow_caustics_ = std::clamp(env_float("GALAXY_RT_WATER_SHALLOW_CAUSTICS", 0.6f), 0.0f, 4.0f);
    {
        // Per 1000 eye units (about 10 m of the game's world): red goes first, then green.
        absorb_ = {25.0f, 4.5f, 2.6f};
        char value[64]{};
        std::size_t length = 0u;
        if (getenv_s(&length, value, sizeof(value), "GALAXY_RT_WATER_ABSORB") == 0 && length > 1u) {
            char* cursor = value;
            for (float& f : absorb_) {
                char* end = nullptr;
                const float parsed = std::strtof(cursor, &end);
                if (end == cursor) {
                    break;
                }
                f = std::max(parsed, 0.0f);
                cursor = *end == ',' ? end + 1 : end;
            }
        }
    }
    // Committed resources start zeroed: a new environment memory and captured sky are empty. Kept ones (ray
    // tracing turned off and on within a stage) hold what the stage has shown, so the reflections keep the sky they
    // remember instead of falling back to its average.
    const bool new_memory = memory_.resource == nullptr;
    const bool new_sky = sky_.resource == nullptr;
    if (!create_pipelines(color_format, quality) ||
        (new_memory &&
         !create_buffer(kMemoryTexels * 16u, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, memory_.resource, L"rt reflect memory")) ||
        (new_sky &&
         !create_buffer(static_cast<std::uint64_t>(kSkySize) * kSkySize * 8u, D3D12_HEAP_TYPE_DEFAULT,
                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                        sky_.resource, L"rt reflect sky")) ||
        !create_buffer(kDrawLightEntries * 16u, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                       D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, draw_light_.resource, L"rt reflect draw light") ||
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
    if (new_memory) {
        memory_.state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }
    if (new_sky) {
        sky_.state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }
    draw_light_.state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
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
    char line[512];
    std::snprintf(line, sizeof(line),
                  "[rt] reflections view=%u history=%u range=%.0f lobe=%.2f fade=%.3f | glossy %.2f %.2f %.2f | "
                  "metal %.2f %.2f %.2f %.2f | unlit %.2f %.2f %.2f | water %.2f %.2f %.2f | sea %.2f %.2f %.2f %.2f "
                  "ripple %.2f absorb %.2f %.2f %.2f clear %.2f caustics %.2f | probe %.2f\n",
                  view_, history_frames_, range_, lobe_, memory_fade_, materials_[1].roughness, materials_[1].f0,
                  materials_[1].strength, materials_[2].roughness, materials_[2].f0, materials_[2].strength,
                  materials_[2].cover, materials_[3].roughness, materials_[3].f0, materials_[3].strength,
                  materials_[4].roughness, materials_[4].f0, materials_[4].strength,
                  materials_[kWaterClass].roughness, materials_[kWaterClass].f0, materials_[kWaterClass].strength,
                  materials_[kWaterClass].cover, ripple_, absorb_[0], absorb_[1], absorb_[2], clear_, caustics_,
                  probe_);
    std::cerr << line;
    return true;
}

void RtReflections::shutdown() noexcept {
    ready_ = false;
    root_.Reset();
    trace_pso_.Reset();
    water_trace_pso_.Reset();
    temporal_pso_.Reset();
    filter_a_pso_.Reset();
    filter_pso_.Reset();
    memory_pso_.Reset();
    sky_pso_.Reset();
    draw_clear_pso_.Reset();
    draw_light_pso_.Reset();
    composite_root_.Reset();
    composite_pso_.Reset();
    water_composite_pso_.Reset();
    underwater_pso_.Reset();
    main_ = {};
    water_ = {};
    raw_ = {};
    for (Buffer& buffer : under_) buffer = {};
    draw_light_ = {};
    stats_ = {};
    stats_zero_.Reset();
    readback_.Reset();
    timestamps_.Reset();
    retired_.clear();
    slots_ = {};
    water_slots_ = {};
    pending_ = {};
    window_ = {};
    composite_timing_ = -1;
    water_result_ = -1;
    water_composite_timing_ = -1;
    samples_ = 0u;
    water_samples_ = 0u;
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

bool RtReflections::create_pipelines(DXGI_FORMAT color_format, unsigned quality) {
    const auto serialize = [&](const D3D12_ROOT_SIGNATURE_DESC& desc, ComPtr<ID3D12RootSignature>& out) {
        return rt_prepare::root_signature(device_.Get(), desc, out);
    };
    // Compiled in parallel (pso_jobs.h), joined where their results decide.
    PsoJobs jobs;
    const auto compute = [&](const void* code, std::size_t bytes, ComPtr<ID3D12PipelineState>& out) {
        jobs.compute(device_.Get(), root_.Get(), code, bytes, out);
        return true;
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
    for (UINT i = kPrevInfo; i <= kEmitters; ++i) {
        params[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[i].Descriptor.ShaderRegister = 6u + (i - kPrevInfo);
    }
    for (UINT i = kInfo; i <= kTiles; ++i) {
        params[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        params[i].Descriptor.ShaderRegister = i - kInfo;
    }
    for (UINT i = kWater; i <= kWaterSurfaces; ++i) {
        params[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[i].Descriptor.ShaderRegister = 11u + (i - kWater);
    }
    params[kUnder].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    params[kUnder].Descriptor.ShaderRegister = 8u;
    params[kSkyMap].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[kSkyMap].Descriptor.ShaderRegister = 14u;
    params[kDraws].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[kDraws].Descriptor.ShaderRegister = 15u;
    params[kSurfaceUv].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[kSurfaceUv].Descriptor.ShaderRegister = 16u;
    params[kDrawTextures].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[kDrawTextures].Descriptor.ShaderRegister = 17u;
    params[kSky].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    params[kSky].Descriptor.ShaderRegister = 9u;
    params[kDrawLight].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    params[kDrawLight].Descriptor.ShaderRegister = 10u;
    // Translucent water's texture coordinates and waves (t18, t19; stand-ins without them).
    params[kWaterUv].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[kWaterUv].Descriptor.ShaderRegister = 18u;
    params[kWaterWaves].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[kWaterWaves].Descriptor.ShaderRegister = 19u;
    for (auto& param : params) {
        param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
    D3D12_STATIC_SAMPLER_DESC samplers[2]{};
    samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplers[0].AddressU = samplers[0].AddressV = samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[0].MaxLOD = D3D12_FLOAT32_MAX;
    samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    // The hits' textures repeat (GX's usual wrap).
    samplers[1] = samplers[0];
    samplers[1].AddressU = samplers[1].AddressV = samplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[1].ShaderRegister = 1u;
    // High quality samples the hits' textures from the frame's descriptor heap: shader model 6.6 and
    // resource binding tier 3 (without them the hits off screen are not textured).
    {
        D3D12_FEATURE_DATA_SHADER_MODEL model{D3D_SHADER_MODEL_6_6};
        D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
        textured_ = quality == 1u &&
                    SUCCEEDED(device_->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &model, sizeof(model))) &&
                    model.HighestShaderModel >= D3D_SHADER_MODEL_6_6 &&
                    SUCCEEDED(device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options))) &&
                    options.ResourceBindingTier >= D3D12_RESOURCE_BINDING_TIER_3 &&
                    env_float("GALAXY_RT_TEXTURED_HITS", 1.0f) != 0.0f;
    }
    D3D12_ROOT_SIGNATURE_DESC rsd{};
    rsd.NumParameters = kParameterCount;
    rsd.pParameters = params;
    rsd.NumStaticSamplers = 2u;
    rsd.pStaticSamplers = samplers;
    rsd.Flags = textured_ ? D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED : D3D12_ROOT_SIGNATURE_FLAG_NONE;
    if (textured_ && !serialize(rsd, root_)) {
        textured_ = false;
        rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    }
    if (textured_) {
        compute(g_rt_reflect_trace_tex_cs, sizeof(g_rt_reflect_trace_tex_cs), trace_pso_);
        compute(g_rt_water_trace_tex_cs, sizeof(g_rt_water_trace_tex_cs), water_trace_pso_);
        if (jobs.wait() != 0u) {
            textured_ = false;
            rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
            root_.Reset();
            trace_pso_.Reset();
            water_trace_pso_.Reset();
        }
    }
    std::cerr << "[rt-reflect] textured hits " << (textured_ ? "on" : "off") << '\n';
    if ((!textured_ && !serialize(rsd, root_)) ||
        (!textured_ && !compute(g_rt_reflect_trace_cs, sizeof(g_rt_reflect_trace_cs), trace_pso_)) ||
        (!textured_ && !compute(g_rt_water_trace_cs, sizeof(g_rt_water_trace_cs), water_trace_pso_)) ||
        !compute(g_rt_reflect_temporal_cs, sizeof(g_rt_reflect_temporal_cs), temporal_pso_) ||
        !compute(g_rt_reflect_filter_a_cs, sizeof(g_rt_reflect_filter_a_cs), filter_a_pso_) ||
        !compute(g_rt_reflect_filter_cs, sizeof(g_rt_reflect_filter_cs), filter_pso_) ||
        !compute(g_rt_reflect_memory_cs, sizeof(g_rt_reflect_memory_cs), memory_pso_) ||
        !compute(g_rt_reflect_sky_cs, sizeof(g_rt_reflect_sky_cs), sky_pso_) ||
        !compute(g_rt_reflect_draw_clear_cs, sizeof(g_rt_reflect_draw_clear_cs), draw_clear_pso_) ||
        !compute(g_rt_reflect_draw_cs, sizeof(g_rt_reflect_draw_cs), draw_light_pso_) || jobs.wait() != 0u) {
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
    composite_params[kCompositeUnder].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    composite_params[kCompositeUnder].Descriptor.ShaderRegister = 4u;
    D3D12_DESCRIPTOR_RANGE scene_range{};
    scene_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    scene_range.NumDescriptors = 1u;
    scene_range.BaseShaderRegister = 5u;
    composite_params[kCompositeScene].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    composite_params[kCompositeScene].DescriptorTable.NumDescriptorRanges = 1u;
    composite_params[kCompositeScene].DescriptorTable.pDescriptorRanges = &scene_range;
    D3D12_DESCRIPTOR_RANGE opaque_range = scene_range;
    opaque_range.BaseShaderRegister = 6u;
    composite_params[kCompositeOpaque].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    composite_params[kCompositeOpaque].DescriptorTable.NumDescriptorRanges = 1u;
    composite_params[kCompositeOpaque].DescriptorTable.pDescriptorRanges = &opaque_range;
    // The effects layer's colour and transmittance (t7, t8).
    D3D12_DESCRIPTOR_RANGE layer_range = scene_range;
    layer_range.NumDescriptors = 2u;
    layer_range.BaseShaderRegister = 7u;
    composite_params[kCompositeLayer].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    composite_params[kCompositeLayer].DescriptorTable.NumDescriptorRanges = 1u;
    composite_params[kCompositeLayer].DescriptorTable.pDescriptorRanges = &layer_range;
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
    if (!rt_prepare::graphics(device_.Get(), graphics, composite_pso_)) {
        return false;
    }
    graphics.PS = {g_rt_water_composite_ps, sizeof(g_rt_water_composite_ps)};
    if (!rt_prepare::graphics(device_.Get(), graphics, water_composite_pso_)) {
        return false;
    }
    graphics.PS = {g_rt_underwater_ps, sizeof(g_rt_underwater_ps)};
    return rt_prepare::graphics(device_.Get(), graphics, underwater_pso_);
}

bool RtReflections::allocate_pass(Pass& pass, std::uint64_t samples, std::uint32_t tiles, const wchar_t* name) {
    const auto make = [&](std::uint64_t bytes, Buffer& out) {
        out.state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        return create_buffer(bytes, D3D12_HEAP_TYPE_DEFAULT, out.state, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                             out.resource, name);
    };
    bool ok = samples != 0u;
    for (unsigned i = 0; ok && i < 2u; ++i) {
        ok = make(samples * 16u, pass.info[i]) && make(samples * 16u, pass.history[i]);
    }
    for (unsigned i = 0; ok && i < kSlots; ++i) {
        ok = make(samples * 16u, pass.final[i]) && make(static_cast<std::uint64_t>(tiles) * 4u, pass.tiles[i]);
    }
    return ok;
}

void RtReflections::release_pass(Pass& pass) {
    for (Buffer& buffer : pass.info) retire(buffer.resource);
    for (Buffer& buffer : pass.history) retire(buffer.resource);
    for (Buffer& buffer : pass.final) retire(buffer.resource);
    for (Buffer& buffer : pass.tiles) retire(buffer.resource);
    pass = {};
}

bool RtReflections::ensure_buffers(std::uint32_t samples, std::uint32_t tiles, bool opaque) {
    if (samples <= samples_ && tiles <= tiles_capacity_ && raw_.resource && (!opaque || main_.final[0].resource)) {
        return true;
    }
    release_pass(main_);
    release_pass(water_);
    retire(raw_.resource);
    for (Buffer& buffer : under_) retire(buffer.resource);
    // Results of the old buffers are gone with them.
    slots_ = {};
    water_slots_ = {};
    samples_ = 0u;
    water_samples_ = 0u;
    tiles_capacity_ = 0u;
    const std::uint64_t n = samples;
    raw_.state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    bool ok = n != 0u &&
              create_buffer(n * 16u, D3D12_HEAP_TYPE_DEFAULT, raw_.state, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                            raw_.resource, L"rt reflect raw") &&
              (!opaque || allocate_pass(main_, n, tiles, L"rt reflect"));
    for (unsigned i = 0; ok && i < kSlots; ++i) {
        under_[i].state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        ok = create_buffer((n + kUnderState) * 8u, D3D12_HEAP_TYPE_DEFAULT, under_[i].state,
                           D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                           under_[i].resource, L"rt reflect under");
    }
    if (!ok) {
        main_ = {};
        raw_ = {};
        for (Buffer& buffer : under_) buffer = {};
        return false;
    }
    samples_ = samples;
    tiles_capacity_ = tiles;
    std::cerr << "[rt] reflect samples=" << samples << " opaque=" << (opaque ? 1 : 0)
              << " mb=" << ((n * (16u * (opaque ? 5u + kSlots : 1u) + 8u * kSlots)) >> 20u) << '\n';
    return true;
}

bool RtReflections::ensure_water_buffers() {
    if (water_samples_ == samples_ && water_.final[0].resource) {
        return true;
    }
    release_pass(water_);
    water_slots_ = {};
    water_samples_ = 0u;
    if (samples_ == 0u || !allocate_pass(water_, samples_, tiles_capacity_, L"rt water")) {
        water_ = {};
        return false;
    }
    water_samples_ = samples_;
    std::cerr << "[rt] water samples=" << samples_
              << " mb=" << ((static_cast<std::uint64_t>(samples_) * 16u * (4u + kSlots)) >> 20u) << '\n';
    return true;
}

void RtReflections::invalidate(unsigned slot) noexcept {
    if (slot < kSlots) {
        slots_[slot].valid = false;
        water_slots_[slot].valid = false;
    }
}

void RtReflections::fill_constants(const RtReflectScene& scene, const Pass& pass, std::uint32_t flags,
                                   void* constants_cpu) {
    std::array<float, 12> inverse{};
    const bool invertible = scene.camera_known && affine_inverse(scene.camera, inverse);
    const bool history = (flags & kNoHistory) == 0u && pass.history_valid && invertible && pass.history_camera_known &&
                         pass.history_width == scene.sample_width && pass.history_height == scene.sample_height &&
                         pass.history_factor == scene.factor;
    const std::array<float, 12> reproject = history ? affine_multiply(pass.history_camera, inverse) : kIdentity34;
    RtReflectConstants c{};
    std::copy(scene.projection.begin(), scene.projection.end(), c.projection);
    std::copy(scene.depth.begin(), scene.depth.end(), c.depth);
    std::copy(scene.viewport.begin(), scene.viewport.end(), c.viewport);
    c.target_size[0] = scene.width;
    c.target_size[1] = scene.height;
    c.sample_size[0] = scene.sample_width;
    c.sample_size[1] = scene.sample_height;
    c.near_plane = scene.near_plane;
    c.frame = frame_++;
    c.factor = scene.factor;
    const RtReflectLights& lights = scene.lights;
    c.flags = (flags & ~kNoHistory) | (history ? 1u : 0u) | (scene.sums != 0u ? 4u : 0u) | (invertible ? 8u : 0u) |
              (lights.emitters != 0u ? 16u : 0u) | (scene.sky_map != 0u && invertible ? 64u : 0u) |
              (scene.draws != 0u && scene.draw_count != 0u ? 128u : 0u);
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
    c.ripple = ripple_;
    c.probe = probe_;
    c.sky_distance = scene.sky_distance > 0.0f ? scene.sky_distance : 1.0e9f;
    std::copy(lights.sun.begin(), lights.sun.end(), c.sun);
    std::copy(lights.sun_light.begin(), lights.sun_light.end(), c.sun_light);
    for (unsigned i = 0; i < 2u * kRtPointLights; ++i) {
        std::copy(lights.points[i].begin(), lights.points[i].end(), c.points[i]);
    }
    c.point_count = std::min(lights.point_count, kRtPointLights);
    c.point_light = lights.point_light;
    c.point_size = lights.point_size;
    c.night_lights = lights.night_lights;
    c.night_cap = lights.night_cap;
    c.sun_cap = lights.sun_cap;
    c.core_point = lights.core_point;
    c.shafts = shafts_;
    c.sky_fade = sky_fade_;
    c.sky_average = sky_average_;
    c.metal_sky = metal_sky_;
    c.local_gain = local_gain_;
    c.draw_count = scene.draws != 0u ? scene.draw_count : 0u;
    c.rough_cutoff = rough_cutoff_;
    c.waves = waves_;
    c.metal_f0 = materials_[2].f0;
    std::copy(scene.hit.begin(), scene.hit.end(), c.hit);
    c.texture_count = scene.draw_textures != 0u ? scene.texture_count : 0u;
    if (textured_ && scene.surface_uv != 0u && scene.draw_textures != 0u) {
        c.flags |= 2048u;
    }
    if (scene.engine_clear) {
        c.flags |= 8192u;
    }
    c.draw_trust = draw_trust_;
    c.sky_trust = sky_trust_;
    c.metal_cover = materials_[kBareMetalClass].cover;
    std::copy(scene.sky_fill.begin(), scene.sky_fill.end(), c.sky_fill);
    // Physical light (bit 12): the hits in the lighting's units; off screen (with the textured hits' heap) lit by it.
    const RtReflectPhysical& physical = scene.physical;
    if (physical.on) {
        c.flags |= 4096u;
        std::copy(physical.sky_radiance.begin(), physical.sky_radiance.end(), c.sky_radiance);
        std::copy(physical.full_light.begin(), physical.full_light.end(), c.full_light);
        std::copy(physical.up.begin(), physical.up.end(), c.up);
        std::copy(physical.light_fit.begin(), physical.light_fit.end(), c.light_fit);
        c.tev_view = physical.tev_view;
        c.tev_count = physical.tev_view != ~0u ? physical.tev_count : 0u;
        c.emit_view = physical.emit_view;
        c.emit_count = physical.emit_view != ~0u ? physical.emit_count : 0u;
        c.emit_scene = physical.emit_scene;
        c.sky_view = physical.sky_view;
    } else {
        c.tev_view = c.emit_view = c.sky_view = ~0u;
    }
    std::memcpy(constants_cpu, &c, sizeof(c));
}

void RtReflections::bind_trace(ID3D12GraphicsCommandList* list, const RtReflectScene& scene, Pass& pass,
                               unsigned slot, D3D12_GPU_VIRTUAL_ADDRESS constants_gpu,
                               D3D12_GPU_DESCRIPTOR_HANDLE color_depth) {
    const unsigned current = pass.parity;
    const unsigned previous = current ^ 1u;
    constexpr D3D12_RESOURCE_STATES kUav = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    constexpr D3D12_RESOURCE_STATES kRead = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    to_state(list, stats_, kUav);
    to_state(list, memory_, kUav);
    to_state(list, sky_, kUav);
    to_state(list, draw_light_, kUav);
    to_state(list, pass.info[current], kUav);
    to_state(list, raw_, kUav);
    to_state(list, pass.history[current], kUav);
    to_state(list, pass.final[slot], kUav);
    to_state(list, pass.tiles[slot], kUav);
    to_state(list, pass.info[previous], kRead);
    to_state(list, pass.history[previous], kRead);
    to_state(list, under_[slot], kUav);
    const D3D12_GPU_VIRTUAL_ADDRESS previous_info = pass.info[previous].resource->GetGPUVirtualAddress();
    const bool water = scene.water_tlas != 0u;
    list->SetComputeRootSignature(root_.Get());
    list->SetComputeRootConstantBufferView(kFrame, constants_gpu);
    list->SetComputeRootShaderResourceView(kScene, scene.tlas);
    list->SetComputeRootShaderResourceView(kPositions, scene.positions);
    list->SetComputeRootShaderResourceView(kSurfaces, scene.surfaces);
    list->SetComputeRootShaderResourceView(kLight, scene.light);
    list->SetComputeRootDescriptorTable(kSources, color_depth);
    list->SetComputeRootShaderResourceView(kPrevInfo, previous_info);
    list->SetComputeRootShaderResourceView(kPrevHistory, pass.history[previous].resource->GetGPUVirtualAddress());
    list->SetComputeRootShaderResourceView(kSums, scene.sums != 0u ? scene.sums : previous_info);
    list->SetComputeRootShaderResourceView(kHistoryIn, pass.history[previous].resource->GetGPUVirtualAddress());
    list->SetComputeRootShaderResourceView(kEmitters, scene.lights.emitters != 0u ? scene.lights.emitters
                                                                                   : previous_info);
    list->SetComputeRootUnorderedAccessView(kInfo, pass.info[current].resource->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(kRaw, raw_.resource->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(kHistory, pass.history[current].resource->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(kFinal, pass.final[slot].resource->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(kStats, stats_.resource->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(kMemory, memory_.resource->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(kPrimary, scene.primary);
    list->SetComputeRootUnorderedAccessView(kTiles, pass.tiles[slot].resource->GetGPUVirtualAddress());
    list->SetComputeRootShaderResourceView(kWater, water ? scene.water_tlas : scene.tlas);
    list->SetComputeRootShaderResourceView(kWaterPositions, water ? scene.water_positions : scene.positions);
    list->SetComputeRootShaderResourceView(kWaterSurfaces, water ? scene.water_surfaces : scene.surfaces);
    list->SetComputeRootUnorderedAccessView(kUnder, under_[slot].resource->GetGPUVirtualAddress());
    list->SetComputeRootShaderResourceView(kSkyMap, scene.sky_map != 0u ? scene.sky_map : previous_info);
    list->SetComputeRootShaderResourceView(kDraws, scene.draws != 0u ? scene.draws : previous_info);
    list->SetComputeRootShaderResourceView(kSurfaceUv, scene.surface_uv != 0u ? scene.surface_uv : previous_info);
    list->SetComputeRootShaderResourceView(kDrawTextures,
                                           scene.draw_textures != 0u ? scene.draw_textures : previous_info);
    list->SetComputeRootUnorderedAccessView(kSky, sky_.resource->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(kDrawLight, draw_light_.resource->GetGPUVirtualAddress());
    list->SetComputeRootShaderResourceView(kWaterUv, water && scene.water_uv != 0u ? scene.water_uv : previous_info);
    list->SetComputeRootShaderResourceView(kWaterWaves,
                                           water && scene.water_waves != 0u ? scene.water_waves : previous_info);
}

void RtReflections::accumulate(ID3D12GraphicsCommandList* list, Pass& pass, unsigned slot, UINT groups_x,
                               UINT groups_y) {
    const unsigned current = pass.parity;
    const D3D12_RESOURCE_BARRIER all_uav = uav_barrier();
    list->ResourceBarrier(1u, &all_uav);
    list->SetPipelineState(temporal_pso_.Get());
    list->Dispatch(groups_x, groups_y, 1u);
    list->ResourceBarrier(1u, &all_uav);
    // The accumulated light is read-only from here; its UAV parameter names a buffer the filters
    // do not touch through it. The first filter writes the raw light's buffer, the second reads it.
    constexpr D3D12_RESOURCE_STATES kRead = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    to_state(list, pass.history[current], kRead);
    list->SetComputeRootShaderResourceView(kHistoryIn, pass.history[current].resource->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(kHistory, raw_.resource->GetGPUVirtualAddress());
    list->SetPipelineState(filter_a_pso_.Get());
    list->Dispatch(groups_x, groups_y, 1u);
    list->ResourceBarrier(1u, &all_uav);
    list->SetPipelineState(filter_pso_.Get());
    list->Dispatch(groups_x, groups_y, 1u);
    to_state(list, pass.final[slot], kFinalRead);
    to_state(list, pass.tiles[slot], kFinalRead);
}

void RtReflections::finish_trace(Pass& pass, std::array<Slot, kSlots>& slots, const RtReflectScene& scene,
                                 unsigned slot, UINT groups_x) {
    Slot& info = slots[slot];
    info.valid = true;
    info.serial = serial_;
    info.projection = scene.projection;
    info.depth = scene.depth;
    info.viewport = scene.viewport;
    info.near_plane = scene.near_plane;
    info.width = scene.width;
    info.height = scene.height;
    info.sample_width = scene.sample_width;
    info.sample_height = scene.sample_height;
    info.factor = scene.factor;
    info.tile_width = groups_x;
    info.sun = {0.0f, 0.0f, 0.0f, 0.0f};
    if (scene.lights.sun_light[0] + scene.lights.sun_light[1] + scene.lights.sun_light[2] > 0.0f) {
        info.sun = {scene.lights.sun[0], scene.lights.sun[1], scene.lights.sun[2], 1.0f};
    }
    pass.history_camera = scene.camera;
    pass.history_camera_known = scene.camera_known;
    pass.history_width = scene.sample_width;
    pass.history_height = scene.sample_height;
    pass.history_factor = scene.factor;
    pass.history_valid = true;
    pass.parity ^= 1u;
}

void RtReflections::trace(ID3D12GraphicsCommandList* list, const RtReflectScene& scene, unsigned slot,
                          void* constants_cpu, D3D12_GPU_VIRTUAL_ADDRESS constants_gpu,
                          D3D12_GPU_DESCRIPTOR_HANDLE color_depth) {
    water_result_ = -1;
    if (!ready_ || slot >= kSlots) {
        return;
    }
    slots_[slot].valid = false;
    water_slots_[slot].valid = false;
    if (list == nullptr || constants_cpu == nullptr || scene.tlas == 0u || scene.positions == 0u ||
        scene.surfaces == 0u || scene.primary == 0u || scene.light == 0u || color_depth.ptr == 0u ||
        scene.width == 0u || scene.height == 0u || scene.sample_width == 0u || scene.sample_height == 0u ||
        scene.factor == 0u) {
        return;
    }
    const UINT groups_x = (scene.sample_width + 7u) / 8u;
    const UINT groups_y = (scene.sample_height + 7u) / 8u;
    if (!ensure_buffers(scene.sample_width * scene.sample_height, groups_x * groups_y, true)) {
        return;
    }
    RtReflectScene opaque = scene;
    opaque.water_tlas = 0u;
    // A new stage: the environment memory starts over (rt_reflections.hlsl ReflectMemoryCS). Views 7 and 8:
    // the samples show their table row or their reflection's source (kViewRow, kViewSource); view 9 cycles
    // through the composite, both and the reflected light, without history (no frame's light is another's).
    const std::uint32_t phase = static_cast<std::uint32_t>(serial_ + 1u) & 3u;
    const std::uint32_t diagnostic = view_ == 7u || (view_ == 9u && phase == 1u)   ? 512u
                                     : view_ == 8u || (view_ == 9u && phase == 2u) ? 1024u
                                     : view_ == 9u                                 ? kNoHistory
                                                                                   : 0u;
    fill_constants(opaque, main_, (std::exchange(memory_reset_, false) ? 256u : 0u) | diagnostic, constants_cpu);

    const unsigned result = static_cast<unsigned>(serial_++ % kResultSlots);
    const bool record = !pending_[result].pending;
    if (record && timestamps_) {
        list->EndQuery(timestamps_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, result * kTimestampsPerSlot);
    }
    if (record) {
        to_state(list, stats_, D3D12_RESOURCE_STATE_COPY_DEST);
        list->CopyBufferRegion(stats_.resource.Get(), 0u, stats_zero_.Get(), 0u, kStatsWords * 4u);
    }
    bind_trace(list, opaque, main_, slot, constants_gpu, color_depth);
    // Also orders the lighting's writes of the primary hits before the trace reads them.
    const D3D12_RESOURCE_BARRIER all_uav = uav_barrier();
    list->SetPipelineState(memory_pso_.Get());
    list->Dispatch(kMemorySize / 8u, kMemorySize / 8u, 1u);
    list->SetPipelineState(sky_pso_.Get());
    list->Dispatch(kSkySize / 8u, kSkySize / 8u, 1u);
    const std::uint32_t draws = opaque.draws != 0u ? std::min(opaque.draw_count, kDrawLightEntries) : 0u;
    if (draws != 0u) {
        list->SetPipelineState(draw_clear_pso_.Get());
        list->Dispatch((draws + 63u) / 64u, 1u, 1u);
        list->ResourceBarrier(1u, &all_uav);
        list->SetPipelineState(draw_light_pso_.Get());
        list->Dispatch(groups_x, groups_y, 1u);
    }
    list->ResourceBarrier(1u, &all_uav);
    list->SetPipelineState(trace_pso_.Get());
    list->Dispatch(groups_x, groups_y, 1u);
    accumulate(list, main_, slot, groups_x, groups_y);
    if (record) {
        if (timestamps_) {
            list->EndQuery(timestamps_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, result * kTimestampsPerSlot + 1u);
            list->ResolveQueryData(timestamps_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, result * kTimestampsPerSlot, 2u,
                                   readback_.Get(), result * kReadbackSlotBytes + kReadbackTicks);
        }
        // All words: the water pass, when it runs, copies its half (16-31) again after its own counts.
        to_state(list, stats_, D3D12_RESOURCE_STATE_COPY_SOURCE);
        list->CopyBufferRegion(readback_.Get(), result * kReadbackSlotBytes, stats_.resource.Get(), 0u,
                               kStatsWords * 4u);
        pending_[result] = Pending{frame_index_, true, false, false, false};
        water_result_ = static_cast<int>(result);
    }
    composite_timing_ = record && timestamps_ ? static_cast<int>(result) : -1;
    water_composite_timing_ = -1;
    ++window_.traces;
    finish_trace(main_, slots_, opaque, slot, groups_x);
}

void RtReflections::trace_water(ID3D12GraphicsCommandList* list, const RtReflectScene& scene, unsigned slot,
                                void* constants_cpu, D3D12_GPU_VIRTUAL_ADDRESS constants_gpu,
                                D3D12_GPU_DESCRIPTOR_HANDLE color_depth) {
    const int result = std::exchange(water_result_, -1);
    if (!ready_ || slot >= kSlots) {
        return;
    }
    water_slots_[slot].valid = false;
    if (list == nullptr || constants_cpu == nullptr || scene.tlas == 0u || scene.water_tlas == 0u ||
        scene.water_positions == 0u || scene.water_surfaces == 0u || scene.primary == 0u || scene.light == 0u ||
        color_depth.ptr == 0u || scene.width == 0u || scene.sample_width == 0u || scene.factor == 0u) {
        if (log_seq_) {
            std::fprintf(stderr, "[rt-seq] reflections water refused primary=%d light=%d width=%u samples=%ux%u of %u raw=%d\n",
                         scene.primary != 0u ? 1 : 0, scene.light != 0u ? 1 : 0, scene.width, scene.sample_width,
                         scene.sample_height, samples_, raw_.resource ? 1 : 0);
        }
        return;
    }
    const UINT groups_x = (scene.sample_width + 7u) / 8u;
    const UINT groups_y = (scene.sample_height + 7u) / 8u;
    // The water pass makes the samples' buffers itself: under the engine the opaque pass, which would make them, does
    // not run.
    if (!ensure_buffers(scene.sample_width * scene.sample_height, groups_x * groups_y, false) ||
        !ensure_water_buffers()) {
        if (log_seq_) {
            std::fprintf(stderr, "[rt-seq] reflections water buffers groups=%u capacity=%u\n", groups_x * groups_y,
                         tiles_capacity_);
        }
        return;
    }
    if (log_seq_) {
        std::fprintf(stderr, "[rt-seq] reflections water traced slot=%u samples=%ux%u\n", slot, scene.sample_width,
                     scene.sample_height);
    }
    // The water pass: the EFB colour is the lit picture.
    fill_constants(scene, water_, 32u, constants_cpu);
    {
        // The tint sums (rt_reflections.hlsl kUnderTint) start at zero.
        const std::uint64_t n = static_cast<std::uint64_t>(scene.sample_width) * scene.sample_height;
        to_state(list, under_[slot], D3D12_RESOURCE_STATE_COPY_DEST);
        list->CopyBufferRegion(under_[slot].resource.Get(), (n + kUnderTint) * 8u, stats_zero_.Get(), 0u, 32u);
    }
    const bool timed = result >= 0 && timestamps_;
    const auto r = static_cast<unsigned>(result);
    if (timed) {
        list->EndQuery(timestamps_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, r * kTimestampsPerSlot + 4u);
    }
    bind_trace(list, scene, water_, slot, constants_gpu, color_depth);
    list->SetPipelineState(water_trace_pso_.Get());
    list->Dispatch(groups_x, groups_y, 1u);
    accumulate(list, water_, slot, groups_x, groups_y);
    if (result >= 0) {
        if (timed) {
            list->EndQuery(timestamps_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, r * kTimestampsPerSlot + 5u);
            list->ResolveQueryData(timestamps_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, r * kTimestampsPerSlot + 4u, 2u,
                                   readback_.Get(), r * kReadbackSlotBytes + kReadbackTicks + 32u);
            pending_[r].water_timed = true;
        }
        // The water's words (16-31) after the trace's.
        to_state(list, stats_, D3D12_RESOURCE_STATE_COPY_SOURCE);
        list->CopyBufferRegion(readback_.Get(), r * kReadbackSlotBytes + kStatsWords * 2u, stats_.resource.Get(),
                               kStatsWords * 2u, kStatsWords * 2u);
    }
    water_composite_timing_ = timed ? result : -1;
    ++window_.waters;
    finish_trace(water_, water_slots_, scene, slot, groups_x);
    water_slots_[slot].game_waves = scene.water_game_waves;
}

void RtReflections::draw_composite(ID3D12GraphicsCommandList* list, Pass& pass, ID3D12PipelineState* pso,
                                   D3D12_CPU_DESCRIPTOR_HANDLE target, D3D12_GPU_DESCRIPTOR_HANDLE depth_srv,
                                   std::uint32_t width, std::uint32_t height, unsigned slot, const Slot& info,
                                   D3D12_GPU_VIRTUAL_ADDRESS reproject, float split) {
    to_state(list, pass.final[slot], kFinalRead);
    to_state(list, pass.tiles[slot], kFinalRead);
    to_state(list, under_[slot], kFinalRead);
    RtReflectCompositeConstants c{};
    std::copy(info.projection.begin(), info.projection.end(), c.projection);
    std::copy(info.depth.begin(), info.depth.end(), c.depth);
    std::copy(info.viewport.begin(), info.viewport.end(), c.viewport);
    c.sample_size[0] = info.sample_width;
    c.sample_size[1] = info.sample_height;
    c.factor = info.factor;
    // GALAXY_RT_REFLECT_VIEW=5: the surface classes on every other traced frame; 6: the composite, the
    // reflected light alone (composite view 4) and its weight with the highlights (5) in turn; 7 and 8: the
    // samples' light alone (their table row, their reflection's source).
    static constexpr std::uint32_t kCycle[4] = {0u, 0u, 4u, 5u};
    const std::uint32_t view = view_ == 5u   ? ((info.serial & 1u) != 0u ? 2u : 0u)
                               : view_ == 6u ? kCycle[info.serial & 3u]
                               : view_ == 9u ? ((info.serial & 3u) != 0u ? 4u : 0u)
                               : view_ >= 7u ? 4u
                                             : view_ & 3u;
    c.flags = (reproject != 0u ? 1u : 0u) | (view << 1u) | (effects_layer_.ptr != 0u ? 16u : 0u);
    c.slot = slot;
    c.tile_width = info.tile_width;
    c.split = split;
    c.near_plane = info.near_plane;
    for (unsigned i = 0; i < kClasses; ++i) {
        const Material& m = materials_[i];
        c.classes[i][0] = half_pair(m.strength, m.f0);
        c.classes[i][1] = half_pair(std::max(m.roughness * m.roughness, 1e-3f), m.cover);
    }
    for (unsigned i = 0; i < 3u; ++i) {
        c.water[i] = absorb_[i] * 1e-3f;
        c.sun[i] = info.sun[i];
    }
    c.water[3] = clear_;
    c.sun[3] = info.sun[3] * caustics_;
    // The composite's own waves rest where the water's are the game's (traced, water_wave).
    c.water2[0] = info.game_waves ? 0.0f : waves_;
    // Its sign: the frame before the water is bound (the water layer over a sharp seabed).
    c.water2[1] = water_opaque_.ptr != 0u ? water_cover_ : -water_cover_;
    c.water2[2] = foam_;
    c.water2[3] = shallow_caustics_;
    const D3D12_GPU_VIRTUAL_ADDRESS final_address = pass.final[slot].resource->GetGPUVirtualAddress();
    list->OMSetRenderTargets(1u, &target, FALSE, nullptr);
    const D3D12_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f};
    const D3D12_RECT scissor{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    list->RSSetViewports(1u, &viewport);
    list->RSSetScissorRects(1u, &scissor);
    list->SetGraphicsRootSignature(composite_root_.Get());
    list->SetPipelineState(pso);
    list->SetGraphicsRoot32BitConstants(kCompositeConstants, sizeof(c) / 4u, &c, 0u);
    list->SetGraphicsRootShaderResourceView(kCompositeFinal, final_address);
    list->SetGraphicsRootDescriptorTable(kCompositeDepth, depth_srv);
    list->SetGraphicsRootShaderResourceView(kCompositeReproject, reproject != 0u ? reproject : final_address);
    list->SetGraphicsRootShaderResourceView(kCompositeTiles, pass.tiles[slot].resource->GetGPUVirtualAddress());
    list->SetGraphicsRootShaderResourceView(kCompositeUnder, under_[slot].resource->GetGPUVirtualAddress());
    // The water composite reads the frame with the game's water (water_scene_); the others never do.
    list->SetGraphicsRootDescriptorTable(kCompositeScene, water_scene_.ptr != 0u ? water_scene_ : depth_srv);
    list->SetGraphicsRootDescriptorTable(kCompositeOpaque, water_opaque_.ptr != 0u ? water_opaque_ : depth_srv);
    list->SetGraphicsRootDescriptorTable(kCompositeLayer, effects_layer_.ptr != 0u ? effects_layer_ : depth_srv);
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list->DrawInstanced(3u, 1u, 0u, 0u);
}

void RtReflections::composite(ID3D12GraphicsCommandList* list, D3D12_CPU_DESCRIPTOR_HANDLE target,
                              D3D12_GPU_DESCRIPTOR_HANDLE depth_srv, std::uint32_t width, std::uint32_t height,
                              unsigned slot, D3D12_GPU_VIRTUAL_ADDRESS reproject, float split) {
    if (!ready_ || list == nullptr || slot >= kSlots || depth_srv.ptr == 0u) {
        return;
    }
    const Slot& info = slots_[slot];
    if (!info.valid || info.width != width || info.height != height || !main_.final[slot].resource) {
        return;
    }
    // GALAXY_RT_REFLECT_VIEW=4: every other traced frame without reflections (consecutive
    // snapshots are A/B pairs); 6: one traced frame in four.
    if ((view_ == 4u && (info.serial & 1u) != 0u) || (view_ == 6u && (info.serial & 3u) == 1u)) {
        return;
    }
    if (view_ == 4u || view_ == 6u || view_ == 9u) {
        // The frames with reflections carry a mark at the middle of the right edge: white the composite,
        // red the reflected light, green its weight (VIEW=6); white the composite, red the table rows, green
        // the sources, blue the reflected light (VIEW=9).
        const std::uint32_t phase = view_ != 4u ? info.serial & 3u : 0u;
        const std::array<float, 4> colour =
            view_ == 9u ? std::array<float, 4>{phase <= 1u ? 1.0f : 0.0f, phase == 0u || phase == 2u ? 1.0f : 0.0f,
                                               phase == 0u || phase == 3u ? 1.0f : 0.0f, 1.0f}
                        : std::array<float, 4>{phase != 3u ? 1.0f : 0.0f, phase != 2u ? 1.0f : 0.0f,
                                               phase == 0u ? 1.0f : 0.0f, 1.0f};
        const D3D12_RECT mark{static_cast<LONG>(width) - 12, static_cast<LONG>(height / 2u) - 6,
                              static_cast<LONG>(width), static_cast<LONG>(height / 2u) + 6};
        list->ClearRenderTargetView(target, colour.data(), 1u, &mark);
    }
    // The traced frame's composite is timed; its sub-frames' cost the same.
    const bool timed = composite_timing_ >= 0 && reproject == 0u;
    const unsigned result = timed ? static_cast<unsigned>(composite_timing_) : 0u;
    if (timed) {
        list->EndQuery(timestamps_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, result * kTimestampsPerSlot + 2u);
    }
    draw_composite(list, main_, composite_pso_.Get(), target, depth_srv, width, height, slot, info, reproject, split);
    ++window_.composites;
    if (timed) {
        list->EndQuery(timestamps_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, result * kTimestampsPerSlot + 3u);
        list->ResolveQueryData(timestamps_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, result * kTimestampsPerSlot + 2u, 2u,
                               readback_.Get(), result * kReadbackSlotBytes + kReadbackTicks + 16u);
        pending_[result].composite_timed = true;
        composite_timing_ = -1;
    }
}

void RtReflections::composite_water(ID3D12GraphicsCommandList* list, D3D12_CPU_DESCRIPTOR_HANDLE target,
                                    D3D12_GPU_DESCRIPTOR_HANDLE depth_srv, std::uint32_t width, std::uint32_t height,
                                    unsigned slot, D3D12_GPU_VIRTUAL_ADDRESS reproject, float split,
                                    D3D12_GPU_DESCRIPTOR_HANDLE scene_srv, D3D12_GPU_DESCRIPTOR_HANDLE opaque_srv,
                                    D3D12_GPU_DESCRIPTOR_HANDLE layer_srv) {
    if (!ready_ || list == nullptr || slot >= kSlots || depth_srv.ptr == 0u) {
        return;
    }
    const Slot& info = water_slots_[slot];
    if (log_seq_) {
        std::fprintf(stderr, "[rt-seq] reflections water composite slot=%u valid=%d size=%ux%u target=%ux%u view=%u\n",
                     slot, info.valid ? 1 : 0, info.width, info.height, width, height, view_);
    }
    if (!info.valid || info.width != width || info.height != height || !water_.final[slot].resource) {
        return;
    }
    if ((view_ == 4u && (slots_[slot].serial & 1u) != 0u) || (view_ == 6u && (slots_[slot].serial & 3u) == 1u)) {
        return;
    }
    const bool timed = water_composite_timing_ >= 0 && reproject == 0u;
    const unsigned result = timed ? static_cast<unsigned>(water_composite_timing_) : 0u;
    if (timed) {
        list->EndQuery(timestamps_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, result * kTimestampsPerSlot + 6u);
    }
    water_scene_ = scene_srv;
    water_opaque_ = scene_srv.ptr != 0u ? opaque_srv : D3D12_GPU_DESCRIPTOR_HANDLE{};
    effects_layer_ = layer_srv;
    draw_composite(list, water_, water_composite_pso_.Get(), target, depth_srv, width, height, slot, info, reproject,
                   split);
    water_scene_ = {};
    water_opaque_ = {};
    effects_layer_ = {};
    // Under the water (its state from the water pass): caustics and shafts over the whole picture.
    if (caustics_ > 0.0f || shafts_ > 0.0f) {
        list->SetPipelineState(underwater_pso_.Get());
        list->DrawInstanced(3u, 1u, 0u, 0u);
    }
    if (timed) {
        list->EndQuery(timestamps_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, result * kTimestampsPerSlot + 7u);
        list->ResolveQueryData(timestamps_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, result * kTimestampsPerSlot + 6u, 2u,
                               readback_.Get(), result * kReadbackSlotBytes + kReadbackTicks + 48u);
        pending_[result].water_composite_timed = true;
        water_composite_timing_ = -1;
    }
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
            const auto span = [&](unsigned begin, double& sum, std::uint64_t& count) {
                if (ticks[begin + 1u] >= ticks[begin] && ticks[begin + 1u] - ticks[begin] < timestamp_frequency_) {
                    sum += static_cast<double>(ticks[begin + 1u] - ticks[begin]) * to_ms;
                    ++count;
                }
            };
            span(0u, window_.trace_ms, window_.timed);
            if (entry.composite_timed) {
                span(2u, window_.composite_ms, window_.composites_timed);
            }
            if (entry.water_timed) {
                span(4u, window_.water_ms, window_.water_timed);
            }
            if (entry.water_composite_timed) {
                span(6u, window_.water_composite_ms, window_.water_composites_timed);
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
    char line[1200];
    std::snprintf(line, sizeof(line),
                  "[rt-reflect] traces=%llu trace-ms=%.3f composite-ms=%.3f composites-per-trace=%.2f samples=%u "
                  "geometry-samples=%.0f found=%.2f%% diffuse=%.2f%% glossy=%.2f%% metal=%.2f%% unlit=%.2f%% "
                  "water=%.2f%% rays=%.0f hit-shown=%.2f%% hit-remembered=%.2f%% hit-unknown=%.2f%% "
                  "hit-probe=%.2f%% hit-draw=%.2f%% sky-shown=%.2f%% sky-captured=%.2f%% sky-remembered=%.2f%% "
                  "(map %.2f%%) sky-average=%.2f%% highlights=%.2f%% | "
                  "waters=%llu water-ms=%.3f water-composite-ms=%.3f on-water=%.2f%% water-rays=%.0f "
                  "hit-shown=%.2f%% hit-remembered=%.2f%% hit-unknown=%.2f%% hit-probe=%.2f%% sky-shown=%.2f%% "
                  "sky-remembered=%.2f%% (map %.2f%%) sky-average=%.2f%% highlights=%.2f%% under=%llu\n",
                  static_cast<unsigned long long>(window_.traces), per(window_.trace_ms, window_.timed),
                  per(window_.composite_ms, window_.composites_timed),
                  per(static_cast<double>(window_.composites), window_.traces), samples_,
                  per(static_cast<double>(window_.stats[0]), window_.counted), pct(1u, 0u), pct(2u, 1u), pct(3u, 1u),
                  pct(4u, 1u), pct(5u, 1u), pct(6u, 1u), per(static_cast<double>(window_.stats[7]), window_.counted),
                  pct(8u, 7u), pct(9u, 7u), pct(10u, 7u), pct(11u, 7u), pct(30u, 7u), pct(12u, 7u), pct(31u, 7u),
                  pct(13u, 7u), pct(28u, 7u), pct(14u, 7u), pct(15u, 7u), static_cast<unsigned long long>(window_.waters),
                  per(window_.water_ms, window_.water_timed),
                  per(window_.water_composite_ms, window_.water_composites_timed), pct(17u, 16u),
                  per(static_cast<double>(window_.stats[18]), window_.counted), pct(19u, 18u), pct(20u, 18u),
                  pct(21u, 18u), pct(22u, 18u), pct(23u, 18u), pct(24u, 18u), pct(29u, 18u), pct(25u, 18u), pct(26u, 18u),
                  static_cast<unsigned long long>(window_.stats[27]));
    std::cerr << line;
    window_ = {};
}

}  // namespace galaxy::gx
