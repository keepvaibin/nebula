// SPDX-License-Identifier: GPL-3.0-only
// SceneRayTracer's camera (rt_camera.hlsl): turns the HDR scene EFB's light into the RGBA8 EFB's picture with bloom
// and the tone curve. RendererD3D12::begin_hdr_scene / end_hdr_scene swap the EFBs.
#include "galaxy/gx/ray_tracing.h"
#include "galaxy/gx/render_config.h"
#include "galaxy/gx/rt_prepare.h"

#include "rt_camera_down_cs.h"
#include "rt_camera_ps.h"
#include "rt_camera_shafts_cs.h"
#include "rt_camera_up_cs.h"
#include "rt_camera_vs.h"
#include "rt_camera_widen_ps.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iostream>

namespace galaxy::gx {
namespace {

using Microsoft::WRL::ComPtr;

// rt_camera.hlsl RtCamera.
struct RtCameraConstants {
    std::uint32_t size[2];
    std::uint32_t level[2];
    std::uint32_t from[2];
    std::uint32_t level_at;
    std::uint32_t from_at;
    float bloom;
    std::uint32_t flags;
    std::uint32_t levels;
    std::uint32_t subframe;     // a sub-frame's light slot + 1 (the shafts' sun turns into its camera); 0 none
    float sun[4];               // the visible sun on the picture (pixels), the shafts' strength, the picture's height
    std::uint32_t quarter[2];   // the shafts' texels
    float far_depth;            // the far plane's depth (sky)
    float sky_distance;         // farther surfaces are the sky's backdrops
    float depth_params[4];      // D3D projection [2][2], [2][3]; viewport min and max depth
    float sun_dir[4];           // towards the sun (eye space); the air's Henyey-Greenstein asymmetry
    float projection[4];        // D3D projection [0][0], [0][2], [1][1], [1][2]
    float viewport[4];          // x, y, width, height (pixels)
};
static_assert(sizeof(RtCameraConstants) == 144u);

constexpr std::uint32_t kCameraPhysical = 1u;
constexpr std::uint32_t kCameraAces = 2u;
constexpr std::uint32_t kCameraClip = 4u;
constexpr std::uint32_t kCameraNeutral = 8u;
constexpr std::uint32_t kNoLevel = 0xFFFFFFFFu;
// Bloom pyramid levels: the coarsest is a 64th of the picture's side (20 texels at 1280 pixels, a blur about a fifth of
// the picture wide).
constexpr std::uint32_t kCameraLevels = 6u;

float env_value(const char* name, float fallback) {
    char value[32]{};
    std::size_t length = 0u;
    if (getenv_s(&length, value, sizeof(value), name) != 0 || length <= 1u) {
        return fallback;
    }
    return std::strtof(value, nullptr);
}

struct Level {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t at = 0;
};

// The pyramid's levels for a width x height picture (each half the one before, rounded up), their first texels.
std::uint32_t camera_levels(std::uint32_t width, std::uint32_t height, Level (&levels)[kCameraLevels]) {
    std::uint32_t count = 0u;
    std::uint32_t at = 0u;
    std::uint32_t w = width;
    std::uint32_t h = height;
    while (count < kCameraLevels && (w > 1u || h > 1u)) {
        w = (w + 1u) / 2u;
        h = (h + 1u) / 2u;
        levels[count] = {w, h, at};
        at += w * h;
        ++count;
    }
    return count;
}

}  // namespace

bool SceneRayTracer::create_camera_pipelines(DXGI_FORMAT scene_format, DXGI_FORMAT display_format) {
    camera_bloom_ = std::clamp(env_value("GALAXY_RT_CAMERA_BLOOM", 0.04f), 0.0f, 1.0f);
    camera_tone_ = static_cast<std::uint32_t>(std::clamp(env_value("GALAXY_RT_TONEMAP", 0.0f), 0.0f, 3.0f));
    camera_shafts_ = std::clamp(env_value("GALAXY_RT_CAMERA_SHAFTS", 1.0f), 0.0f, 8.0f);
    // The picture's colour and depth views (t0, t1: the renderer's EFB conversion table); the pyramid's down and up
    // levels (u0, u1) and the up levels again for the final draw (t2).
    D3D12_DESCRIPTOR_RANGE picture{};
    picture.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    picture.NumDescriptors = 2u;
    // A sub-frame's camera rows per light slot (t3: write_reprojection's buffer) for the shafts' sun.
    D3D12_ROOT_PARAMETER params[6]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].Constants.Num32BitValues = sizeof(RtCameraConstants) / 4u;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1u;
    params[1].DescriptorTable.pDescriptorRanges = &picture;
    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    params[3].Descriptor.ShaderRegister = 1u;
    params[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[4].Descriptor.ShaderRegister = 2u;
    params[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[5].Descriptor.ShaderRegister = 3u;
    for (auto& param : params) {
        param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
    D3D12_ROOT_SIGNATURE_DESC rsd{};
    rsd.NumParameters = 6u;
    rsd.pParameters = params;
    if (!rt_prepare::root_signature(device_.Get(), rsd, camera_root_)) {
        std::cerr << "[rt] camera root signature failed\n";
        return false;
    }
    const auto compute = [&](const void* code, std::size_t size, ComPtr<ID3D12PipelineState>& out) {
        D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};
        desc.pRootSignature = camera_root_.Get();
        desc.CS = {code, size};
        return rt_prepare::compute(device_.Get(), desc, out);
    };
    const auto graphics = [&](const void* code, std::size_t size, DXGI_FORMAT format, ComPtr<ID3D12PipelineState>& out) {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
        desc.pRootSignature = camera_root_.Get();
        desc.VS = {g_rt_camera_vs, sizeof(g_rt_camera_vs)};
        desc.PS = {code, size};
        desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        desc.SampleMask = UINT_MAX;
        desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        desc.RasterizerState.DepthClipEnable = TRUE;
        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        desc.NumRenderTargets = 1u;
        desc.RTVFormats[0] = format;
        desc.SampleDesc.Count = 1u;
        return rt_prepare::graphics(device_.Get(), desc, out);
    };
    if (!compute(g_rt_camera_down_cs, sizeof(g_rt_camera_down_cs), camera_down_pso_) ||
        !compute(g_rt_camera_up_cs, sizeof(g_rt_camera_up_cs), camera_up_pso_) ||
        !compute(g_rt_camera_shafts_cs, sizeof(g_rt_camera_shafts_cs), camera_shafts_pso_) ||
        !graphics(g_rt_camera_ps, sizeof(g_rt_camera_ps), display_format, camera_pso_) ||
        !graphics(g_rt_camera_widen_ps, sizeof(g_rt_camera_widen_ps), scene_format, camera_widen_pso_)) {
        std::cerr << "[rt] camera pipelines failed\n";
        return false;
    }
    std::cerr << "[rt] camera bloom=" << camera_bloom_ << " shafts=" << camera_shafts_ << " tone="
              << (camera_tone_ == 1u ? "aces" : camera_tone_ == 2u ? "clip" : "neutral") << '\n';
    return true;
}

bool SceneRayTracer::ensure_camera_pyramid(std::uint32_t width, std::uint32_t height) {
    Level levels[kCameraLevels];
    const std::uint32_t count = camera_levels(width, height, levels);
    const std::uint64_t texels = count != 0u ? levels[count - 1u].at + levels[count - 1u].width * levels[count - 1u].height
                                             : 1u;
    if (texels <= camera_texels_ && camera_pyramid_[0] && camera_pyramid_[1]) {
        return true;
    }
    for (auto& buffer : camera_pyramid_) retire(buffer);
    camera_texels_ = 0u;
    for (unsigned i = 0; i < 2u; ++i) {
        if (!create_buffer(texels * 8u, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                           D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, camera_pyramid_[i],
                           i == 0u ? L"rt camera down" : L"rt camera up")) {
            for (auto& buffer : camera_pyramid_) buffer.Reset();
            return false;
        }
        camera_pyramid_states_[i] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }
    camera_texels_ = texels;
    return true;
}

void SceneRayTracer::bind_camera(ID3D12GraphicsCommandList* list, bool graphics_root,
                                 D3D12_GPU_DESCRIPTOR_HANDLE picture_table) {
    const D3D12_GPU_VIRTUAL_ADDRESS down = camera_pyramid_[0]->GetGPUVirtualAddress();
    const D3D12_GPU_VIRTUAL_ADDRESS up = camera_pyramid_[1]->GetGPUVirtualAddress();
    // A sub-frame's camera rows (read only by a sub-frame's shafts; a stand-in otherwise, never read).
    const D3D12_GPU_VIRTUAL_ADDRESS rows = reproject_ ? reproject_->GetGPUVirtualAddress() : up;
    if (graphics_root) {
        list->SetGraphicsRootSignature(camera_root_.Get());
        list->SetGraphicsRootDescriptorTable(1u, picture_table);
        list->SetGraphicsRootUnorderedAccessView(2u, down);
        list->SetGraphicsRootUnorderedAccessView(3u, down);
        list->SetGraphicsRootShaderResourceView(4u, up);
        list->SetGraphicsRootShaderResourceView(5u, rows);
    } else {
        list->SetComputeRootSignature(camera_root_.Get());
        list->SetComputeRootDescriptorTable(1u, picture_table);
        list->SetComputeRootUnorderedAccessView(2u, down);
        list->SetComputeRootUnorderedAccessView(3u, up);
        list->SetComputeRootShaderResourceView(4u, up);
        list->SetComputeRootShaderResourceView(5u, rows);
    }
}

bool SceneRayTracer::camera_widen(ID3D12GraphicsCommandList* list, D3D12_GPU_DESCRIPTOR_HANDLE picture_table,
                                  D3D12_CPU_DESCRIPTOR_HANDLE target, std::uint32_t width, std::uint32_t height) {
    if (!camera_ready() || list == nullptr || picture_table.ptr == 0u || width == 0u || height == 0u ||
        !ensure_camera_pyramid(width, height)) {
        return false;
    }
    RtCameraConstants c{};
    c.size[0] = width;
    c.size[1] = height;
    list->OMSetRenderTargets(1u, &target, FALSE, nullptr);
    const D3D12_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f};
    const D3D12_RECT scissor{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    list->RSSetViewports(1u, &viewport);
    list->RSSetScissorRects(1u, &scissor);
    bind_camera(list, true, picture_table);
    list->SetPipelineState(camera_widen_pso_.Get());
    list->SetGraphicsRoot32BitConstants(0u, sizeof(c) / 4u, &c, 0u);
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list->DrawInstanced(3u, 1u, 0u, 0u);
    return true;
}

bool SceneRayTracer::camera_finish(ID3D12GraphicsCommandList* list, D3D12_GPU_DESCRIPTOR_HANDLE picture_table,
                                   D3D12_CPU_DESCRIPTOR_HANDLE target, std::uint32_t width, std::uint32_t height,
                                   int slot_index, bool subframe) {
    if (!camera_ready() || list == nullptr || picture_table.ptr == 0u || width == 0u || height == 0u ||
        !ensure_camera_pyramid(width, height)) {
        return false;
    }
    const int timed = begin_pass(list);
    RtCameraConstants c{};
    c.size[0] = width;
    c.size[1] = height;
    // The picture's own frame: its slot (a sub-frame's is a frame the tracer has since moved past; its current one
    // may belong to the next frame, with another camera, or be none while that frame's scene is open).
    const LightSlot* const slot = slot_index >= 0 && slot_index < static_cast<int>(kLightSlots)
                                      ? &light_slots_[static_cast<unsigned>(slot_index)]
                                      : nullptr;
    const bool physical = lighting_active() && slot != nullptr && slot->physical;
    c.flags = physical ? kCameraPhysical | (camera_tone_ == 1u   ? kCameraAces
                                            : camera_tone_ == 2u ? kCameraClip
                                            : camera_tone_ == 3u ? kCameraNeutral
                                                                 : 0u)
                       : 0u;
    Level levels[kCameraLevels];
    const std::uint32_t count = camera_levels(width, height, levels);
    // The player's choices (LENS BLOOM, SUN RAYS); the shafts use the pyramid's buffers with or without the bloom.
    const RenderConfig config = get_render_config();
    const float bloom = rt_effect_on(config, RtEffect::LensBloom) ? camera_bloom_ : 0.0f;
    // The sun's shafts start from the disc the sky shows in front of the camera (sun_visible, on the picture or off
    // it), not from the light's direction, which may be lifted over the player's horizon.
    const bool shafts = physical && count != 0u && rt_effect_on(config, RtEffect::SunRays) && slot != nullptr &&
                        slot->sun_shown &&
                        camera_shafts_ > 0.0f && slot->sun_visible[2] < -0.05f && slot->width == width &&
                        slot->height == height;
    if (physical && count != 0u && (bloom > 0.0f || shafts)) {
        for (unsigned i = 0; i < 2u; ++i) {
            to_state(list, camera_pyramid_[i].Get(), camera_pyramid_states_[i], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
        bind_camera(list, false, picture_table);
        D3D12_RESOURCE_BARRIER all_uav{};
        all_uav.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        const auto run = [&](ID3D12PipelineState* pso, const Level& level, const Level* from) {
            c.level[0] = level.width;
            c.level[1] = level.height;
            c.level_at = level.at;
            c.from[0] = from != nullptr ? from->width : width;
            c.from[1] = from != nullptr ? from->height : height;
            c.from_at = from != nullptr ? from->at : kNoLevel;
            list->SetComputeRoot32BitConstants(0u, sizeof(c) / 4u, &c, 0u);
            list->SetPipelineState(pso);
            list->Dispatch((level.width + 7u) / 8u, (level.height + 7u) / 8u, 1u);
            list->ResourceBarrier(1u, &all_uav);
        };
        // Down from the picture; up from the coarsest level (which starts the sum: no coarser one).
        if (bloom > 0.0f) {
            for (std::uint32_t k = 0; k < count; ++k) {
                run(camera_down_pso_.Get(), levels[k], k == 0u ? nullptr : &levels[k - 1u]);
            }
            for (std::uint32_t k = count; k-- > 0u;) {
                run(camera_up_pso_.Get(), levels[k], k + 1u == count ? nullptr : &levels[k + 1u]);
            }
            c.levels = count;
            c.bloom = bloom;
        }
        c.from[0] = levels[0].width;
        c.from[1] = levels[0].height;
        // The sun's shafts go into the downward pyramid's first texels (free once the bloom is summed).
        if (shafts) {
            const std::array<float, 4>& pr = slot->view.projection;
            const std::array<float, 4>& vp = slot->view.viewport;
            const std::array<float, 3>& sun = slot->sun_visible;
            const float w = -sun[2];
            const float ndc_x = (pr[0] * sun[0] + pr[1] * sun[2]) / w;
            const float ndc_y = (pr[2] * sun[1] + pr[3] * sun[2]) / w;
            const float stage = stage_sun_ != nullptr && stage_sun_->shafts >= 0.0f ? stage_sun_->shafts : 1.0f;
            c.sun[0] = vp[0] + (ndc_x + 1.0f) * 0.5f * vp[2];
            c.sun[1] = vp[1] + (1.0f - ndc_y) * 0.5f * vp[3];
            // GALAXY_RT_LOG_SUN=1: logs the shafts' sun (eye space) and where it falls on the picture every 120 frames.
            static const bool log_sun = env_value("GALAXY_RT_LOG_SUN", 0.0f) != 0.0f;
            static std::uint32_t logged = 0u;
            if (log_sun && (logged++ % 120u) == 0u) {
                std::fprintf(stderr,
                             "[rt-sun] eye=%.4f,%.4f,%.4f pixel=%.1f,%.1f picture=%ux%u "
                             "viewport=%.0f,%.0f,%.0f,%.0f\n",
                             sun[0], sun[1], sun[2], c.sun[0], c.sun[1], width, height, vp[0], vp[1],
                             vp[2], vp[3]);
            }
            c.sun[2] = camera_shafts_ * stage;
            c.sun[3] = static_cast<float>(height);
            // A sub-frame shows its own camera: the shader turns the sun into it (write_reprojection's rows).
            c.subframe = subframe && reproject_ ? static_cast<std::uint32_t>(slot_index) + 1u : 0u;
            c.quarter[0] = (width + 3u) / 4u;
            c.quarter[1] = (height + 3u) / 4u;
            c.far_depth = slot->view.min_depth;
            c.sky_distance = sky_distance_;
            c.depth_params[0] = slot->view.z_scale;
            c.depth_params[1] = slot->view.z_offset;
            c.depth_params[2] = slot->view.min_depth;
            c.depth_params[3] = slot->view.max_depth;
            // Aerosol (Mie) forward scattering, g 0.76 (Preetham et al. 1999's haze; Nishita et al. 1993).
            for (unsigned i = 0u; i < 3u; ++i) {
                c.sun_dir[i] = sun[i];
            }
            c.sun_dir[3] = 0.76f;
            std::copy(pr.begin(), pr.end(), c.projection);
            std::copy(vp.begin(), vp.end(), c.viewport);
            list->SetComputeRoot32BitConstants(0u, sizeof(c) / 4u, &c, 0u);
            list->SetPipelineState(camera_shafts_pso_.Get());
            list->Dispatch((c.quarter[0] + 7u) / 8u, (c.quarter[1] + 7u) / 8u, 1u);
            list->ResourceBarrier(1u, &all_uav);
        }
        to_state(list, camera_pyramid_[1].Get(), camera_pyramid_states_[1], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }
    list->OMSetRenderTargets(1u, &target, FALSE, nullptr);
    const D3D12_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f};
    const D3D12_RECT scissor{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    list->RSSetViewports(1u, &viewport);
    list->RSSetScissorRects(1u, &scissor);
    bind_camera(list, true, picture_table);
    list->SetPipelineState(camera_pso_.Get());
    list->SetGraphicsRoot32BitConstants(0u, sizeof(c) / 4u, &c, 0u);
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list->DrawInstanced(3u, 1u, 0u, 0u);
    end_pass(list, timed, kPassCamera);
    return true;
}

}  // namespace galaxy::gx
