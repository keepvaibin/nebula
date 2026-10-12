#include "galaxy/gx/post_process.h"
#include "galaxy/gx/renderer_d3d12.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <iostream>

// SMAA lookup textures (third_party/smaa, MIT).
#include "AreaTex.h"
#include "SearchTex.h"

// Pass shaders, compiled from post_process.hlsl by fxc at build time.
#include "post_fullscreen_vs.h"
#include "post_smaa_edges_vs.h"
#include "post_smaa_edges_ps_low.h"
#include "post_smaa_edges_ps_medium.h"
#include "post_smaa_edges_ps_high.h"
#include "post_smaa_edges_ps_ultra.h"
#include "post_smaa_weights_vs_low.h"
#include "post_smaa_weights_vs_medium.h"
#include "post_smaa_weights_vs_high.h"
#include "post_smaa_weights_vs_ultra.h"
#include "post_smaa_weights_ps_low.h"
#include "post_smaa_weights_ps_medium.h"
#include "post_smaa_weights_ps_high.h"
#include "post_smaa_weights_ps_ultra.h"
#include "post_smaa_blend_vs.h"
#include "post_smaa_blend_ps.h"
#include "post_fsr_easu_ps.h"
#include "post_fsr_rcas_ps.h"
#include "post_resample_ps.h"

namespace galaxy::gx {
namespace {

using Microsoft::WRL::ComPtr;

struct ShaderBytes {
    const void* data;
    std::size_t size;
};

template <std::size_t N>
constexpr ShaderBytes bytes(const BYTE (&code)[N]) noexcept {
    return {code, N};
}

constexpr DXGI_FORMAT kColorFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
constexpr DXGI_FORMAT kEdgesFormat = DXGI_FORMAT_R8G8_UNORM;
constexpr DXGI_FORMAT kStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
constexpr unsigned kRtvSlots = 4u;

std::uint32_t bits(float value) noexcept { return std::bit_cast<std::uint32_t>(value); }

// FsrEasuCon with the input viewport equal to the input resource.
void easu_constants(std::uint32_t (&con0)[4], std::uint32_t (&con1)[4],
                    std::uint32_t (&con2)[4], std::uint32_t (&con3)[4],
                    float in_w, float in_h, float out_w, float out_h) noexcept {
    con0[0] = bits(in_w / out_w);
    con0[1] = bits(in_h / out_h);
    con0[2] = bits(0.5f * in_w / out_w - 0.5f);
    con0[3] = bits(0.5f * in_h / out_h - 0.5f);
    con1[0] = bits(1.0f / in_w);
    con1[1] = bits(1.0f / in_h);
    con1[2] = bits(1.0f / in_w);
    con1[3] = bits(-1.0f / in_h);
    con2[0] = bits(-1.0f / in_w);
    con2[1] = bits(2.0f / in_h);
    con2[2] = bits(1.0f / in_w);
    con2[3] = bits(2.0f / in_h);
    con3[0] = bits(0.0f);
    con3[1] = bits(4.0f / in_h);
    con3[2] = 0u;
    con3[3] = 0u;
}

// FsrRcasCon: sharpness in stops, 0 = strongest. The 32-bit path reads con.x.
void rcas_constants(std::uint32_t (&con0)[4], unsigned sharpness_percent) noexcept {
    const float stops = 2.0f * (1.0f - std::clamp(sharpness_percent, 0u, 100u) / 100.0f);
    con0[0] = bits(std::exp2(-stops));
    con0[1] = 0u;
    con0[2] = 0u;
    con0[3] = 0u;
}

D3D12_SHADER_BYTECODE code(ShaderBytes shader) noexcept {
    return {shader.data, shader.size};
}

}  // namespace

PostProcessPlan plan_post_process(
    unsigned source_width, unsigned source_height,
    unsigned target_width, unsigned target_height,
    const PostProcessSettings& settings) noexcept {
    PostProcessPlan plan{};
    if (source_width == 0u || source_height == 0u ||
        target_width == 0u || target_height == 0u) {
        return plan;
    }
    // Supersampled: at least 1.5 source pixels per window pixel on both axes.
    const bool supersampled =
        source_width * 2u >= target_width * 3u &&
        source_height * 2u >= target_height * 3u;
    plan.smaa = settings.aa == PostProcessSettings::Aa::kSmaa ||
        (settings.aa == PostProcessSettings::Aa::kAuto && !supersampled);
    const bool same_size =
        source_width == target_width && source_height == target_height;
    const bool enlarging = target_width >= source_width &&
        target_height >= source_height && !same_size;
    plan.easu = enlarging &&
        settings.upscaler == PostProcessSettings::Upscaler::kFsr1;
    plan.rcas = settings.sharpness > 0u;
    // The box filter handles reduction, mixed axes, bilinear enlargement and
    // the plain copy when neither EASU nor RCAS writes the window.
    plan.resample = !plan.easu && !(same_size && plan.rcas);
    plan.resample_to_target = plan.resample && !plan.rcas;
    return plan;
}

bool PostProcessor::create_pipeline(D3D12_SHADER_BYTECODE vs, D3D12_SHADER_BYTECODE ps,
                                    DXGI_FORMAT format, int stencil,
                                    ComPtr<ID3D12PipelineState>& out) {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
    pd.pRootSignature = root_signature_.Get();
    pd.VS = vs;
    pd.PS = ps;
    pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pd.SampleMask = UINT_MAX;
    pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pd.RasterizerState.DepthClipEnable = TRUE;
    pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pd.NumRenderTargets = 1;
    pd.RTVFormats[0] = format;
    pd.SampleDesc.Count = 1;
    // stencil: 0 none, 1 write 1 where the pixel shader keeps the pixel,
    // 2 draw only where the stencil is 1 (SMAA's edge mask).
    if (stencil != 0) {
        pd.DSVFormat = kStencilFormat;
        auto& ds = pd.DepthStencilState;
        ds.DepthEnable = FALSE;
        ds.StencilEnable = TRUE;
        ds.StencilReadMask = 0xFF;
        ds.StencilWriteMask = 0xFF;
        D3D12_DEPTH_STENCILOP_DESC op{};
        op.StencilFailOp = D3D12_STENCIL_OP_KEEP;
        op.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
        op.StencilPassOp = stencil == 1 ? D3D12_STENCIL_OP_REPLACE : D3D12_STENCIL_OP_KEEP;
        op.StencilFunc = stencil == 1 ? D3D12_COMPARISON_FUNC_ALWAYS
                                      : D3D12_COMPARISON_FUNC_EQUAL;
        ds.FrontFace = op;
        ds.BackFace = op;
    }
    return SUCCEEDED(device_->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&out)));
}

bool PostProcessor::create_smaa_pipelines(unsigned quality,
                                          ComPtr<ID3D12PipelineState>& edges,
                                          ComPtr<ID3D12PipelineState>& weights) {
    static constexpr ShaderBytes kEdgesPs[4] = {
        bytes(g_post_smaa_edges_ps_low), bytes(g_post_smaa_edges_ps_medium),
        bytes(g_post_smaa_edges_ps_high), bytes(g_post_smaa_edges_ps_ultra)};
    static constexpr ShaderBytes kWeightsVs[4] = {
        bytes(g_post_smaa_weights_vs_low), bytes(g_post_smaa_weights_vs_medium),
        bytes(g_post_smaa_weights_vs_high), bytes(g_post_smaa_weights_vs_ultra)};
    static constexpr ShaderBytes kWeightsPs[4] = {
        bytes(g_post_smaa_weights_ps_low), bytes(g_post_smaa_weights_ps_medium),
        bytes(g_post_smaa_weights_ps_high), bytes(g_post_smaa_weights_ps_ultra)};
    const unsigned q = std::min(quality, 3u);
    return create_pipeline(code(bytes(g_post_smaa_edges_vs)), code(kEdgesPs[q]), kEdgesFormat, 1, edges) &&
           create_pipeline(code(kWeightsVs[q]), code(kWeightsPs[q]), kColorFormat, 2, weights);
}

void PostProcessor::apply_settings(const PostProcessSettings& settings,
                                   std::uint64_t frame_index, unsigned frames_in_flight) {
    PostProcessSettings next = settings;
    next.smaa_quality = std::min<std::uint8_t>(next.smaa_quality, 3u);
    if (ready_ && next.smaa_quality != settings_.smaa_quality) {
        ComPtr<ID3D12PipelineState> edges, weights;
        if (!create_smaa_pipelines(next.smaa_quality, edges, weights)) {
            std::cerr << "[post-process] SMAA quality " << unsigned(next.smaa_quality)
                      << " pipelines failed; keeping " << unsigned(settings_.smaa_quality) << '\n';
            next.smaa_quality = settings_.smaa_quality;
        } else {
            const std::uint64_t release = frame_index + frames_in_flight;
            retired_.push_back({nullptr, release, std::move(smaa_edges_)});
            retired_.push_back({nullptr, release, std::move(smaa_weights_)});
            smaa_edges_ = std::move(edges);
            smaa_weights_ = std::move(weights);
        }
    }
    settings_ = next;
}

bool PostProcessor::initialize(ID3D12Device* device, const PostProcessSettings& settings) {
    shutdown();
    device_ = device;
    settings_ = settings;
    settings_.smaa_quality = std::min<std::uint8_t>(settings_.smaa_quality, 3u);
    if (device_ == nullptr) {
        return false;
    }

    D3D12_DESCRIPTOR_RANGE range{};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 3;
    range.BaseShaderRegister = 0;
    range.OffsetInDescriptorsFromTableStart = 0;
    D3D12_ROOT_PARAMETER params[2]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].Constants.ShaderRegister = 0;
    params[0].Constants.Num32BitValues = sizeof(Constants) / 4u;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges = &range;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_STATIC_SAMPLER_DESC samplers[2]{};
    for (unsigned i = 0; i < 2u; ++i) {
        samplers[i].AddressU = samplers[i].AddressV = samplers[i].AddressW =
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
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
        FAILED(device_->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                            IID_PPV_ARGS(&root_signature_)))) {
        std::cerr << "[post-process] root signature failed\n";
        shutdown();
        return false;
    }

    const bool ok =
        create_smaa_pipelines(settings_.smaa_quality, smaa_edges_, smaa_weights_) &&
        create_pipeline(code(bytes(g_post_smaa_blend_vs)), code(bytes(g_post_smaa_blend_ps)), kColorFormat, 0, smaa_blend_) &&
        create_pipeline(code(bytes(g_post_fullscreen_vs)), code(bytes(g_post_fsr_easu_ps)), kColorFormat, 0, easu_) &&
        create_pipeline(code(bytes(g_post_fullscreen_vs)), code(bytes(g_post_fsr_rcas_ps)), kColorFormat, 0, rcas_) &&
        create_pipeline(code(bytes(g_post_fullscreen_vs)), code(bytes(g_post_resample_ps)), kColorFormat, 0, resample_);
    if (!ok) {
        std::cerr << "[post-process] pipeline creation failed\n";
        shutdown();
        return false;
    }

    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    hd.NumDescriptors = kRtvSlots;
    if (FAILED(device_->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&rtv_heap_)))) {
        shutdown();
        return false;
    }
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    hd.NumDescriptors = 1;
    if (FAILED(device_->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&dsv_heap_)))) {
        shutdown();
        return false;
    }
    rtv_increment_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    const auto texture = [&](unsigned w, unsigned h, DXGI_FORMAT format,
                             ComPtr<ID3D12Resource>& out) {
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width = w;
        rd.Height = h;
        rd.DepthOrArraySize = 1;
        rd.MipLevels = 1;
        rd.Format = format;
        rd.SampleDesc.Count = 1;
        return SUCCEEDED(device_->CreateCommittedResource(
            &heap, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr, IID_PPV_ARGS(&out)));
    };
    if (!texture(AREATEX_WIDTH, AREATEX_HEIGHT, DXGI_FORMAT_R8G8_UNORM, area_texture_) ||
        !texture(SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT, DXGI_FORMAT_R8_UNORM, search_texture_)) {
        shutdown();
        return false;
    }
    lookup_uploaded_ = false;
    ready_ = true;
    return true;
}

void PostProcessor::shutdown() noexcept {
    ready_ = false;
    lookup_uploaded_ = false;
    smaa_edges_.Reset();
    smaa_weights_.Reset();
    smaa_blend_.Reset();
    easu_.Reset();
    rcas_.Reset();
    resample_.Reset();
    root_signature_.Reset();
    area_texture_.Reset();
    search_texture_.Reset();
    rtv_heap_.Reset();
    dsv_heap_.Reset();
    edges_ = {};
    weights_ = {};
    antialiased_ = {};
    scaled_ = {};
    stencil_ = {};
    retired_.clear();
    device_ = nullptr;
}

void PostProcessor::release_retired(std::uint64_t frame_index) noexcept {
    retired_.erase(
        std::remove_if(retired_.begin(), retired_.end(), [&](const Retired& r) {
            return frame_index > r.release_after_frame;
        }),
        retired_.end());
}

bool PostProcessor::ensure_target(Target& target, unsigned width, unsigned height,
                                  DXGI_FORMAT format, unsigned rtv_slot,
                                  std::uint64_t frame_index, unsigned frames_in_flight) {
    if (target.resource && target.width == width && target.height == height &&
        target.format == format) {
        return true;
    }
    if (target.resource) {
        retired_.push_back({std::move(target.resource), frame_index + frames_in_flight});
    }
    target = {};
    const bool depth = format == kStencilFormat;
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
    rd.Flags = depth ? D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL | D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE
                     : D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_CLEAR_VALUE clear{};
    clear.Format = format;
    clear.DepthStencil.Depth = 1.0f;
    clear.DepthStencil.Stencil = 0;
    const D3D12_RESOURCE_STATES initial =
        depth ? D3D12_RESOURCE_STATE_DEPTH_WRITE : D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    if (FAILED(device_->CreateCommittedResource(
            &heap, D3D12_HEAP_FLAG_NONE, &rd, initial, &clear,
            IID_PPV_ARGS(&target.resource)))) {
        return false;
    }
    target.width = width;
    target.height = height;
    target.format = format;
    target.state = initial;
    if (depth) {
        target.rtv = dsv_heap_->GetCPUDescriptorHandleForHeapStart();
        device_->CreateDepthStencilView(target.resource.Get(), nullptr, target.rtv);
    } else {
        target.rtv = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
        target.rtv.ptr += static_cast<SIZE_T>(rtv_slot) * rtv_increment_;
        device_->CreateRenderTargetView(target.resource.Get(), nullptr, target.rtv);
    }
    return true;
}

void PostProcessor::transition(ID3D12GraphicsCommandList* list, Target& target,
                               D3D12_RESOURCE_STATES state) {
    if (target.state == state) {
        return;
    }
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = target.resource.Get();
    b.Transition.StateBefore = target.state;
    b.Transition.StateAfter = state;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    list->ResourceBarrier(1, &b);
    target.state = state;
}

void PostProcessor::upload_lookup_textures(ID3D12GraphicsCommandList* list,
                                           std::uint64_t frame_index,
                                           unsigned frames_in_flight) {
    const auto upload = [&](ID3D12Resource* destination, const unsigned char* data,
                            unsigned width, unsigned height, unsigned pitch,
                            DXGI_FORMAT format) {
        const unsigned aligned = (pitch + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u) &
                                 ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u);
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        rd.Width = static_cast<UINT64>(aligned) * height;
        rd.Height = 1;
        rd.DepthOrArraySize = 1;
        rd.MipLevels = 1;
        rd.SampleDesc.Count = 1;
        rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        ComPtr<ID3D12Resource> staging;
        if (FAILED(device_->CreateCommittedResource(
                &heap, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr, IID_PPV_ARGS(&staging)))) {
            return false;
        }
        unsigned char* mapped = nullptr;
        const D3D12_RANGE none{0, 0};
        if (FAILED(staging->Map(0, &none, reinterpret_cast<void**>(&mapped)))) {
            return false;
        }
        for (unsigned row = 0; row < height; ++row) {
            std::memcpy(mapped + static_cast<std::size_t>(row) * aligned,
                        data + static_cast<std::size_t>(row) * pitch, pitch);
        }
        staging->Unmap(0, nullptr);
        D3D12_TEXTURE_COPY_LOCATION src{};
        src.pResource = staging.Get();
        src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint.Footprint.Format = format;
        src.PlacedFootprint.Footprint.Width = width;
        src.PlacedFootprint.Footprint.Height = height;
        src.PlacedFootprint.Footprint.Depth = 1;
        src.PlacedFootprint.Footprint.RowPitch = aligned;
        D3D12_TEXTURE_COPY_LOCATION dst{};
        dst.pResource = destination;
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.SubresourceIndex = 0;
        list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = destination;
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        list->ResourceBarrier(1, &b);
        retired_.push_back({std::move(staging), frame_index + frames_in_flight});
        return true;
    };
    lookup_uploaded_ =
        upload(area_texture_.Get(), areaTexBytes, AREATEX_WIDTH, AREATEX_HEIGHT,
               AREATEX_PITCH, DXGI_FORMAT_R8G8_UNORM) &&
        upload(search_texture_.Get(), searchTexBytes, SEARCHTEX_WIDTH,
               SEARCHTEX_HEIGHT, SEARCHTEX_PITCH, DXGI_FORMAT_R8_UNORM);
}

void PostProcessor::draw(ID3D12GraphicsCommandList* list, DescriptorRing& srv_ring,
                         ID3D12PipelineState* pipeline,
                         std::initializer_list<ID3D12Resource*> inputs,
                         std::initializer_list<DXGI_FORMAT> formats,
                         const D3D12_VIEWPORT& viewport,
                         const Constants& constants) {
    const DescriptorRing::Table table = srv_ring.allocate(3);
    const UINT increment = device_->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_CPU_DESCRIPTOR_HANDLE slot = table.cpu;
    auto format = formats.begin();
    unsigned written = 0;
    for (ID3D12Resource* input : inputs) {
        D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format = *format++;
        sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        sd.Texture2D.MipLevels = 1;
        device_->CreateShaderResourceView(input, &sd, slot);
        slot.ptr += increment;
        ++written;
    }
    for (; written < 3u; ++written) {
        D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format = kColorFormat;
        sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        sd.Texture2D.MipLevels = 1;
        device_->CreateShaderResourceView(nullptr, &sd, slot);
        slot.ptr += increment;
    }
    list->SetPipelineState(pipeline);
    list->SetGraphicsRoot32BitConstants(0, sizeof(Constants) / 4u, &constants, 0);
    list->SetGraphicsRootDescriptorTable(1, table.gpu);
    const D3D12_RECT scissor{
        static_cast<LONG>(viewport.TopLeftX), static_cast<LONG>(viewport.TopLeftY),
        static_cast<LONG>(viewport.TopLeftX + viewport.Width),
        static_cast<LONG>(viewport.TopLeftY + viewport.Height)};
    list->RSSetViewports(1, &viewport);
    list->RSSetScissorRects(1, &scissor);
    list->DrawInstanced(3, 1, 0, 0);
}

void PostProcessor::record(ID3D12GraphicsCommandList* list,
                           DescriptorRing& srv_ring,
                           ID3D12Resource* source,
                           unsigned source_width,
                           unsigned source_height,
                           D3D12_CPU_DESCRIPTOR_HANDLE target_rtv,
                           const D3D12_VIEWPORT& viewport,
                           std::uint64_t frame_index,
                           unsigned frames_in_flight) {
    release_retired(frame_index);
    const unsigned target_width = static_cast<unsigned>(viewport.Width);
    const unsigned target_height = static_cast<unsigned>(viewport.Height);
    PostProcessPlan plan = plan_post_process(
        source_width, source_height, target_width, target_height, settings_);
    if (plan.smaa && !lookup_uploaded_) {
        upload_lookup_textures(list, frame_index, frames_in_flight);
        plan.smaa = lookup_uploaded_;
    }

    list->SetGraphicsRootSignature(root_signature_.Get());
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    const D3D12_VIEWPORT source_viewport{
        0.0f, 0.0f, static_cast<float>(source_width), static_cast<float>(source_height), 0.0f, 1.0f};
    const D3D12_VIEWPORT window_sized{
        0.0f, 0.0f, static_cast<float>(target_width), static_cast<float>(target_height), 0.0f, 1.0f};
    Constants c{};
    c.rt_metrics[0] = 1.0f / static_cast<float>(source_width);
    c.rt_metrics[1] = 1.0f / static_cast<float>(source_height);
    c.rt_metrics[2] = static_cast<float>(source_width);
    c.rt_metrics[3] = static_cast<float>(source_height);

    ID3D12Resource* image = source;
    if (plan.smaa &&
        ensure_target(edges_, source_width, source_height, kEdgesFormat, 0u, frame_index, frames_in_flight) &&
        ensure_target(weights_, source_width, source_height, kColorFormat, 1u, frame_index, frames_in_flight) &&
        ensure_target(antialiased_, source_width, source_height, kColorFormat, 2u, frame_index, frames_in_flight) &&
        ensure_target(stencil_, source_width, source_height, kStencilFormat, 0u, frame_index, frames_in_flight)) {
        constexpr float kZero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        const D3D12_CPU_DESCRIPTOR_HANDLE dsv = stencil_.rtv;
        transition(list, edges_, D3D12_RESOURCE_STATE_RENDER_TARGET);
        list->ClearRenderTargetView(edges_.rtv, kZero, 0, nullptr);
        list->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
        list->OMSetRenderTargets(1, &edges_.rtv, FALSE, &dsv);
        list->OMSetStencilRef(1);
        draw(list, srv_ring, smaa_edges_.Get(), {source}, {kColorFormat},
             source_viewport, c);
        transition(list, edges_, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        transition(list, weights_, D3D12_RESOURCE_STATE_RENDER_TARGET);
        list->ClearRenderTargetView(weights_.rtv, kZero, 0, nullptr);
        list->OMSetRenderTargets(1, &weights_.rtv, FALSE, &dsv);
        draw(list, srv_ring, smaa_weights_.Get(),
             {edges_.resource.Get(), area_texture_.Get(), search_texture_.Get()},
             {kEdgesFormat, DXGI_FORMAT_R8G8_UNORM, DXGI_FORMAT_R8_UNORM},
             source_viewport, c);
        transition(list, weights_, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        transition(list, antialiased_, D3D12_RESOURCE_STATE_RENDER_TARGET);
        list->OMSetRenderTargets(1, &antialiased_.rtv, FALSE, nullptr);
        draw(list, srv_ring, smaa_blend_.Get(), {source, weights_.resource.Get()},
             {kColorFormat, kColorFormat}, source_viewport, c);
        transition(list, antialiased_, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        image = antialiased_.resource.Get();
    }

    // Window-sized intermediate for a scaling step that RCAS then reads.
    const bool needs_scaled = (plan.easu || plan.resample) && plan.rcas;
    if (needs_scaled &&
        !ensure_target(scaled_, target_width, target_height, kColorFormat, 3u,
                       frame_index, frames_in_flight)) {
        plan.rcas = false;
        plan.resample = !plan.easu;
        plan.resample_to_target = plan.resample;
    }

    const auto into = [&](bool to_target, D3D12_VIEWPORT& out_viewport,
                          Constants& constants) {
        if (to_target) {
            list->OMSetRenderTargets(1, &target_rtv, FALSE, nullptr);
            out_viewport = viewport;
            constants.origin[0] = static_cast<std::int32_t>(viewport.TopLeftX);
            constants.origin[1] = static_cast<std::int32_t>(viewport.TopLeftY);
        } else {
            transition(list, scaled_, D3D12_RESOURCE_STATE_RENDER_TARGET);
            list->OMSetRenderTargets(1, &scaled_.rtv, FALSE, nullptr);
            out_viewport = window_sized;
            constants.origin[0] = 0;
            constants.origin[1] = 0;
        }
    };

    if (plan.easu) {
        Constants e = c;
        easu_constants(e.con0, e.con1, e.con2, e.con3,
                       static_cast<float>(source_width), static_cast<float>(source_height),
                       static_cast<float>(target_width), static_cast<float>(target_height));
        D3D12_VIEWPORT vp{};
        into(!plan.rcas, vp, e);
        draw(list, srv_ring, easu_.Get(), {image}, {kColorFormat}, vp, e);
        if (plan.rcas) {
            transition(list, scaled_, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            image = scaled_.resource.Get();
        }
    } else if (plan.resample) {
        Constants r = c;
        r.params[0] = static_cast<float>(source_width);
        r.params[1] = static_cast<float>(source_height);
        r.params[2] = static_cast<float>(target_width);
        r.params[3] = static_cast<float>(target_height);
        D3D12_VIEWPORT vp{};
        into(plan.resample_to_target, vp, r);
        draw(list, srv_ring, resample_.Get(), {image}, {kColorFormat}, vp, r);
        if (!plan.resample_to_target) {
            transition(list, scaled_, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            image = scaled_.resource.Get();
        }
    }

    if (plan.rcas) {
        Constants s = c;
        rcas_constants(s.con0, settings_.sharpness);
        s.params[0] = static_cast<float>(target_width);
        s.params[1] = static_cast<float>(target_height);
        D3D12_VIEWPORT vp{};
        into(true, vp, s);
        draw(list, srv_ring, rcas_.Get(), {image}, {kColorFormat}, vp, s);
    }
}

}  // namespace galaxy::gx
