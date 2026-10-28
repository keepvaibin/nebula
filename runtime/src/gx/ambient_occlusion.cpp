// SPDX-License-Identifier: GPL-3.0-only
// AmbientOcclusion: GTAO from the EFB depth, multiplied into the scene colour (ambient_occlusion.h).
#include "galaxy/gx/ambient_occlusion.h"

#include "galaxy/gx/renderer_d3d12.h"

#include <algorithm>
#include <bit>
#include <iostream>

#include "ao_apply_ps.h"
#include "ao_apply_vs.h"
#include "ao_denoise_cs.h"
#include "ao_distance_cs.h"
#include "ao_occlusion_cs.h"

namespace galaxy::gx {
namespace {

// AoConstants (ambient_occlusion.hlsl).
struct Constants {
    float ray[4];
    float depth[4];
    float viewport[4];
    float params[4];
    std::uint32_t size[4];
};
static_assert(sizeof(Constants) == 80u);

constexpr DXGI_FORMAT kOcclusionFormat = DXGI_FORMAT_R16G16_FLOAT;
constexpr DXGI_FORMAT kDistanceFormat = DXGI_FORMAT_R32_FLOAT;

// The occlusion's reach and look: XeGTAO's defaults (radius 0.5 m times its 1.457 multiplier, final power 2.2) in the
// game's units (centimetres: Mario stands about 160 tall).
constexpr float kRadius = 73.0f;
constexpr float kPower = 2.2f;
constexpr float kStrength = 1.0f;

D3D12_SHADER_BYTECODE code(const void* bytes, std::size_t size) {
    return {bytes, size};
}

}  // namespace

bool AmbientOcclusion::initialize(ID3D12Device* device) {
    shutdown();
    device_ = device;
    if (device_ == nullptr) {
        return false;
    }
    D3D12_DESCRIPTOR_RANGE ranges[2]{};
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[0].NumDescriptors = 3;
    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    ranges[1].NumDescriptors = 1;
    D3D12_ROOT_PARAMETER params[3]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].Constants.Num32BitValues = sizeof(Constants) / 4u;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    for (unsigned i = 1; i < 3u; ++i) {
        params[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[i].DescriptorTable.NumDescriptorRanges = 1;
        params[i].DescriptorTable.pDescriptorRanges = &ranges[i - 1u];
        params[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
    // The apply's gathers of the occlusion's 2x2 blocks (edges clamped).
    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC rsd{};
    rsd.NumParameters = 3;
    rsd.pParameters = params;
    rsd.NumStaticSamplers = 1;
    rsd.pStaticSamplers = &sampler;
    Microsoft::WRL::ComPtr<ID3DBlob> blob;
    Microsoft::WRL::ComPtr<ID3DBlob> errors;
    if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors)) ||
        FAILED(device_->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                            IID_PPV_ARGS(&root_signature_)))) {
        std::cerr << "[ambient-occlusion] root signature failed\n";
        return false;
    }
    D3D12_COMPUTE_PIPELINE_STATE_DESC cd{};
    cd.pRootSignature = root_signature_.Get();
    cd.CS = code(g_ao_distance_cs, sizeof(g_ao_distance_cs));
    if (FAILED(device_->CreateComputePipelineState(&cd, IID_PPV_ARGS(&distance_pass_)))) {
        std::cerr << "[ambient-occlusion] distance pipeline failed\n";
        return false;
    }
    cd.CS = code(g_ao_occlusion_cs, sizeof(g_ao_occlusion_cs));
    if (FAILED(device_->CreateComputePipelineState(&cd, IID_PPV_ARGS(&occlusion_)))) {
        std::cerr << "[ambient-occlusion] occlusion pipeline failed\n";
        return false;
    }
    cd.CS = code(g_ao_denoise_cs, sizeof(g_ao_denoise_cs));
    if (FAILED(device_->CreateComputePipelineState(&cd, IID_PPV_ARGS(&denoise_)))) {
        std::cerr << "[ambient-occlusion] denoise pipeline failed\n";
        return false;
    }
    ready_ = true;
    return true;
}

void AmbientOcclusion::shutdown() noexcept {
    ready_ = false;
    distance_pass_.Reset();
    occlusion_.Reset();
    denoise_.Reset();
    for (auto& pipeline : apply_) pipeline.Reset();
    root_signature_.Reset();
    distance_ = {};
    raw_ = {};
    denoised_ = {};
    retired_.clear();
    device_ = nullptr;
}

// The multiply: the shader's output times the EFB's colour (RGB; the game's alpha stays), no depth. `view`: the
// shader's output replaces the colour (GALAXY_AO_VIEW).
ID3D12PipelineState* AmbientOcclusion::apply_pipeline(unsigned samples, bool hdr, bool view) {
    const unsigned index = std::min(static_cast<unsigned>(std::countr_zero(std::max(samples, 1u))), 3u) +
                           (hdr ? 4u : 0u) + (view ? 8u : 0u);
    if (apply_[index]) {
        return apply_[index].Get();
    }
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
    pd.pRootSignature = root_signature_.Get();
    pd.VS = code(g_ao_apply_vs, sizeof(g_ao_apply_vs));
    pd.PS = code(g_ao_apply_ps, sizeof(g_ao_apply_ps));
    D3D12_RENDER_TARGET_BLEND_DESC& blend = pd.BlendState.RenderTarget[0];
    blend.BlendEnable = view ? FALSE : TRUE;
    blend.SrcBlend = D3D12_BLEND_ZERO;
    blend.DestBlend = D3D12_BLEND_SRC_COLOR;
    blend.BlendOp = D3D12_BLEND_OP_ADD;
    blend.SrcBlendAlpha = D3D12_BLEND_ZERO;
    blend.DestBlendAlpha = D3D12_BLEND_ONE;
    blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blend.LogicOp = D3D12_LOGIC_OP_NOOP;
    blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_RED | D3D12_COLOR_WRITE_ENABLE_GREEN |
                                  D3D12_COLOR_WRITE_ENABLE_BLUE;
    pd.SampleMask = UINT_MAX;
    pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pd.RasterizerState.DepthClipEnable = TRUE;
    pd.DepthStencilState.DepthEnable = FALSE;
    pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pd.NumRenderTargets = 1;
    pd.RTVFormats[0] = hdr ? kEfbHdrColorFormat : kEfbColorFormat;
    pd.SampleDesc.Count = 1u << (index & 3u);
    if (FAILED(device_->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&apply_[index])))) {
        std::cerr << "[ambient-occlusion] multiply pipeline failed for " << (1u << (index & 3u)) << " samples\n";
        return nullptr;
    }
    return apply_[index].Get();
}

bool AmbientOcclusion::ensure_target(Target& target, DXGI_FORMAT format, unsigned width, unsigned height,
                                     std::uint64_t frame_index, unsigned frames_in_flight) {
    retired_.erase(std::remove_if(retired_.begin(), retired_.end(),
                                  [&](const Retired& r) { return frame_index > r.release_after_frame; }),
                   retired_.end());
    if (target.resource && target.width == width && target.height == height) {
        return true;
    }
    if (target.resource) {
        retired_.push_back({std::move(target.resource), frame_index + frames_in_flight});
    }
    target = {};
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = width;
    rd.Height = height;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.Format = format;
    rd.SampleDesc.Count = 1;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &rd,
                                                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr,
                                                IID_PPV_ARGS(&target.resource)))) {
        return false;
    }
    target.resource->SetName(L"ambient occlusion");
    target.width = width;
    target.height = height;
    target.state = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    return true;
}

void AmbientOcclusion::transition(ID3D12GraphicsCommandList* list, Target& target, D3D12_RESOURCE_STATES state) {
    if (target.state == state) {
        return;
    }
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = target.resource.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = target.state;
    barrier.Transition.StateAfter = state;
    list->ResourceBarrier(1, &barrier);
    target.state = state;
}

bool AmbientOcclusion::record(ID3D12GraphicsCommandList* list, DescriptorRing& ring, const Inputs& in,
                              std::uint64_t frame_index, unsigned frames_in_flight) {
    if (!ready_ || in.depth == nullptr || in.width == 0u || in.height == 0u || in.view.viewport[2] <= 0.0f ||
        in.view.viewport[3] <= 0.0f) {
        return false;
    }
    // GALAXY_AO_VIEW=1: the scene shows the occlusion alone (white: open), to judge it by itself.
    static const bool view = [] {
        char value[8]{};
        std::size_t length = 0;
        return getenv_s(&length, value, sizeof(value), "GALAXY_AO_VIEW") == 0 && length > 1 && value[0] == '1';
    }();
    ID3D12PipelineState* const apply = apply_pipeline(in.samples, in.hdr, view);
    // The game's resolution: one occlusion pixel per internal-scale x internal-scale EFB pixels.
    const unsigned divisor = std::max(in.efb_scale, 1u);
    const unsigned width = (in.width + divisor - 1u) / divisor;
    const unsigned height = (in.height + divisor - 1u) / divisor;
    if (apply == nullptr ||
        !ensure_target(distance_, kDistanceFormat, width, height, frame_index, frames_in_flight) ||
        !ensure_target(raw_, kOcclusionFormat, width, height, frame_index, frames_in_flight) ||
        !ensure_target(denoised_, kOcclusionFormat, width, height, frame_index, frames_in_flight)) {
        return false;
    }

    Constants c{};
    // An EFB pixel centre's eye ray (z = -1): NDC from the viewport, then through the projection (x' = A x + B z).
    const float a = in.view.projection[0];
    const float b = in.view.projection[1];
    const float cy = in.view.projection[2];
    const float d = in.view.projection[3];
    const float vx = in.view.viewport[0];
    const float vy = in.view.viewport[1];
    const float vw = in.view.viewport[2];
    const float vh = in.view.viewport[3];
    if (a == 0.0f || cy == 0.0f) {
        return false;
    }
    c.ray[0] = 2.0f / (vw * a);
    c.ray[1] = (-2.0f * vx / vw - 1.0f + b) / a;
    c.ray[2] = -2.0f / (vh * cy);
    c.ray[3] = (1.0f + 2.0f * vy / vh + d) / cy;
    for (unsigned i = 0; i < 4u; ++i) {
        c.viewport[i] = in.view.viewport[i];
    }
    c.depth[0] = in.view.z_scale;
    c.depth[1] = in.view.z_offset;
    c.depth[2] = in.view.min_depth;
    c.depth[3] = in.view.max_depth;
    c.params[0] = kRadius;
    c.params[1] = kPower;
    c.params[2] = kStrength;
    c.params[3] = std::max(8.0f, 0.12f * in.view.viewport[3] / static_cast<float>(divisor));
    c.size[0] = width;
    c.size[1] = height;
    c.size[2] = divisor;
    c.size[3] = view ? 1u : 0u;

    // Two SRV tables (t0 the EFB depth, t1 an occlusion target, t2 the distances: {denoised} for the distance,
    // occlusion and multiply passes, {raw} for the denoise), then the three targets' UAVs.
    ID3D12Device* const device = device_;
    D3D12_SHADER_RESOURCE_VIEW_DESC depth_view{};
    depth_view.Format = kEfbDepthSrvFormat;
    depth_view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    depth_view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    depth_view.Texture2D.MipLevels = 1;
    D3D12_SHADER_RESOURCE_VIEW_DESC occlusion_view{};
    occlusion_view.Format = kOcclusionFormat;
    occlusion_view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    occlusion_view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    occlusion_view.Texture2D.MipLevels = 1;
    D3D12_SHADER_RESOURCE_VIEW_DESC distance_view = occlusion_view;
    distance_view.Format = kDistanceFormat;
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
    uav.Format = kOcclusionFormat;
    uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    D3D12_UNORDERED_ACCESS_VIEW_DESC distance_uav = uav;
    distance_uav.Format = kDistanceFormat;
    const UINT increment = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    const DescriptorRing::Table table = ring.allocate(9u);
    const auto cpu = [&](unsigned i) {
        D3D12_CPU_DESCRIPTOR_HANDLE h = table.cpu;
        h.ptr += static_cast<SIZE_T>(i) * increment;
        return h;
    };
    const auto gpu = [&](unsigned i) {
        D3D12_GPU_DESCRIPTOR_HANDLE h = table.gpu;
        h.ptr += static_cast<UINT64>(i) * increment;
        return h;
    };
    device->CreateShaderResourceView(in.depth, &depth_view, cpu(0));
    device->CreateShaderResourceView(denoised_.resource.Get(), &occlusion_view, cpu(1));
    device->CreateShaderResourceView(distance_.resource.Get(), &distance_view, cpu(2));
    device->CreateShaderResourceView(in.depth, &depth_view, cpu(3));
    device->CreateShaderResourceView(raw_.resource.Get(), &occlusion_view, cpu(4));
    device->CreateShaderResourceView(distance_.resource.Get(), &distance_view, cpu(5));
    device->CreateUnorderedAccessView(distance_.resource.Get(), nullptr, &distance_uav, cpu(6));
    device->CreateUnorderedAccessView(raw_.resource.Get(), nullptr, &uav, cpu(7));
    device->CreateUnorderedAccessView(denoised_.resource.Get(), nullptr, &uav, cpu(8));

    list->SetComputeRootSignature(root_signature_.Get());
    list->SetComputeRoot32BitConstants(0, sizeof(Constants) / 4u, &c, 0);
    const UINT groups_x = (width + 7u) / 8u;
    const UINT groups_y = (height + 7u) / 8u;
    transition(list, distance_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    list->SetPipelineState(distance_pass_.Get());
    list->SetComputeRootDescriptorTable(1, gpu(0));
    list->SetComputeRootDescriptorTable(2, gpu(6));
    list->Dispatch(groups_x, groups_y, 1);
    transition(list, distance_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    transition(list, raw_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    list->SetPipelineState(occlusion_.Get());
    list->SetComputeRootDescriptorTable(2, gpu(7));
    list->Dispatch(groups_x, groups_y, 1);
    transition(list, raw_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    transition(list, denoised_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    list->SetPipelineState(denoise_.Get());
    list->SetComputeRootDescriptorTable(1, gpu(3));
    list->SetComputeRootDescriptorTable(2, gpu(8));
    list->Dispatch(groups_x, groups_y, 1);
    transition(list, denoised_, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    list->SetGraphicsRootSignature(root_signature_.Get());
    list->SetGraphicsRoot32BitConstants(0, sizeof(Constants) / 4u, &c, 0);
    list->SetGraphicsRootDescriptorTable(1, gpu(0));
    list->SetPipelineState(apply);
    list->OMSetRenderTargets(1, &in.color_rtv, FALSE, nullptr);
    const D3D12_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(in.width), static_cast<float>(in.height), 0.0f, 1.0f};
    const D3D12_RECT scissor{0, 0, static_cast<LONG>(in.width), static_cast<LONG>(in.height)};
    list->RSSetViewports(1, &viewport);
    list->RSSetScissorRects(1, &scissor);
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list->DrawInstanced(3, 1, 0, 0);
    return true;
}

}  // namespace galaxy::gx
