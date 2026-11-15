// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/render/scene_camera.h"
#include "galaxy/render/scene_camera_shape.h"

#include "galaxy/render/renderer_d3d12.h"
#include "barriers_d3d12.h"

#include <algorithm>
#include <bit>
#include <iostream>

#include "camera_meter_cs.h"
#include "camera_vs.h"
#include "camera_ps.h"
#include "camera_samples_ps.h"
#include "camera_resolve_ps.h"
#include "camera_widen_ps.h"

namespace galaxy::render {

namespace {

constexpr DXGI_FORMAT kExposureFormat = DXGI_FORMAT_R32_FLOAT;

D3D12_SHADER_BYTECODE code(const void* bytes, std::size_t size) {
    return {bytes, size};
}

// Read by the meter (compute) and the composite (pixels).
constexpr gpu::Use kRead = gpu::Use::ShaderRead;

}  // namespace

bool SceneCamera::initialize(ID3D12Device* device) {
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
    params[0].Constants.Num32BitValues = sizeof(camera::Constants) / 4u;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    for (unsigned i = 1; i < 3u; ++i) {
        params[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[i].DescriptorTable.NumDescriptorRanges = 1;
        params[i].DescriptorTable.pDescriptorRanges = &ranges[i - 1u];
        params[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
    D3D12_ROOT_SIGNATURE_DESC rsd{};
    rsd.NumParameters = 3;
    rsd.pParameters = params;
    Microsoft::WRL::ComPtr<ID3DBlob> blob;
    Microsoft::WRL::ComPtr<ID3DBlob> errors;
    if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors)) ||
        FAILED(device_->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                            IID_PPV_ARGS(&root_signature_)))) {
        std::cerr << "[scene-camera] root signature failed\n";
        return false;
    }
    D3D12_COMPUTE_PIPELINE_STATE_DESC cd{};
    cd.pRootSignature = root_signature_.Get();
    cd.CS = code(g_camera_meter_cs, sizeof(g_camera_meter_cs));
    if (FAILED(device_->CreateComputePipelineState(&cd, IID_PPV_ARGS(&meter_)))) {
        std::cerr << "[scene-camera] meter pipeline failed\n";
        return false;
    }
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = 2;  // linear exposure and its precomputed display-space gain
    rd.Height = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.Format = kExposureFormat;
    rd.SampleDesc.Count = 1;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    if (FAILED(create_texture(device_, &heap, D3D12_HEAP_FLAG_NONE, &rd,
                                                gpu::Use::ShaderRead,
                                                nullptr, IID_PPV_ARGS(&exposure_)))) {
        std::cerr << "[scene-camera] exposure target failed\n";
        return false;
    }
    exposure_->SetName(L"scene camera exposure");
    ready_ = true;
    return true;
}

void SceneCamera::shutdown() {
    exposure_ready_ = false;
    ready_ = false;
    root_signature_.Reset();
    meter_.Reset();
    camera_ = {};
    camera_resolve_.Reset();
    widen_ = {};
    exposure_.Reset();
    device_ = nullptr;
}

ID3D12PipelineState* SceneCamera::camera_pipeline(unsigned samples, bool resolve) {
    const unsigned index = std::min(static_cast<unsigned>(std::countr_zero(std::max(samples, 1u))), 3u);
    auto& output = resolve ? camera_resolve_ : camera_[index];
    if (output) {
        return output.Get();
    }
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
    pd.pRootSignature = root_signature_.Get();
    pd.VS = code(g_camera_vs, sizeof(g_camera_vs));
    pd.PS = resolve ? code(g_camera_resolve_ps, sizeof(g_camera_resolve_ps)) :
        index != 0u ? code(g_camera_samples_ps, sizeof(g_camera_samples_ps)) : code(g_camera_ps, sizeof(g_camera_ps));
    pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pd.SampleMask = UINT_MAX;
    pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pd.RasterizerState.DepthClipEnable = TRUE;
    pd.DepthStencilState.DepthEnable = FALSE;
    pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pd.NumRenderTargets = 1;
    pd.RTVFormats[0] = kSceneColorFormat;
    pd.SampleDesc.Count = resolve ? 1u : 1u << index;
    if (FAILED(device_->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&output)))) {
        std::cerr << "[scene-camera] pipeline failed for " << (1u << index) << " samples\n";
        return nullptr;
    }
    return output.Get();
}

ID3D12PipelineState* SceneCamera::widen_pipeline(unsigned samples) {
    const unsigned index = std::min(static_cast<unsigned>(std::countr_zero(std::max(samples, 1u))), 3u);
    if (widen_[index]) {
        return widen_[index].Get();
    }
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
    pd.pRootSignature = root_signature_.Get();
    pd.VS = code(g_camera_vs, sizeof(g_camera_vs));
    pd.PS = code(g_camera_widen_ps, sizeof(g_camera_widen_ps));
    pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pd.SampleMask = UINT_MAX;
    pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pd.RasterizerState.DepthClipEnable = TRUE;
    pd.DepthStencilState.DepthEnable = FALSE;
    pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pd.NumRenderTargets = 1;
    pd.RTVFormats[0] = kSceneHdrColorFormat;
    pd.SampleDesc.Count = 1u << index;
    if (FAILED(device_->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&widen_[index])))) {
        std::cerr << "[scene-camera] widen pipeline failed for " << (1u << index) << " samples\n";
        return nullptr;
    }
    return widen_[index].Get();
}

bool SceneCamera::record(ID3D12GraphicsCommandList* list, DescriptorRing& ring, const Inputs& in) {
    if (!ready_ || in.wide == nullptr || in.depth == nullptr || in.width == 0u || in.height == 0u) {
        return false;
    }
    const bool samples = in.samples > 1u && in.wide_ms != nullptr;
    ID3D12PipelineState* const pipeline = camera_pipeline(samples ? in.samples : 1u, samples && in.resolve);
    if (pipeline == nullptr) {
        return false;
    }
    camera::Constants constants{};
    constants.width = static_cast<float>(in.width);
    constants.height = static_cast<float>(in.height);
    constants.seconds = exposure_ready_ ? in.seconds : -1.0f;
    constants.finish = static_cast<float>(in.finish);
    exposure_ready_ = true;

    // Two tables: the meter's (the resolved picture, the exposure, its storage view) and the camera's (the picture it
    // writes from, the exposure).
    const DescriptorRing::Table table = ring.allocate(7u);
    const UINT stride = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    const auto cpu = [&](unsigned i) {
        D3D12_CPU_DESCRIPTOR_HANDLE handle = table.cpu;
        handle.ptr += stride * i;
        return handle;
    };
    const auto gpu = [&](unsigned i) {
        D3D12_GPU_DESCRIPTOR_HANDLE handle = table.gpu;
        handle.ptr += stride * i;
        return handle;
    };
    D3D12_SHADER_RESOURCE_VIEW_DESC view{};
    view.Format = kSceneHdrColorFormat;
    view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    view.Texture2D.MipLevels = 1u;
    D3D12_SHADER_RESOURCE_VIEW_DESC exposure_view = view;
    exposure_view.Format = kExposureFormat;
    device_->CreateShaderResourceView(in.wide, &view, cpu(0));
    device_->CreateShaderResourceView(exposure_.Get(), &exposure_view, cpu(1));
    D3D12_SHADER_RESOURCE_VIEW_DESC depth_view = view;
    depth_view.Format = kSceneDepthSrvFormat;
    device_->CreateShaderResourceView(in.depth, &depth_view, cpu(2));
    D3D12_UNORDERED_ACCESS_VIEW_DESC storage{};
    storage.Format = kExposureFormat;
    storage.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    device_->CreateUnorderedAccessView(exposure_.Get(), nullptr, &storage, cpu(3));
    if (samples) {
        D3D12_SHADER_RESOURCE_VIEW_DESC many{};
        many.Format = kSceneHdrColorFormat;
        many.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
        many.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        device_->CreateShaderResourceView(in.wide_ms, &many, cpu(4));
    } else {
        device_->CreateShaderResourceView(in.wide, &view, cpu(4));
    }
    device_->CreateShaderResourceView(exposure_.Get(), &exposure_view, cpu(5));
    device_->CreateShaderResourceView(in.depth, &depth_view, cpu(6));

    // The meter's inputs and its exposure change in one call.
    BarriersD3D12 to_meter{list};
    to_meter.texture(in.wide, gpu::Use::ColorWrite, kRead);
    if (samples) {
        to_meter.texture(in.wide_ms, gpu::Use::ColorWrite, kRead);
    }
    to_meter.texture(exposure_.Get(), kRead, gpu::Use::ComputeWrite);
    to_meter.texture(in.depth, gpu::Use::DepthWrite, kRead);
    to_meter.issue();
    list->SetComputeRootSignature(root_signature_.Get());
    list->SetPipelineState(meter_.Get());
    list->SetComputeRoot32BitConstants(0, sizeof(constants) / 4u, &constants, 0);
    ring.bind(list);
    list->SetComputeRootDescriptorTable(1, gpu(0));
    list->SetComputeRootDescriptorTable(2, gpu(3));
    list->Dispatch(1, 1, 1);
    BarriersD3D12 to_draw{list};
    to_draw.texture(in.depth, kRead, gpu::Use::DepthWrite);
    to_draw.texture(exposure_.Get(), gpu::Use::ComputeWrite, kRead);
    to_draw.issue();

    list->SetGraphicsRootSignature(root_signature_.Get());
    list->SetPipelineState(pipeline);
    list->SetGraphicsRoot32BitConstants(0, sizeof(constants) / 4u, &constants, 0);
    list->SetGraphicsRootDescriptorTable(1, gpu(4));
    list->SetGraphicsRootDescriptorTable(2, gpu(3));
    const D3D12_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(in.width), static_cast<float>(in.height), 0.0f, 1.0f};
    const D3D12_RECT scissor{0, 0, static_cast<LONG>(in.width), static_cast<LONG>(in.height)};
    list->RSSetViewports(1, &viewport);
    list->RSSetScissorRects(1, &scissor);
    list->OMSetRenderTargets(1, &in.rtv, FALSE, nullptr);
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list->DrawInstanced(3, 1, 0, 0);

    BarriersD3D12 back{list};
    back.texture(in.wide, kRead, gpu::Use::ColorWrite);
    if (samples) {
        back.texture(in.wide_ms, kRead, gpu::Use::ColorWrite);
    }
    back.issue();
    return true;
}

bool SceneCamera::widen(ID3D12GraphicsCommandList* list, DescriptorRing& ring, const WidenInputs& in) {
    if (!ready_ || in.picture == nullptr || in.width == 0u || in.height == 0u) {
        return false;
    }
    ID3D12PipelineState* const pipeline = widen_pipeline(in.samples);
    if (pipeline == nullptr) {
        return false;
    }
    const DescriptorRing::Table table = ring.allocate(3u);
    const UINT stride = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = table.cpu;
    D3D12_SHADER_RESOURCE_VIEW_DESC view{};
    view.Format = kSceneColorFormat;
    view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    view.Texture2D.MipLevels = 1u;
    device_->CreateShaderResourceView(in.picture, &view, cpu);
    cpu.ptr += stride;
    D3D12_SHADER_RESOURCE_VIEW_DESC exposure_view = view;
    exposure_view.Format = kExposureFormat;
    device_->CreateShaderResourceView(exposure_.Get(), &exposure_view, cpu);
    cpu.ptr += stride;
    D3D12_UNORDERED_ACCESS_VIEW_DESC storage{};
    storage.Format = kExposureFormat;
    storage.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    device_->CreateUnorderedAccessView(exposure_.Get(), nullptr, &storage, cpu);
    D3D12_GPU_DESCRIPTOR_HANDLE storage_table = table.gpu;
    storage_table.ptr += stride * 2u;

    BarriersD3D12 picture{list};
    picture.texture(in.picture, gpu::Use::ColorWrite, kRead);
    picture.issue();
    list->SetGraphicsRootSignature(root_signature_.Get());
    list->SetPipelineState(pipeline);
    ring.bind(list);
    list->SetGraphicsRootDescriptorTable(1, table.gpu);
    list->SetGraphicsRootDescriptorTable(2, storage_table);
    const D3D12_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(in.width), static_cast<float>(in.height), 0.0f, 1.0f};
    const D3D12_RECT scissor{0, 0, static_cast<LONG>(in.width), static_cast<LONG>(in.height)};
    list->RSSetViewports(1, &viewport);
    list->RSSetScissorRects(1, &scissor);
    list->OMSetRenderTargets(1, &in.rtv, FALSE, nullptr);
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list->DrawInstanced(3, 1, 0, 0);
    picture.undo();
    return true;
}

}  // namespace galaxy::render
