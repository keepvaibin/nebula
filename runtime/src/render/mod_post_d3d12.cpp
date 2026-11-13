// SPDX-License-Identifier: GPL-3.0-only
// draw.post and draw.native on D3D12 (LumaCore plan 2.4 layers 8 and 9; contract draw 15 and 16): a mod's post
// program drawn full screen over the frame (RenderCore::walk_post), and a mod's native function given the device, the
// open command list and the target at its draw point (RenderCore::walk_native).
#include "galaxy/render/renderer_d3d12.h"

#include "barriers_d3d12.h"
#include "galaxy/render/mod_post.h"

#include <nebula_frame_list.h>

#include <algorithm>
#include <iostream>

namespace galaxy::render {

using Microsoft::WRL::ComPtr;

namespace {

void null_view(ID3D12Device* device, DXGI_FORMAT format, D3D12_CPU_DESCRIPTOR_HANDLE slot) {
    D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = format;
    sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sd.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(nullptr, &sd, slot);
}

void texture_view(ID3D12Device* device, ID3D12Resource* texture, DXGI_FORMAT format, D3D12_CPU_DESCRIPTOR_HANDLE slot) {
    D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = format;
    sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sd.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(texture, &sd, slot);
}

}  // namespace

// The layout of mod_post.h: the block (32 words) at b0, the frame, depth and motion at t0 to t2, linear and point clamp
// at s0 and s1.
bool RendererD3D12::ensure_mod_post_root() {
    if (mod_post_root_) {
        return true;
    }
    if (mod_post_root_failed_) {
        return false;
    }
    D3D12_DESCRIPTOR_RANGE range{};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 3;
    D3D12_ROOT_PARAMETER params[2]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].Constants.ShaderRegister = 0;
    params[0].Constants.Num32BitValues = kModPostConstantWords;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges = &range;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_STATIC_SAMPLER_DESC samplers[2]{};
    for (unsigned i = 0; i < 2u; ++i) {
        samplers[i].AddressU = samplers[i].AddressV = samplers[i].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[i].MaxLOD = D3D12_FLOAT32_MAX;
        samplers[i].ShaderRegister = i;
        samplers[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    }
    samplers[0].Filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    samplers[1].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    D3D12_ROOT_SIGNATURE_DESC rsd{};
    rsd.NumParameters = 2;
    rsd.pParameters = params;
    rsd.NumStaticSamplers = 2;
    rsd.pStaticSamplers = samplers;
    ComPtr<ID3DBlob> blob, error;
    if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error)) ||
        FAILED(device_->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&mod_post_root_)))) {
        std::cerr << "[draw.post] root signature failed: post programs draw nothing\n";
        mod_post_root_failed_ = true;
        return false;
    }
    return true;
}

// The program's pipeline for the target's format and samples, made the first time; null when the device refuses it
// (said once a program).
ID3D12PipelineState* RendererD3D12::mod_post_pipeline(const ModPostProgram& program, DXGI_FORMAT format, UINT samples) {
    // Programs freed since the last look leave the cache, their pipelines kept until the frames in flight end.
    const std::uint64_t epoch = mod_post_epoch();
    std::erase_if(mod_post_retired_, [this](const RetiredPipeline& old) { return frame_index_ >= old.free_from; });
    if (epoch != mod_post_epoch_) {
        mod_post_epoch_ = epoch;
        std::erase_if(mod_post_pipelines_, [this](ModPostPipeline& kept) {
            if (mod_post_program(kept.program) != nullptr) {
                return false;
            }
            if (kept.pipeline) {
                mod_post_retired_.push_back({std::move(kept.pipeline), frame_index_ + kFramesInFlight + 1u});
            }
            return true;
        });
    }
    for (const ModPostPipeline& kept : mod_post_pipelines_) {
        if (kept.program == program.id && kept.format == format && kept.samples == samples) {
            return kept.pipeline.Get();
        }
    }
    ModPostPipeline made{program.id, format, samples, nullptr};
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
    pd.pRootSignature = mod_post_root_.Get();
    pd.VS = {program.vertex.data(), program.vertex.size()};
    pd.PS = {program.pixel.data(), program.pixel.size()};
    pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pd.SampleMask = UINT_MAX;
    pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pd.RasterizerState.DepthClipEnable = TRUE;
    pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pd.NumRenderTargets = 1;
    pd.RTVFormats[0] = format;
    pd.SampleDesc.Count = samples;
    const HRESULT result = device_->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&made.pipeline));
    if (FAILED(result)) {
        // Kept empty: the pass draws nothing for it from now on, and the log says why once.
        std::cerr << "[draw.post] program " << program.id << " pipeline failed " << std::hex << static_cast<unsigned long>(result)
                  << std::dec << '\n';
        made.pipeline = nullptr;
    }
    mod_post_pipelines_.push_back(std::move(made));
    return mod_post_pipelines_.back().pipeline.Get();
}

void RendererD3D12::record_mod_post(const ModPostProgram& program, ModPostConstants constants, const gpu::TextureRef& picture,
                                    bool depth_known) {
    if (!picture || !scene_color_ || !ensure_mod_post_root()) {
        return;
    }
    const GpuPassScope pass{*this, GpuPass::FramePass};
    const D3D12_RESOURCE_DESC target = scene_color_->GetDesc();
    const UINT samples = std::max(scene_msaa_.samples, 1u);
    ID3D12PipelineState* const pipeline = mod_post_pipeline(program, target.Format, samples);
    if (pipeline == nullptr) {
        return;
    }
    const float width = static_cast<float>(frame_.width);
    const float height = static_cast<float>(frame_.height);
    constants.size[0] = width;
    constants.size[1] = height;
    constants.inv_size[0] = 1.0f / width;
    constants.inv_size[1] = 1.0f / height;
    constants.flags = 0u;

    // The scene's depth: the scene target's own (multisampled: its resolve), or with the output scene target in place
    // (a temporal upscale or the 2D layer at the window's size) the internal one it was drawn at, which covers the
    // same view. None inside a target pass (its own camera).
    ID3D12Resource* depth = nullptr;
    if (depth_known && (program.reads & kModPostReadsDepth) != 0u && !target_pass_active_) {
        if (!output_scene_active_) {
            if (scene_depth_ && scene_msaa_.samples > 1u) {
                resolve_scene(true, false);
            }
            depth = scene_depth_.Get();
        } else if (alt_scene_.depth) {
            // The scene ended with its samples' depth unresolved (begin_output_scene carries its colour only): that
            // set in place for its resolve, then the output scene target back.
            if (alt_scene_.msaa.samples > 1u && alt_scene_.msaa.depth_dirty) {
                swap_scene_sets();
                resolve_scene(true, false);
                swap_scene_sets();
            }
            if (alt_scene_.msaa.samples <= 1u || !alt_scene_.msaa.depth_dirty) {
                depth = alt_scene_.depth.Get();
            }
        }
    }
    // The motion target, when this frame's scene wrote it (a temporal scene), in pixels: where the surface was minus
    // where it is, so uv units are its pixels over its size.
    ID3D12Resource* motion = nullptr;
    if ((program.reads & kModPostReadsMotion) != 0u && !target_pass_active_ && scene_motion_ && motion_target_ &&
        motion_used_frame_ == frame_index_ && motion_target_map_.width != 0u && motion_target_map_.height != 0u) {
        motion = motion_target_.Get();
        constants.motion_scale[0] = 1.0f / static_cast<float>(motion_target_map_.width);
        constants.motion_scale[1] = 1.0f / static_cast<float>(motion_target_map_.height);
    }
    if (depth != nullptr) {
        constants.flags |= kModPostHasDepth;
    }
    if (motion != nullptr) {
        constants.flags |= kModPostHasMotion;
    }

    BarriersD3D12 uses{command_list_.Get()};
    if (depth != nullptr) {
        uses.texture(depth, gpu::Use::DepthWrite, gpu::Use::ShaderRead);
    }
    if (motion != nullptr) {
        uses.texture(motion, gpu::Use::ColorWrite, gpu::Use::PixelRead);
    }
    uses.issue();

    const DescriptorRing::Table table = srv_ring_.allocate(3);
    const UINT step = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_CPU_DESCRIPTOR_HANDLE slot = table.cpu;
    texture_view(device_.Get(), picture.Get(), picture->GetDesc().Format, slot);
    slot.ptr += step;
    if (depth != nullptr) {
        texture_view(device_.Get(), depth, kSceneDepthSrvFormat, slot);
    } else {
        null_view(device_.Get(), DXGI_FORMAT_R32_FLOAT, slot);
    }
    slot.ptr += step;
    if (motion != nullptr) {
        texture_view(device_.Get(), motion, kMotionVectorFormat, slot);
    } else {
        null_view(device_.Get(), DXGI_FORMAT_R16G16_FLOAT, slot);
    }

    ID3D12GraphicsCommandList* const list = command_list_.Get();
    list->OMSetRenderTargets(1, &scene_rtv_, FALSE, nullptr);
    list->SetGraphicsRootSignature(mod_post_root_.Get());
    list->SetPipelineState(pipeline);
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list->SetGraphicsRoot32BitConstants(0, kModPostConstantWords, &constants, 0);
    srv_ring_.bind(list);
    list->SetGraphicsRootDescriptorTable(1, table.gpu);
    const D3D12_VIEWPORT viewport{0.0f, 0.0f, width, height, 0.0f, 1.0f};
    const D3D12_RECT scissor{0, 0, static_cast<LONG>(frame_.width), static_cast<LONG>(frame_.height)};
    list->RSSetViewports(1, &viewport);
    list->RSSetScissorRects(1, &scissor);
    list->DrawInstanced(3, 1, 0, 0);
    uses.undo();
    if (scene_msaa_.samples > 1u) {
        scene_msaa_.color_dirty = true;
    }
    // The scene's own state again for the draws after it.
    list->SetGraphicsRootSignature(root_signature_.Get());
    invalidate_draw_bindings();
    bound_rt_aux_ = 0xFFu;
    bind_scene();
}

void RendererD3D12::record_native_call(const NebulaNativeCall& call, LumaDrawNativeFrame& frame, bool scene_point) {
    if (call.fn == nullptr || !scene_color_) {
        return;
    }
    const GpuPassScope pass{*this, GpuPass::FramePass};
    const bool msaa = scene_msaa_.samples > 1u;
    ID3D12Resource* const target = msaa ? scene_msaa_.color.Get() : scene_color_.Get();
    ID3D12Resource* const depth = scene_point ? (msaa ? scene_msaa_.depth.Get() : scene_depth_.Get()) : nullptr;
    if (target == nullptr) {
        return;
    }
    frame.backend = LUMA_DRAW_NATIVE_D3D12;
    frame.width = frame_.width;
    frame.height = frame_.height;
    frame.samples = std::max(scene_msaa_.samples, 1u);
    frame.d3d12.device = device_.Get();
    frame.d3d12.queue = queue_.Get();
    frame.d3d12.command_list = command_list_.Get();
    frame.d3d12.target = target;
    frame.d3d12.target_rtv = scene_rtv_.ptr;
    frame.d3d12.target_format = static_cast<std::uint32_t>(target->GetDesc().Format);
    if (depth != nullptr) {
        frame.d3d12.depth = depth;
        frame.d3d12.depth_dsv = scene_dsv_.ptr;
        frame.d3d12.depth_format = static_cast<std::uint32_t>(kSceneDepthFormat);
    }
    call.fn(call.user, &frame);
    // What the function drew is in the targets it was given; its bindings, heaps and state are its own.
    if (msaa) {
        scene_msaa_.color_dirty = true;
        scene_msaa_.depth_dirty = scene_msaa_.depth_dirty || depth != nullptr;
    }
    ID3D12GraphicsCommandList* const list = command_list_.Get();
    list->SetGraphicsRootSignature(root_signature_.Get());
    srv_ring_.forget_bound();
    srv_ring_.bind(list);
    invalidate_draw_bindings();
    bound_rt_aux_ = 0xFFu;
    bind_scene();
}

void RendererD3D12::release_mod_post() {
    mod_post_pipelines_.clear();
    mod_post_retired_.clear();
    mod_post_root_.Reset();
    mod_post_root_failed_ = false;
}

}  // namespace galaxy::render
