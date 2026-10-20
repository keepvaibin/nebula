// SPDX-License-Identifier: GPL-3.0-only
// SceneRayTracer: the ray tracing engine (rt_engine.hlsl, docs/RAY_TRACING_ENGINE.md). Under physical light with the
// textured lighting (shader model 6.6) it takes the lighting's place between the emitters and the composite: the
// G-buffer, ReSTIR DI, ReSTIR GI, GGX specular and the denoiser, into the slot's full-resolution result that the
// composite and the sub-frames read. Its buffers stay unordered-access (the slot results excepted) and are reached
// through views in the scene's descriptor block (rt_engine.hlsl g_engine_views).
#include "galaxy/gx/ray_tracing.h"

#include "rt_engine_atrous_cs.h"
#include "rt_engine_clear_cs.h"
#include "rt_engine_direct_cs.h"
#include "rt_engine_direct_spatial_cs.h"
#include "rt_engine_final_cs.h"
#include "rt_engine_gbuffer_cs.h"
#include "rt_engine_indirect_cs.h"
#include "rt_engine_indirect_spatial_cs.h"
#include "rt_engine_specular_cs.h"
#include "rt_engine_temporal_cs.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>

namespace galaxy::gx {
namespace {

using Microsoft::WRL::ComPtr;

// rt_engine.hlsl kEv*.
enum EngineView : std::uint32_t {
    kEvGBuffer, kEvGBufferPrev, kEvDiTemp, kEvDi, kEvDiPrev, kEvDirect, kEvGiTemp, kEvGi, kEvGiPrev, kEvGiOut,
    kEvSpecular, kEvHistDiffuse, kEvHistDiffusePrev, kEvHistSpecular, kEvHistSpecularPrev, kEvAtrousA, kEvAtrousB,
    kEvSlot, kEvPicture, kEvCount,
    // The clear-surface pass's (trace_engine_clear), after the trace's.
    kEvWaterScene = kEvCount, kEvWaterPositions, kEvWaterSurfaces, kEvClearDraws, kEvClear, kEvClearEnd,
};
static_assert(kEvClearEnd <= 24u);

// rt_engine.hlsl kEngine*.
constexpr std::uint32_t kEngineOn = 1u;
constexpr std::uint32_t kEngineSpecular = 2u;
constexpr std::uint32_t kEngineIndirect = 4u;
constexpr std::uint32_t kEnginePoints = 256u;
constexpr std::uint32_t kEngineClearFind = 512u;

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
    {32u, false, L"rt engine di temp"},     {32u, false, L"rt engine di 0"},
    {32u, false, L"rt engine di 1"},        {16u, false, L"rt engine direct"},
    {32u, true, L"rt engine gi temp"},      {32u, true, L"rt engine gi 0"},
    {32u, true, L"rt engine gi 1"},         {8u, true, L"rt engine gi out"},
    {8u, false, L"rt engine specular"},     {16u, false, L"rt engine diffuse history 0"},
    {16u, false, L"rt engine diffuse history 1"}, {16u, false, L"rt engine specular history 0"},
    {16u, false, L"rt engine specular history 1"}, {16u, false, L"rt engine a-trous a"},
    {16u, false, L"rt engine a-trous b"},
};

bool SceneRayTracer::create_engine_pipelines() {
    engine_wanted_ = env_value("GALAXY_RT_ENGINE", 1.0f) != 0.0f;
    engine_view_ = static_cast<std::uint32_t>(std::clamp(env_value("GALAXY_RT_ENGINE_VIEW", 0.0f), 0.0f, 15.0f));
    engine_indirect_ = env_value("GALAXY_RT_ENGINE_INDIRECT", 1.0f) != 0.0f;
    engine_specular_ = env_value("GALAXY_RT_ENGINE_SPECULAR", 1.0f) != 0.0f;
    engine_points_ = env_value("GALAXY_RT_ENGINE_POINTS", 1.0f) != 0.0f;
    engine_clear_mode_ = static_cast<std::uint32_t>(std::clamp(env_value("GALAXY_RT_ENGINE_CLEAR", 1.0f), 0.0f, 2.0f));
    engine_clear_wanted_ = engine_clear_mode_ != 0u;
    if (!engine_wanted_ || !light_textured_ || !light_root_) {
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
        compute(g_rt_engine_indirect_cs, sizeof(g_rt_engine_indirect_cs), engine_indirect_pso_) &&
        compute(g_rt_engine_indirect_spatial_cs, sizeof(g_rt_engine_indirect_spatial_cs),
                engine_indirect_spatial_pso_) &&
        compute(g_rt_engine_specular_cs, sizeof(g_rt_engine_specular_cs), engine_specular_pso_) &&
        compute(g_rt_engine_temporal_cs, sizeof(g_rt_engine_temporal_cs), engine_temporal_pso_) &&
        compute(g_rt_engine_atrous_cs, sizeof(g_rt_engine_atrous_cs), engine_atrous_pso_) &&
        compute(g_rt_engine_final_cs, sizeof(g_rt_engine_final_cs), engine_final_pso_) &&
        compute(g_rt_engine_clear_cs, sizeof(g_rt_engine_clear_cs), engine_clear_pso_);
    if (!ok) {
        std::cerr << "[rt] engine pipelines failed: the lighting's own passes run\n";
        return false;
    }
    std::cerr << "[rt] engine on view=" << engine_view_ << " indirect=" << (engine_indirect_ ? 1 : 0)
              << " specular=" << (engine_specular_ ? 1 : 0) << '\n';
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
    if (width == engine_width_ && height == engine_height_ && engine_buffers_[0]) {
        return true;
    }
    for (auto& buffer : engine_buffers_) retire(buffer);
    for (auto& buffer : engine_slots_) retire(buffer);
    for (auto& buffer : engine_clear_) retire(buffer);
    engine_clear_valid_ = {};
    engine_width_ = engine_height_ = 0u;
    const std::uint64_t full = static_cast<std::uint64_t>(width) * height;
    const std::uint64_t half = static_cast<std::uint64_t>((width + 1u) / 2u) * ((height + 1u) / 2u);
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
              << ((full * (32u * 5u + 16u + 8u + 16u * 6u + 16u * kLightSlots) + half * (32u * 3u + 8u)) >> 20u) << '\n';
    return true;
}

bool SceneRayTracer::engine_prepare(std::uint32_t width, std::uint32_t height, unsigned slot, RtEngineConstants& c,
                                    std::uint32_t& flags) {
    engine_running_ = false;
    if (!engine_active() || engine_picture_ == nullptr || !ensure_engine_buffers(width, height)) {
        // A trace without it: its histories are stale when it runs again.
        engine_fresh_ = true;
        return false;
    }
    const unsigned cur = engine_parity_;
    const unsigned prev = cur ^ 1u;
    const auto full = static_cast<std::uint64_t>(width) * height;
    const auto half = static_cast<std::uint64_t>((width + 1u) / 2u) * ((height + 1u) / 2u);
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
    for (const std::uint32_t view : views) {
        if (view == 0xFFFFFFFFu) {
            engine_fresh_ = true;
            return false;
        }
    }
    std::copy(std::begin(views), std::end(views), c.views);
    c.size[0] = width;
    c.size[1] = height;
    c.size[2] = (width + 1u) / 2u;
    c.size[3] = (height + 1u) / 2u;
    c.info[0] = kEngineOn | (engine_specular_ ? kEngineSpecular : 0u) | (engine_indirect_ ? kEngineIndirect : 0u) |
                (engine_points_ ? kEnginePoints : 0u) | (engine_clear_mode_ == 2u ? kEngineClearFind : 0u) |
                (engine_view_ << 4u);
    c.info[1] = 0u;
    c.info[2] = slot;
    c.info[3] = 0u;
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
    const UINT hx = ((engine_width_ + 1u) / 2u + 7u) / 8u;
    const UINT hy = ((engine_height_ + 1u) / 2u + 7u) / 8u;
    const D3D12_RESOURCE_BARRIER barrier = all_uavs();
    to_state(list, engine_slots_[slot].Get(), engine_slot_state_[slot], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    const auto run = [&](ID3D12PipelineState* pso, UINT x, UINT y) {
        list->SetPipelineState(pso);
        list->Dispatch(x, y, 1u);
        list->ResourceBarrier(1u, &barrier);
    };
    run(engine_gbuffer_pso_.Get(), gx, gy);
    run(engine_direct_pso_.Get(), gx, gy);
    run(engine_direct_spatial_pso_.Get(), gx, gy);
    run(engine_indirect_pso_.Get(), hx, hy);
    run(engine_indirect_spatial_pso_.Get(), hx, hy);
    run(engine_specular_pso_.Get(), gx, gy);
    run(engine_temporal_pso_.Get(), gx, gy);
    // The a-trous iterations (steps 1, 2, 4, 8: A to B, B to A, ...) and the final pass (the result in A), each with
    // its step in the frame's constants.
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
    for (std::uint32_t i = 0; i < 4u; ++i) {
        if (with_info(1u << i, i & 1u)) {
            run(engine_atrous_pso_.Get(), gx, gy);
        }
    }
    if (with_info(0u, 0u)) {
        run(engine_final_pso_.Get(), gx, gy);
    }
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
