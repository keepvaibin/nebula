// SPDX-License-Identifier: GPL-3.0-only
// AmbientOcclusion: GTAO from the scene target depth, multiplied into the scene colour (ambient_occlusion.h).
#include "galaxy/render/ambient_occlusion.h"
#include "barriers_d3d12.h"
#include "galaxy/render/ambient_occlusion_shape.h"

#include "galaxy/render/renderer_d3d12.h"

#include <cstring>
#include <algorithm>
#include <bit>
#include <iostream>

#include "ao_apply_ps.h"
#include "ao_apply_vs.h"
#include "ao_denoise_cs.h"
#include "ao_distance_cs.h"
#include "ao_occlusion_cs.h"
#include "ao_ssao_cs.h"

namespace galaxy::render {
namespace {

// AoConstants (ambient_occlusion.hlsl).
using ao::Constants;
constexpr DXGI_FORMAT kOcclusionFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
constexpr DXGI_FORMAT kDistanceFormat = DXGI_FORMAT_R32_FLOAT;
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
    D3D12_ROOT_PARAMETER params[5]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].Constants.Num32BitValues = sizeof(Constants) / 4u;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    for (unsigned i = 1; i < 3u; ++i) {
        params[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[i].DescriptorTable.NumDescriptorRanges = 1;
        params[i].DescriptorTable.pDescriptorRanges = &ranges[i - 1u];
        params[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
    for (unsigned i = 3; i < 5u; ++i) {
        params[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[i].Descriptor.ShaderRegister = i;
        params[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
    D3D12_ROOT_SIGNATURE_DESC rsd{};
    rsd.NumParameters = 5;
    rsd.pParameters = params;
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
    cd.CS = code(g_ao_ssao_cs, sizeof(g_ao_ssao_cs));
    if (FAILED(device_->CreateComputePipelineState(&cd, IID_PPV_ARGS(&ssao_)))) {
        std::cerr << "[ambient-occlusion] SSAO pipeline failed\n";
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
    ssao_.Reset();
    denoise_.Reset();
    for (auto& pipeline : apply_) pipeline.Reset();
    root_signature_.Reset();
    distance_ = {};
    raw_ = {};
    denoised_ = {};
    retired_.clear();
    device_ = nullptr;
}

// The multiply: the multiplier its shader makes from the depth and the denoised occlusion times the scene target's
// colour (RGB; the game's alpha stays), no depth target.
ID3D12PipelineState* AmbientOcclusion::apply_pipeline(unsigned samples, bool hdr) {
    const unsigned index = std::min(static_cast<unsigned>(std::countr_zero(std::max(samples, 1u))), 3u) + (hdr ? 4u : 0u);
    if (apply_[index]) {
        return apply_[index].Get();
    }
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
    pd.pRootSignature = root_signature_.Get();
    pd.VS = code(g_ao_apply_vs, sizeof(g_ao_apply_vs));
    pd.PS = code(g_ao_apply_ps, sizeof(g_ao_apply_ps));
    D3D12_RENDER_TARGET_BLEND_DESC& blend = pd.BlendState.RenderTarget[0];
    blend.BlendEnable = TRUE;
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
    pd.RTVFormats[0] = hdr ? kSceneHdrColorFormat : kSceneColorFormat;
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
        target.used_frame = frame_index;
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
    if (FAILED(create_texture(device_, &heap, D3D12_HEAP_FLAG_NONE, &rd,
                                                gpu::Use::ComputeRead, nullptr,
                                                IID_PPV_ARGS(&target.resource)))) {
        return false;
    }
    target.resource->SetName(L"ambient occlusion");
    target.width = width;
    target.height = height;
    target.use = gpu::Use::ComputeRead;
    target.used_frame = frame_index;
    return true;
}

void AmbientOcclusion::trim_idle(std::uint64_t frame_index, unsigned frames_in_flight) {
    constexpr std::uint64_t kIdleFrames = 600u;
    std::erase_if(retired_, [&](const Retired& r) { return frame_index > r.release_after_frame; });
    for (Target* target : {&distance_, &raw_, &denoised_}) {
        if (target->resource && frame_index > target->used_frame + kIdleFrames) {
            retired_.push_back({std::move(target->resource), frame_index + frames_in_flight});
            *target = {};
        }
    }
}

void AmbientOcclusion::transition(ID3D12GraphicsCommandList* list, Target& target, gpu::Use use) {
    if (target.use == use) {
        return;
    }
    BarriersD3D12 uses{list};
    uses.texture(target.resource.Get(), target.use, use);
    uses.issue();
    target.use = use;
}

bool AmbientOcclusion::record(ID3D12GraphicsCommandList* list, DescriptorRing& ring, const Inputs& in,
                              std::uint64_t frame_index, unsigned frames_in_flight) {
    if (!ready_ || in.depth == nullptr || in.width == 0u || in.height == 0u) {
        return false;
    }
    ID3D12PipelineState* const apply = apply_pipeline(in.samples, in.hdr);
    const unsigned divisor = ao::occlusion_divisor(in.height);
    const unsigned width = (in.width + divisor - 1u) / divisor;
    const unsigned height = (in.height + divisor - 1u) / divisor;
    Constants c{};
    if (apply == nullptr || in.objects == nullptr || in.ring == nullptr || !ao::make_constants(in.view, width, height, divisor, in.ssao, c) ||
        !ensure_target(distance_, kDistanceFormat, width, height, frame_index, frames_in_flight) ||
        !ensure_target(raw_, kOcclusionFormat, width, height, frame_index, frames_in_flight) ||
        !ensure_target(denoised_, kOcclusionFormat, width, height, frame_index, frames_in_flight)) {
        return false;
    }

    // SRV tables (t0 the scene target's depth, t1 an occlusion target, t2 the distances) and the targets' UAVs.
    ID3D12Device* const device = device_;
    D3D12_SHADER_RESOURCE_VIEW_DESC depth_view{};
    depth_view.Format = kSceneDepthSrvFormat;
    depth_view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    depth_view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    depth_view.Texture2D.MipLevels = 1;
    D3D12_SHADER_RESOURCE_VIEW_DESC occlusion_view = depth_view;
    occlusion_view.Format = kOcclusionFormat;
    D3D12_SHADER_RESOURCE_VIEW_DESC distance_view = depth_view;
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
    // 0-2: reads the denoised occlusion (the multiply too); 3-5: the raw.
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
    ao::bin_objects(*in.objects, c, object_values_, object_tiles_, object_scratch_);
    const UploadRing::Allocation objects = in.ring->allocate(object_values_.size() * sizeof(float), 16u);
    const UploadRing::Allocation tiles = in.ring->allocate(object_tiles_.size() * sizeof(std::uint32_t), 16u);
    std::memcpy(objects.cpu, object_values_.data(), object_values_.size() * sizeof(float));
    std::memcpy(tiles.cpu, object_tiles_.data(), object_tiles_.size() * sizeof(std::uint32_t));
    list->SetComputeRootShaderResourceView(3, objects.gpu);
    list->SetComputeRootShaderResourceView(4, tiles.gpu);
    const UINT groups_x = (width + 7u) / 8u;
    const UINT groups_y = (height + 7u) / 8u;
    transition(list, distance_, gpu::Use::ComputeWrite);
    list->SetPipelineState(distance_pass_.Get());
    ring.bind(list);
    list->SetComputeRootDescriptorTable(1, gpu(0));
    list->SetComputeRootDescriptorTable(2, gpu(6));
    list->Dispatch(groups_x, groups_y, 1);
    transition(list, distance_, gpu::Use::ComputeRead);
    transition(list, raw_, gpu::Use::ComputeWrite);
    list->SetPipelineState(in.ssao ? ssao_.Get() : occlusion_.Get());
    list->SetComputeRootDescriptorTable(2, gpu(7));
    list->Dispatch(groups_x, groups_y, 1);
    transition(list, raw_, gpu::Use::ComputeRead);
    transition(list, denoised_, gpu::Use::ComputeWrite);
    list->SetPipelineState(denoise_.Get());
    list->SetComputeRootDescriptorTable(1, gpu(3));
    list->SetComputeRootDescriptorTable(2, gpu(8));
    list->Dispatch(groups_x, groups_y, 1);
    transition(list, denoised_, gpu::Use::ShaderRead);

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

}  // namespace galaxy::render
