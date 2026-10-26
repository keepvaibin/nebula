// SPDX-License-Identifier: GPL-3.0-only
// SceneRayTracer's ray tracing engine (rt_engine.hlsl, docs/RAY_TRACING_ENGINE.md): G-buffer, ReSTIR DI and GI, GGX
// specular and the denoiser, replacing the lighting passes under physical light with the textured lighting.
// Its buffers stay unordered-access (the slot results excepted) and are reached through views in the scene's
// descriptor block (rt_engine.hlsl g_engine_views).
#include "galaxy/gx/ray_tracing.h"

#include "rt_engine_atrous_cs.h"
#include "rt_engine_clear_cs.h"
#include "rt_engine_direct_cs.h"
#include "rt_engine_direct_spatial_cs.h"
#include "rt_engine_final_cs.h"
#include "rt_engine_gbuffer_cs.h"
#include "rt_engine_indirect_cs.h"
#include "rt_engine_indirect_spatial_cs.h"
#include "rt_engine_light_grid_cs.h"
#include "rt_engine_cache_cs.h"
#include "rt_engine_reference_cs.h"
#include "rt_engine_shadow_blur_cs.h"
#include "rt_engine_shadow_post_cs.h"
#include "rt_engine_shadow_stabilize_cs.h"
#include "rt_engine_specular_cs.h"
#include "rt_engine_temporal_cs.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <unordered_map>

namespace galaxy::gx {
namespace {

using Microsoft::WRL::ComPtr;

// rt_engine.hlsl kEv*.
enum EngineView : std::uint32_t {
    kEvGBuffer, kEvGBufferPrev, kEvDiTemp, kEvDi, kEvDiPrev, kEvDirect, kEvGiTemp, kEvGi, kEvGiPrev, kEvGiOut,
    kEvSpecular, kEvHistDiffuse, kEvHistDiffusePrev, kEvHistSpecular, kEvHistSpecularPrev, kEvAtrousA, kEvAtrousB,
    kEvSlot, kEvPicture, kEvShadow, kEvShadowPrev, kEvLightGrid, kEvLightRemap, kEvCount,
    // The clear-surface pass's views (trace_engine_clear) follow the trace's.
    kEvWaterScene = kEvCount, kEvWaterPositions, kEvWaterSurfaces, kEvClearDraws, kEvClear, kEvClearEnd,
    // The raster's motion where the frame has it (kEngineMotion).
    kEvMotion = kEvClearEnd,
    // The world's radiance cache (rt_engine.hlsl EngineCacheCS).
    kEvRadianceCache,
};
static_assert(kEvRadianceCache < 32u);

// rt_engine.hlsl's light grid: kGridCells^3 cells of kGridSlots slots (a light, its weight).
constexpr std::uint64_t kEngineGridBytes = 24ull * 24ull * 24ull * 64ull * 8ull;

// rt_engine.hlsl kEngine*.
constexpr std::uint32_t kEngineOn = 1u;
constexpr std::uint32_t kEngineSpecular = 2u;
constexpr std::uint32_t kEngineIndirect = 4u;
constexpr std::uint32_t kEnginePoints = 256u;
constexpr std::uint32_t kEngineClearFind = 512u;
constexpr std::uint32_t kEngineClearNoRay = 1024u;
constexpr std::uint32_t kEngineClearNoSun = 2048u;
constexpr std::uint32_t kEngineClearNoBack = 4096u;
constexpr std::uint32_t kEngineClearNoShade = 8192u;
constexpr std::uint32_t kEngineHitNoLights = 16384u;
constexpr std::uint32_t kEngineHitNoSky = 32768u;
constexpr std::uint32_t kEngineClearNoTev = 65536u;
constexpr std::uint32_t kEngineBounceCache = 131072u;
constexpr std::uint32_t kEngineReference = 262144u;
constexpr std::uint32_t kEngineWaterLight = 524288u;
constexpr std::uint32_t kEngineNoAntiLag = 1048576u;
constexpr std::uint32_t kEngineNoLightHistory = 2097152u;
constexpr std::uint32_t kEngineClearSolidLeft = 4194304u;
constexpr std::uint32_t kEngineNoBoiling = 8388608u;
constexpr std::uint32_t kEngineMotion = 16777216u;
constexpr std::uint32_t kEngineNoFootprint = 33554432u;
constexpr std::uint32_t kEngineNoCache = 67108864u;
constexpr std::uint32_t kEngineNoFirefly = 134217728u;
constexpr std::uint32_t kEngineNoDiSpatial = 1073741824u;
constexpr std::uint32_t kEngineGiFull = 2147483648u;
// rt_engine.hlsl's radiance cache: kCacheEntries x kCacheEntryBytes.
constexpr std::uint64_t kEngineCacheEntries = 1048576ull;
constexpr std::uint64_t kEngineCacheBytes = kEngineCacheEntries * 32ull;

float env_value(const char* name, float fallback) {
    char value[32]{};
    std::size_t length = 0u;
    if (getenv_s(&length, value, sizeof(value), name) != 0 || length <= 1u) {
        return fallback;
    }
    return std::strtof(value, nullptr);
}

D3D12_RESOURCE_BARRIER all_uavs() {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    return barrier;
}

}  // namespace

// The engine's buffers: bytes per pixel (full resolution) or per half-resolution sample.
const SceneRayTracer::EngineBufferSpec SceneRayTracer::kEngineBufferSpecs[kEngineBuffers] = {
    {32u, false, L"rt engine gbuffer 0"},   {32u, false, L"rt engine gbuffer 1"},
    {16u, false, L"rt engine di temp"},     {16u, false, L"rt engine di 0"},
    {16u, false, L"rt engine di 1"},        {16u, false, L"rt engine direct"},
    {32u, true, L"rt engine gi temp"},      {32u, true, L"rt engine gi 0"},
    {32u, true, L"rt engine gi 1"},         {8u, true, L"rt engine gi out"},
    {8u, false, L"rt engine specular"},     {16u, false, L"rt engine diffuse history 0"},
    {16u, false, L"rt engine diffuse history 1"}, {16u, false, L"rt engine specular history 0"},
    {16u, false, L"rt engine specular history 1"}, {16u, false, L"rt engine a-trous a"},
    {16u, false, L"rt engine a-trous b"},   {4u, false, L"rt engine shadow 0"},
    {4u, false, L"rt engine shadow 1"},
};

bool SceneRayTracer::create_engine_pipelines() {
    engine_view_ = static_cast<std::uint32_t>(std::clamp(env_value("GALAXY_RT_ENGINE_VIEW", 0.0f), 0.0f, 15.0f));
    engine_indirect_ = env_value("GALAXY_RT_ENGINE_INDIRECT", 1.0f) != 0.0f;
    engine_specular_ = env_value("GALAXY_RT_ENGINE_SPECULAR", 1.0f) != 0.0f;
    engine_points_ = env_value("GALAXY_RT_ENGINE_POINTS", 1.0f) != 0.0f;
    engine_bounces_ = env_value("GALAXY_RT_ENGINE_BOUNCES", 1.0f) != 0.0f;
    engine_anti_lag_ = env_value("GALAXY_RT_ENGINE_ANTILAG", 1.0f) != 0.0f;
    engine_light_history_ = env_value("GALAXY_RT_ENGINE_LIGHT_HISTORY", 1.0f) != 0.0f;
    engine_boiling_ = env_value("GALAXY_RT_ENGINE_BOILING", 1.0f) != 0.0f;
    engine_footprint_ = env_value("GALAXY_RT_ENGINE_FOOTPRINT", 1.0f) != 0.0f;
    engine_motion_wanted_ = env_value("GALAXY_RT_ENGINE_MOTION", 1.0f) != 0.0f;
    engine_cache_wanted_ = env_value("GALAXY_RT_ENGINE_CACHE", 1.0f) != 0.0f;
    engine_firefly_ = env_value("GALAXY_RT_ENGINE_FIREFLY", 1.0f) != 0.0f;
    engine_di_spatial_ = env_value("GALAXY_RT_ENGINE_DI_SPATIAL", 1.0f) != 0.0f;
    engine_gi_full_env_ = static_cast<int>(std::clamp(env_value("GALAXY_RT_ENGINE_GI_FULL", -1.0f), -1.0f, 1.0f));
    engine_clear_mode_ = static_cast<std::uint32_t>(std::clamp(env_value("GALAXY_RT_ENGINE_CLEAR", 1.0f), 0.0f, 10.0f));
    engine_clear_wanted_ = engine_clear_mode_ != 0u;
    engine_reference_ = env_value("GALAXY_RT_ENGINE_REFERENCE", 0.0f) != 0.0f;
    water_light_ = env_value("GALAXY_RT_WATER_LIGHT", 1.0f) != 0.0f;
    engine_reference_bounces_ =
        static_cast<std::uint32_t>(std::clamp(env_value("GALAXY_RT_ENGINE_REFERENCE_BOUNCES", 8.0f), 0.0f, 32.0f));
    if (!light_textured_ || !light_root_) {
        return false;
    }
    const auto compute = [&](const void* code, std::size_t size, ComPtr<ID3D12PipelineState>& out) {
        D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};
        desc.pRootSignature = light_root_.Get();
        desc.CS = {code, size};
        return SUCCEEDED(device_->CreateComputePipelineState(&desc, IID_PPV_ARGS(&out)));
    };
    const bool ok =
        compute(g_rt_engine_gbuffer_cs, sizeof(g_rt_engine_gbuffer_cs), engine_gbuffer_pso_) &&
        compute(g_rt_engine_direct_cs, sizeof(g_rt_engine_direct_cs), engine_direct_pso_) &&
        compute(g_rt_engine_direct_spatial_cs, sizeof(g_rt_engine_direct_spatial_cs), engine_direct_spatial_pso_) &&
        compute(g_rt_engine_light_grid_cs, sizeof(g_rt_engine_light_grid_cs), engine_light_grid_pso_) &&
        compute(g_rt_engine_cache_cs, sizeof(g_rt_engine_cache_cs), engine_cache_pso_) &&
        compute(g_rt_engine_shadow_blur_cs, sizeof(g_rt_engine_shadow_blur_cs), engine_shadow_blur_pso_) &&
        compute(g_rt_engine_shadow_post_cs, sizeof(g_rt_engine_shadow_post_cs), engine_shadow_post_pso_) &&
        compute(g_rt_engine_shadow_stabilize_cs, sizeof(g_rt_engine_shadow_stabilize_cs),
                engine_shadow_stabilize_pso_) &&
        compute(g_rt_engine_indirect_cs, sizeof(g_rt_engine_indirect_cs), engine_indirect_pso_) &&
        compute(g_rt_engine_indirect_spatial_cs, sizeof(g_rt_engine_indirect_spatial_cs),
                engine_indirect_spatial_pso_) &&
        compute(g_rt_engine_specular_cs, sizeof(g_rt_engine_specular_cs), engine_specular_pso_) &&
        compute(g_rt_engine_temporal_cs, sizeof(g_rt_engine_temporal_cs), engine_temporal_pso_) &&
        compute(g_rt_engine_atrous_cs, sizeof(g_rt_engine_atrous_cs), engine_atrous_pso_) &&
        compute(g_rt_engine_final_cs, sizeof(g_rt_engine_final_cs), engine_final_pso_) &&
        compute(g_rt_engine_clear_cs, sizeof(g_rt_engine_clear_cs), engine_clear_pso_) &&
        compute(g_rt_engine_reference_cs, sizeof(g_rt_engine_reference_cs), engine_reference_pso_);
    if (!ok) {
        std::cerr << "[rt] engine pipelines failed\n";
        return false;
    }
    std::cerr << "[rt] engine on view=" << engine_view_ << " indirect=" << (engine_indirect_ ? 1 : 0)
              << " specular=" << (engine_specular_ ? 1 : 0) << " reference=" << (engine_reference_ ? 1 : 0)
              << " bounces=" << engine_reference_bounces_ << '\n';
    return true;
}

std::uint32_t SceneRayTracer::buffer_uav(ID3D12Resource* resource, std::uint64_t bytes) {
    if (!texture_views_usable_ || resource == nullptr || !device_ || bytes < 4u ||
        texture_view_count_ >= kTextureViewsPerScene) {
        return 0xFFFFFFFFu;
    }
    const std::uint32_t slot = texture_view_scene_ * kTextureViewsPerScene + texture_view_count_++;
    D3D12_CPU_DESCRIPTOR_HANDLE destination = texture_view_cpu_;
    destination.ptr += static_cast<SIZE_T>(slot) * texture_view_increment_;
    D3D12_UNORDERED_ACCESS_VIEW_DESC view{};
    view.Format = DXGI_FORMAT_R32_TYPELESS;
    view.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    view.Buffer.NumElements = static_cast<UINT>(std::min<std::uint64_t>(bytes / 4u, 0x7FFFFFFFu));
    view.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
    device_->CreateUnorderedAccessView(resource, nullptr, &view, destination);
    texture_view_blocks_[texture_view_scene_].resources.emplace_back(resource);
    return texture_view_first_ + slot;
}

std::uint32_t SceneRayTracer::texture_srv(ID3D12Resource* resource) {
    if (!texture_views_usable_ || resource == nullptr || !device_ || texture_view_count_ >= kTextureViewsPerScene) {
        return 0xFFFFFFFFu;
    }
    const std::uint32_t slot = texture_view_scene_ * kTextureViewsPerScene + texture_view_count_++;
    D3D12_CPU_DESCRIPTOR_HANDLE destination = texture_view_cpu_;
    destination.ptr += static_cast<SIZE_T>(slot) * texture_view_increment_;
    D3D12_SHADER_RESOURCE_VIEW_DESC view{};
    view.Format = resource->GetDesc().Format;
    view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    view.Texture2D.MipLevels = 1u;
    device_->CreateShaderResourceView(resource, &view, destination);
    texture_view_blocks_[texture_view_scene_].resources.emplace_back(resource);
    return texture_view_first_ + slot;
}

bool SceneRayTracer::ensure_engine_buffers(std::uint32_t width, std::uint32_t height) {
    if (!engine_light_grid_ &&
        !create_buffer(kEngineGridBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                       D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, engine_light_grid_, L"rt engine light grid")) {
        engine_light_grid_.Reset();
        return false;
    }
    // The radiance cache (created zeroed: every entry free); without it the hits keep their own samples.
    if (!engine_radiance_cache_ &&
        !create_buffer(kEngineCacheBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                       D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, engine_radiance_cache_, L"rt engine radiance cache")) {
        engine_radiance_cache_.Reset();
    }
    if (width == engine_width_ && height == engine_height_ && engine_buffers_[0]) {
        return true;
    }
    for (auto& buffer : engine_buffers_) retire(buffer);
    for (auto& buffer : engine_slots_) retire(buffer);
    for (auto& buffer : engine_clear_) retire(buffer);
    engine_clear_valid_ = {};
    engine_width_ = engine_height_ = 0u;
    const std::uint64_t full = static_cast<std::uint64_t>(width) * height;
    // The indirect light's buffers: every pixel at High (GALAXY_RT_ENGINE_GI_FULL=0/1 overrides), else one of each 2 x 2.
    engine_gi_full_ = engine_gi_full_env_ >= 0 ? engine_gi_full_env_ != 0 : high_quality_;
    const std::uint64_t half =
        engine_gi_full_ ? full : static_cast<std::uint64_t>((width + 1u) / 2u) * ((height + 1u) / 2u);
    for (unsigned i = 0; i < kEngineBuffers; ++i) {
        const EngineBufferSpec& spec = kEngineBufferSpecs[i];
        if (!create_buffer((spec.half ? half : full) * spec.bytes, D3D12_HEAP_TYPE_DEFAULT,
                           D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                           engine_buffers_[i], spec.name)) {
            for (auto& buffer : engine_buffers_) buffer.Reset();
            return false;
        }
    }
    for (unsigned i = 0; i < kLightSlots; ++i) {
        if (!create_buffer(full * 16u, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                           D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, engine_slots_[i], L"rt engine slot")) {
            for (auto& buffer : engine_buffers_) buffer.Reset();
            for (auto& buffer : engine_slots_) buffer.Reset();
            return false;
        }
        engine_slot_state_[i] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        engine_slot_valid_[i] = false;
    }
    engine_width_ = width;
    engine_height_ = height;
    engine_fresh_ = true;
    std::cerr << "[rt] engine buffers " << width << 'x' << height << " mb="
              << ((full * (32u * 2u + 16u * 3u + 16u + 8u + 16u * 6u + 4u * 2u + 16u * kLightSlots) + half * (32u * 3u + 8u)) >>
                  20u)
              << '\n';
    return true;
}

std::uint32_t SceneRayTracer::light_remap(Allocate allocate, void* allocate_context, std::uint32_t scene,
                                          std::uint32_t lights, const std::uint32_t* points, std::uint32_t point_count) {
    // This trace's lights by draw: the scene's emitting draws (when they are among its lights), then the glow emitters
    // the last frame wrote (rt_lighting.hlsl emit_triangle_of: from `scene` on).
    light_groups_.clear();
    if (scene != 0u) {
        for (std::size_t r = 0; r < emit_ranges_.size() && r < emit_range_keys_.size(); ++r) {
            const auto& range = emit_ranges_[r];
            if (range[3] < scene && emit_range_keys_[r] != 0u) {
                light_groups_.push_back({emit_range_keys_[r], range[3], std::min(range[1], scene - range[3])});
            }
        }
    }
    for (const auto& glow : glow_written_) {
        if (scene + glow[1] < lights && glow[0] != 0u) {
            light_groups_.push_back({glow[0], scene + glow[1], std::min(glow[2], lights - scene - glow[1])});
        }
    }
    light_points_.assign(points, points + point_count);
    // A draw drawn again under its key keeps its lights, index for index; a key two draws share (in either trace) keeps
    // none.
    constexpr std::size_t kShared = ~std::size_t{0};
    std::unordered_map<std::uint32_t, std::size_t> last;
    last.reserve(light_groups_last_.size());
    for (std::size_t i = 0; i < light_groups_last_.size(); ++i) {
        const auto [it, inserted] = last.try_emplace(light_groups_last_[i].key, i);
        if (!inserted) {
            it->second = kShared;
        }
    }
    std::unordered_map<std::uint32_t, std::uint32_t> now;
    now.reserve(light_groups_.size());
    for (const LightGroup& group : light_groups_) {
        ++now[group.key];
    }
    std::vector<std::array<std::uint32_t, 4>> ranges;
    ranges.reserve(light_groups_.size());
    for (const LightGroup& group : light_groups_) {
        const auto it = last.find(group.key);
        if (it == last.end() || it->second == kShared || now[group.key] != 1u) {
            continue;
        }
        const LightGroup& before = light_groups_last_[it->second];
        ranges.push_back({before.first, std::min(before.count, group.count), group.first, 0u});
    }
    std::sort(ranges.begin(), ranges.end(), [](const auto& a, const auto& b) { return a[0] < b[0]; });
    std::vector<std::uint32_t> point_map(light_points_last_.size(), 0xFFFFFFFFu);
    std::uint32_t points_kept = 0u;
    for (std::size_t i = 0; i < light_points_last_.size(); ++i) {
        for (std::uint32_t j = 0; j < point_count; ++j) {
            if (points[j] == light_points_last_[i] && points[j] != 0u) {
                point_map[i] = j;
                ++points_kept;
                break;
            }
        }
    }
    // GALAXY_RT_LOG_EMITTERS: every 120th trace, how many of its lights the last trace had (by draw and by light).
    if (log_emitters_ && ++light_remap_traces_ % 120u == 0u) [[unlikely]] {
        std::uint64_t kept = 0u;
        for (const auto& range : ranges) {
            kept += range[1];
        }
        std::uint64_t total = 0u;
        for (const LightGroup& group : light_groups_) {
            total += group.count;
        }
        std::fprintf(stderr,
                     "[rt-remap] draws=%zu kept=%zu (last %zu) lights=%llu kept=%llu points=%u kept=%u "
                     "(last %zu)\n",
                     light_groups_.size(), ranges.size(), light_groups_last_.size(),
                     static_cast<unsigned long long>(total), static_cast<unsigned long long>(kept), point_count,
                     points_kept, light_points_last_.size());
    }
    // The other way (rt_engine.hlsl engine_light_existed): the same ranges by their first index now, and per point
    // light now whether it was.
    std::vector<std::array<std::uint32_t, 4>> now_ranges;
    now_ranges.reserve(ranges.size());
    for (const auto& range : ranges) {
        now_ranges.push_back({range[2], range[1], range[0], 0u});
    }
    std::sort(now_ranges.begin(), now_ranges.end(), [](const auto& a, const auto& b) { return a[0] < b[0]; });
    std::vector<std::uint32_t> point_existed(point_count, 0u);
    for (const std::uint32_t index : point_map) {
        if (index < point_count) {
            point_existed[index] = 1u;
        }
    }
    const std::size_t bytes =
        16u + ranges.size() * 16u + point_map.size() * 4u + now_ranges.size() * 16u + point_existed.size() * 4u + 4u;
    const RtUpload upload = allocate(allocate_context, bytes, 256u);
    if (upload.cpu == nullptr) {
        return 0xFFFFFFFFu;
    }
    auto* const words = static_cast<std::uint32_t*>(upload.cpu);
    words[0] = static_cast<std::uint32_t>(ranges.size());
    words[1] = static_cast<std::uint32_t>(point_map.size());
    words[2] = static_cast<std::uint32_t>(now_ranges.size());
    words[3] = static_cast<std::uint32_t>(point_existed.size());
    std::uint32_t* at = words + 4u;
    std::memcpy(at, ranges.data(), ranges.size() * 16u);
    at += ranges.size() * 4u;
    std::memcpy(at, point_map.data(), point_map.size() * 4u);
    at += point_map.size();
    std::memcpy(at, now_ranges.data(), now_ranges.size() * 16u);
    at += now_ranges.size() * 4u;
    std::memcpy(at, point_existed.data(), point_existed.size() * 4u);
    at[point_existed.size()] = 0u;
    return buffer_view(upload.resource, upload.offset, bytes);
}

void SceneRayTracer::light_remap_commit(bool ran) {
    if (ran) {
        light_groups_last_.swap(light_groups_);
        light_points_last_.swap(light_points_);
    } else {
        light_groups_last_.clear();
        light_points_last_.clear();
    }
}

bool SceneRayTracer::engine_prepare(std::uint32_t width, std::uint32_t height, unsigned slot, RtEngineConstants& c,
                                    std::uint32_t& flags) {
    static_assert(kEvWaterScene == kEngineClearViews, "the clear pass's views follow the trace's");
    engine_running_ = false;
    if (!engine_active() || engine_picture_ == nullptr || !ensure_engine_buffers(width, height)) {
        // A trace without the engine leaves its histories stale.
        engine_fresh_ = true;
        return false;
    }
    const unsigned cur = engine_parity_;
    const unsigned prev = cur ^ 1u;
    const auto full = static_cast<std::uint64_t>(width) * height;
    const auto half = engine_gi_full_ ? full : static_cast<std::uint64_t>((width + 1u) / 2u) * ((height + 1u) / 2u);
    const auto uav = [&](unsigned buffer) {
        const EngineBufferSpec& spec = kEngineBufferSpecs[buffer];
        return buffer_uav(engine_buffers_[buffer].Get(), (spec.half ? half : full) * spec.bytes);
    };
    std::uint32_t views[kEvCount]{};
    views[kEvGBuffer] = uav(kGb0 + cur);
    views[kEvGBufferPrev] = uav(kGb0 + prev);
    views[kEvDiTemp] = uav(kDiTemp);
    views[kEvDi] = uav(kDi0 + cur);
    views[kEvDiPrev] = uav(kDi0 + prev);
    views[kEvDirect] = uav(kDirect);
    views[kEvGiTemp] = uav(kGiTemp);
    views[kEvGi] = uav(kGi0 + cur);
    views[kEvGiPrev] = uav(kGi0 + prev);
    views[kEvGiOut] = uav(kGiOut);
    views[kEvSpecular] = uav(kSpecular);
    views[kEvHistDiffuse] = uav(kHistD0 + cur);
    views[kEvHistDiffusePrev] = uav(kHistD0 + prev);
    views[kEvHistSpecular] = uav(kHistS0 + cur);
    views[kEvHistSpecularPrev] = uav(kHistS0 + prev);
    views[kEvAtrousA] = uav(kAtrousA);
    views[kEvAtrousB] = uav(kAtrousB);
    views[kEvSlot] = buffer_uav(engine_slots_[slot].Get(), full * 16u);
    views[kEvPicture] = texture_srv(engine_picture_);
    views[kEvShadow] = uav(kShadow0 + cur);
    views[kEvShadowPrev] = uav(kShadow0 + prev);
    views[kEvLightGrid] = buffer_uav(engine_light_grid_.Get(), kEngineGridBytes);
    views[kEvLightRemap] = light_remap_view_;
    for (std::uint32_t v = 0; v < kEvCount; ++v) {
        if (views[v] == 0xFFFFFFFFu) {
            // Once per view: the trace runs without the engine while it has none.
            static std::uint32_t logged = 0u;
            if ((logged & (1u << v)) == 0u) {
                logged |= 1u << v;
                std::cerr << "[rt] engine view " << v << " unavailable: the scene is not traced\n";
            }
            engine_fresh_ = true;
            return false;
        }
    }
    std::copy(std::begin(views), std::end(views), c.views);
    // The raster's motion, where the scene wrote it (RendererD3D12 scene_motion_): the histories follow what moved by
    // itself (rt_engine.hlsl engine_history).
    // The world's radiance cache for the hits' light (rt_engine.hlsl engine_cache_light).
    bool cache = false;
    if (engine_cache_wanted_ && engine_radiance_cache_) {
        const std::uint32_t view = buffer_uav(engine_radiance_cache_.Get(), kEngineCacheBytes);
        if (view != 0xFFFFFFFFu) {
            c.views[kEvRadianceCache] = view;
            cache = true;
        }
    }
    bool motion = false;
    if (engine_motion_ != nullptr && engine_motion_wanted_) {
        const std::uint32_t view = texture_srv(engine_motion_);
        if (view != 0xFFFFFFFFu) {
            c.views[kEvMotion] = view;
            motion = true;
        }
    }
    // Light under water reads the last water pass's surfaces; the next water pass rebuilds them after this trace.
    bool water_light = false;
    if (water_light_ && water_light_triangles_ != 0u && water_tlas_ && water_positions_ &&
        water_positions_state_ == D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) {
        const std::uint32_t scene = tlas_view(water_tlas_->GetGPUVirtualAddress());
        const std::uint32_t positions =
            buffer_view(water_positions_.Get(), 0u, static_cast<std::uint64_t>(water_light_triangles_) * 36u);
        if (scene != 0xFFFFFFFFu && positions != 0xFFFFFFFFu) {
            c.views[kEvWaterScene] = scene;
            c.views[kEvWaterPositions] = positions;
            water_light = true;
        }
    }
    c.size[0] = width;
    c.size[1] = height;
    c.size[2] = engine_gi_full_ ? width : (width + 1u) / 2u;
    c.size[3] = engine_gi_full_ ? height : (height + 1u) / 2u;
    c.info[0] = kEngineOn | (engine_specular_ && surface_reflections_active() ? kEngineSpecular : 0u) | (engine_indirect_ ? kEngineIndirect : 0u) |
                (engine_points_ ? kEnginePoints : 0u) | (engine_clear_mode_ == 2u ? kEngineClearFind : 0u) |
                (engine_clear_mode_ == 3u ? kEngineClearNoRay : 0u) | (engine_clear_mode_ == 4u ? kEngineClearNoSun : 0u) |
                (engine_clear_mode_ == 5u ? kEngineClearNoBack : 0u) | (engine_clear_mode_ == 6u ? kEngineClearNoShade : 0u) |
                (engine_clear_mode_ == 7u ? kEngineHitNoLights : 0u) | (engine_clear_mode_ == 8u ? kEngineHitNoSky : 0u) |
                (engine_clear_mode_ == 9u ? kEngineClearNoTev : 0u) | (engine_bounces_ ? kEngineBounceCache : 0u) |
                (water_light ? kEngineWaterLight : 0u) | (engine_anti_lag_ ? 0u : kEngineNoAntiLag) |
                (engine_light_history_ ? 0u : kEngineNoLightHistory) |
                (engine_clear_mode_ == 10u ? kEngineClearSolidLeft : 0u) | (engine_boiling_ ? 0u : kEngineNoBoiling) |
                (motion ? kEngineMotion : 0u) | (engine_footprint_ ? 0u : kEngineNoFootprint) |
                (cache ? 0u : kEngineNoCache) | (engine_firefly_ ? 0u : kEngineNoFirefly) |
                (engine_di_spatial_ ? 0u : kEngineNoDiSpatial) | (engine_gi_full_ ? kEngineGiFull : 0u) |
                (engine_view_ << 4u);
    c.info[1] = 0u;
    c.info[2] = slot;
    c.info[3] = 0u;
    // GALAXY_RT_ENGINE_REFERENCE: a converged reference for a still view, one path per present from
    // GALAXY_RT_REFERENCE_FROM on; its sums restart when the picture's size changes.
    engine_reference_running_ = false;
    if (engine_reference_ && engine_reference_pso_ && presents_ >= reference_from_ &&
        presents_ != engine_reference_present_) {
        if (!engine_reference_sums_ || engine_reference_pixels_ != full) {
            retire(engine_reference_sums_);
            engine_reference_pixels_ = 0u;
            engine_reference_paths_ = 0u;
            if (create_buffer(full * 16u, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                              D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, engine_reference_sums_, L"rt engine reference")) {
                engine_reference_pixels_ = full;
            }
        }
        const std::uint32_t view =
            engine_reference_pixels_ == full ? buffer_uav(engine_reference_sums_.Get(), full * 16u) : 0xFFFFFFFFu;
        if (view != 0xFFFFFFFFu) {
            c.params[0] = std::bit_cast<float>(view);
            c.params[1] = static_cast<float>(engine_reference_paths_);
            c.params[2] = std::bit_cast<float>(engine_reference_bounces_);
            c.info[0] |= kEngineReference;
            engine_reference_running_ = true;
            engine_reference_present_ = presents_;
        }
    }
    if (engine_fresh_) {
        // New buffers: no history to reuse.
        flags &= ~1u;
    }
    engine_running_ = true;
    return true;
}

void SceneRayTracer::trace_engine(ID3D12GraphicsCommandList* list, Allocate allocate, void* allocate_context,
                                  const void* constants, std::size_t bytes, std::size_t engine_at,
                                  D3D12_GPU_VIRTUAL_ADDRESS frame, unsigned slot) {
    const UINT gx = (engine_width_ + 7u) / 8u;
    const UINT gy = (engine_height_ + 7u) / 8u;
    const UINT hx = ((engine_gi_full_ ? engine_width_ : (engine_width_ + 1u) / 2u) + 7u) / 8u;
    const UINT hy = ((engine_gi_full_ ? engine_height_ : (engine_height_ + 1u) / 2u) + 7u) / 8u;
    const D3D12_RESOURCE_BARRIER barrier = all_uavs();
    to_state(list, engine_slots_[slot].Get(), engine_slot_state_[slot], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    const auto run = [&](ID3D12PipelineState* pso, UINT x, UINT y, const wchar_t* name) {
        dred_marker(list, name);
        list->SetPipelineState(pso);
        list->Dispatch(x, y, 1u);
        list->ResourceBarrier(1u, &barrier);
    };
    // GALAXY_RT_PASS_TIMING: each stage's GPU time ([rt-engine-pass]).
    int timed = begin_pass(list);
    run(engine_gbuffer_pso_.Get(), gx, gy, L"engine_gbuffer");
    end_pass(list, timed, kPassEngineGBuffer);
    // The light grid (the lights near each cell about the camera), for the surfaces' and the hits' light candidates.
    timed = begin_pass(list);
    run(engine_light_grid_pso_.Get(), static_cast<UINT>((kEngineGridBytes / 8u + 63u) / 64u), 1u,
        L"engine_light_grid");
    if (engine_reference_running_) {
        run(engine_reference_pso_.Get(), gx, gy, L"engine_reference");
        ++engine_reference_paths_;
    }
    run(engine_direct_pso_.Get(), gx, gy, L"engine_direct");
    run(engine_direct_spatial_pso_.Get(), gx, gy, L"engine_direct_spatial");
    end_pass(list, timed, kPassEngineDirect);
    // The sun's shadow (its blurs in the a-trous buffers, free until the temporal pass).
    timed = begin_pass(list);
    run(engine_shadow_blur_pso_.Get(), gx, gy, L"engine_shadow_blur");
    run(engine_shadow_post_pso_.Get(), gx, gy, L"engine_shadow_post");
    run(engine_shadow_stabilize_pso_.Get(), gx, gy, L"engine_shadow_stabilize");
    end_pass(list, timed, kPassEngineShadow);
    timed = begin_pass(list);
    run(engine_indirect_pso_.Get(), hx, hy, L"engine_indirect");
    run(engine_indirect_spatial_pso_.Get(), hx, hy, L"engine_indirect_spatial");
    end_pass(list, timed, kPassEngineIndirect);
    timed = begin_pass(list);
    run(engine_specular_pso_.Get(), gx, gy, L"engine_specular");
    // The radiance cache takes the frame's samples of the bounce and reflection hits (read by the next frame's).
    run(engine_cache_pso_.Get(), static_cast<UINT>((kEngineCacheEntries + 63u) / 64u), 1u, L"engine_cache");
    end_pass(list, timed, kPassEngineSpecular);
    timed = begin_pass(list);
    run(engine_temporal_pso_.Get(), gx, gy, L"engine_temporal");
    // The a-trous iterations (steps 1, 2, 4, 8 and on: A to B, B to A, ...) and the final pass, each with its step in
    // the frame's constants.
    const auto with_info = [&](std::uint32_t step, std::uint32_t parity) {
        const RtUpload upload = allocate(allocate_context, bytes, 256u);
        if (upload.cpu == nullptr) {
            return false;
        }
        std::memcpy(upload.cpu, constants, bytes);
        auto* const engine = reinterpret_cast<RtEngineConstants*>(static_cast<std::byte*>(upload.cpu) + engine_at);
        engine->info[1] = step;
        engine->info[3] = parity;
        list->SetComputeRootConstantBufferView(0u, upload.gpu);
        return true;
    };
    // Four at the engine's reference picture (rt_engine.hlsl kFootprintLines, 1056 lines), one more each time the lines
    // double: the filter's reach stays the same share of the picture (6x at 2560 x 1440: five, steps to 16).
    const double lines = engine_footprint_ ? std::max(static_cast<double>(engine_height_) / 1056.0, 1.0) : 1.0;
    const std::uint32_t iterations = 4u + static_cast<std::uint32_t>(std::lround(std::log2(lines)));
    for (std::uint32_t i = 0; i < iterations; ++i) {
        if (with_info(1u << i, i & 1u)) {
            run(engine_atrous_pso_.Get(), gx, gy, L"engine_atrous");
        }
    }
    end_pass(list, timed, kPassEngineDenoise);
    timed = begin_pass(list);
    // The result is where the last iteration wrote it (A after an even count, B after an odd one).
    if (with_info(0u, iterations & 1u)) {
        run(engine_final_pso_.Get(), gx, gy, L"engine_final");
    }
    end_pass(list, timed, kPassEngineFinal);
    list->SetComputeRootConstantBufferView(0u, frame);
    engine_slot_valid_[slot] = true;
    engine_parity_ ^= 1u;
    engine_fresh_ = false;
}

bool SceneRayTracer::engine_slot(unsigned slot, std::uint32_t width, std::uint32_t height) const noexcept {
    return slot < kLightSlots && engine_slot_valid_[slot] && engine_slots_[slot] && width == engine_width_ &&
           height == engine_height_;
}

}  // namespace galaxy::gx
