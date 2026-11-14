// PostProcessor: the D3D12 presentation chain (post_process.h).
#include "galaxy/render/post_process.h"
#include "barriers_d3d12.h"
#include "galaxy/render/renderer_d3d12.h"

#include <algorithm>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>

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
#include "post_smaa_blend_keep_ps.h"
#include "post_fsr_easu_ps.h"
#include "post_fsr_rcas_ps.h"
#include "post_fsr_rcas_scaled_ps.h"
#include "post_resample_ps.h"
#include "post_copy_ps.h"
#include "post_fxaa_ps.h"

namespace galaxy::render {
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

// Pictures between passes, as the scene; the window.
constexpr DXGI_FORMAT kColorFormat = kSceneColorFormat;
constexpr DXGI_FORMAT kWindowFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
// The HDR10 swap chain's buffers.
constexpr DXGI_FORMAT kHdrWindowFormat = DXGI_FORMAT_R10G10B10A2_UNORM;
// SMAA's blending weights: one in each channel, the alpha's as fine as the rest.
constexpr DXGI_FORMAT kWeightsFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
constexpr DXGI_FORMAT kEdgesFormat = DXGI_FORMAT_R8G8_UNORM;
// SMAA's edge marks: its stencil alone is used, never its depth, so the smallest format with a stencil.
constexpr DXGI_FORMAT kStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
constexpr unsigned kRtvSlots = 7u;

D3D12_SHADER_BYTECODE code(ShaderBytes shader) noexcept {
    return {shader.data, shader.size};
}

}  // namespace

bool PostProcessor::create_pipeline(D3D12_SHADER_BYTECODE vs, D3D12_SHADER_BYTECODE ps,
                                    DXGI_FORMAT format, int stencil,
                                    ComPtr<ID3D12PipelineState>& out, UINT8 write_mask, UINT samples) {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
    pd.pRootSignature = root_signature_.Get();
    pd.VS = vs;
    pd.PS = ps;
    pd.BlendState.RenderTarget[0].RenderTargetWriteMask = write_mask;
    pd.SampleMask = UINT_MAX;
    pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pd.RasterizerState.DepthClipEnable = TRUE;
    pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pd.NumRenderTargets = 1;
    pd.RTVFormats[0] = format;
    pd.SampleDesc.Count = samples;
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
           create_pipeline(code(kWeightsVs[q]), code(kWeightsPs[q]), kWeightsFormat, 2, weights);
}

void PostProcessor::apply_settings(const PostProcessSettings& settings,
                                   std::uint64_t frame_index, unsigned frames_in_flight) {
    PostProcessSettings next = settings;
    next.smaa_quality = std::min<std::uint8_t>(next.smaa_quality, 3u);
    if (ready_ && status_.smaa_ok && next.smaa_quality != settings_.smaa_quality) {
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
    status_.live_aa = static_cast<std::uint8_t>(settings_.aa);
    status_.live_quality = settings_.smaa_quality;
}

namespace {

template <std::size_t N>
void set_reason(std::array<char, N>& out, const char* text) noexcept {
    std::snprintf(out.data(), out.size(), "%s", text);
}

}  // namespace

bool PostProcessor::initialize(ID3D12Device* device, ID3D12CommandQueue* queue,
                               const PostProcessSettings& settings) {
    shutdown();
    device_ = device;
    settings_ = settings;
    settings_.smaa_quality = std::min<std::uint8_t>(settings_.smaa_quality, 3u);
    status_ = {};
    status_.live_aa = static_cast<std::uint8_t>(settings_.aa);
    status_.live_quality = settings_.smaa_quality;
    set_reason(status_.smaa_reason, "INIT FAILED");
    set_reason(status_.fxaa_reason, "INIT FAILED");
    if (device_ == nullptr || queue == nullptr) {
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

    // The scaling passes are required; each AA pass is optional and only
    // reported when it cannot be built.
    const bool scaling =
        create_pipeline(code(bytes(g_post_fullscreen_vs)), code(bytes(g_post_fsr_easu_ps)), kColorFormat, 0, easu_) &&
        create_pipeline(code(bytes(g_post_fullscreen_vs)), code(bytes(g_post_fsr_easu_ps)), kWindowFormat, 0, easu_window_) &&
        create_pipeline(code(bytes(g_post_fullscreen_vs)), code(bytes(g_post_fsr_rcas_ps)), kWindowFormat, 0, rcas_) &&
        create_pipeline(code(bytes(g_post_fullscreen_vs)), code(bytes(g_post_fsr_rcas_scaled_ps)), kWindowFormat, 0,
                        rcas_scaled_) &&
        create_pipeline(code(bytes(g_post_fullscreen_vs)), code(bytes(g_post_resample_ps)), kColorFormat, 0, resample_) &&
        create_pipeline(code(bytes(g_post_fullscreen_vs)), code(bytes(g_post_resample_ps)), kWindowFormat, 0, resample_window_) &&
        create_pipeline(code(bytes(g_post_fullscreen_vs)), code(bytes(g_post_copy_ps)), kWindowFormat, 0, copy_window_);
    if (!scaling) {
        std::cerr << "[post-process] pipeline creation failed\n";
        shutdown();
        return false;
    }
    constexpr UINT8 kRgb = D3D12_COLOR_WRITE_ENABLE_RED | D3D12_COLOR_WRITE_ENABLE_GREEN |
                           D3D12_COLOR_WRITE_ENABLE_BLUE;
    const bool smaa_pipelines =
        create_smaa_pipelines(settings_.smaa_quality, smaa_edges_, smaa_weights_) &&
        create_pipeline(code(bytes(g_post_smaa_blend_vs)), code(bytes(g_post_smaa_blend_ps)), kColorFormat, 0, smaa_blend_) &&
        create_pipeline(code(bytes(g_post_smaa_blend_vs)), code(bytes(g_post_smaa_blend_keep_ps)), kColorFormat, 0,
                        smaa_blend_rgb_, kRgb);
    const bool fxaa_pipeline =
        create_pipeline(code(bytes(g_post_fullscreen_vs)), code(bytes(g_post_fxaa_ps)), kColorFormat, 0, fxaa_) &&
        create_pipeline(code(bytes(g_post_fullscreen_vs)), code(bytes(g_post_fxaa_ps)), kColorFormat, 0, fxaa_rgb_,
                        kRgb);

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
        return SUCCEEDED(create_texture(device_, &heap, D3D12_HEAP_FLAG_NONE, &rd, gpu::Use::CopyDest,
            nullptr, IID_PPV_ARGS(&out)));
    };
    const bool lookup_textures =
        texture(AREATEX_WIDTH, AREATEX_HEIGHT, DXGI_FORMAT_R8G8_UNORM, area_texture_) &&
        texture(SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT, DXGI_FORMAT_R8_UNORM, search_texture_);
    ready_ = true;

    const AaAvailability available = aa_availability(smaa_pipelines, lookup_textures, fxaa_pipeline);
    set_reason(status_.smaa_reason, available.smaa_reason);
    set_reason(status_.fxaa_reason, available.fxaa_reason);
    status_.smaa_ok = available.smaa_ok;
    status_.fxaa_ok = available.fxaa_ok;
    self_test(queue);
    status_.initialized = true;
    std::cerr << "[post-process] self-test smaa=" << (status_.smaa_ok ? "pass" : "fail")
              << (status_.smaa_ok ? "" : " smaa-reason=") << status_.smaa_reason.data()
              << " fxaa=" << (status_.fxaa_ok ? "pass" : "fail")
              << (status_.fxaa_ok ? "" : " fxaa-reason=") << status_.fxaa_reason.data() << '\n';
    return true;
}

bool PostProcessor::prepare_hdr() {
    if (!ready_) {
        return false;
    }
    if (easu_hdr_ && rcas_hdr_ && rcas_scaled_hdr_ && resample_hdr_ && copy_hdr_) {
        return true;
    }
    const bool made =
        create_pipeline(code(bytes(g_post_fullscreen_vs)), code(bytes(g_post_fsr_easu_ps)), kHdrWindowFormat, 0, easu_hdr_) &&
        create_pipeline(code(bytes(g_post_fullscreen_vs)), code(bytes(g_post_fsr_rcas_ps)), kHdrWindowFormat, 0, rcas_hdr_) &&
        create_pipeline(code(bytes(g_post_fullscreen_vs)), code(bytes(g_post_fsr_rcas_scaled_ps)), kHdrWindowFormat, 0,
                        rcas_scaled_hdr_) &&
        create_pipeline(code(bytes(g_post_fullscreen_vs)), code(bytes(g_post_resample_ps)), kHdrWindowFormat, 0, resample_hdr_) &&
        create_pipeline(code(bytes(g_post_fullscreen_vs)), code(bytes(g_post_copy_ps)), kHdrWindowFormat, 0, copy_hdr_);
    if (!made) {
        easu_hdr_.Reset();
        rcas_hdr_.Reset();
        rcas_scaled_hdr_.Reset();
        resample_hdr_.Reset();
        copy_hdr_.Reset();
        std::cerr << "[hdr] the HDR10 last-write pipelines could not be made\n";
    }
    return made;
}

void PostProcessor::shutdown() noexcept {
    easu_window_.Reset();
    resample_window_.Reset();
    copy_window_.Reset();
    easu_hdr_.Reset();
    rcas_hdr_.Reset();
    rcas_scaled_hdr_.Reset();
    resample_hdr_.Reset();
    copy_hdr_.Reset();
    ready_ = false;
    status_.initialized = false;
    status_.smaa_ok = false;
    status_.fxaa_ok = false;
    smaa_edges_.Reset();
    smaa_weights_.Reset();
    smaa_blend_.Reset();
    fxaa_.Reset();
    smaa_blend_rgb_.Reset();
    fxaa_rgb_.Reset();
    for (auto& pass : scene_pass_ms_) {
        for (auto& pipeline : pass) pipeline.Reset();
    }
    easu_.Reset();
    rcas_.Reset();
    rcas_scaled_.Reset();
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
    traded_.Reset();
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
        target.used_frame = frame_index;
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
    const gpu::Use initial = depth ? gpu::Use::DepthWrite : gpu::Use::PixelRead;
    if (FAILED(create_texture(device_, &heap, D3D12_HEAP_FLAG_NONE, &rd, initial, &clear,
            IID_PPV_ARGS(&target.resource)))) {
        return false;
    }
    target.width = width;
    target.height = height;
    target.format = format;
    target.use = initial;
    if (depth) {
        target.rtv = dsv_heap_->GetCPUDescriptorHandleForHeapStart();
        device_->CreateDepthStencilView(target.resource.Get(), nullptr, target.rtv);
    } else {
        target.rtv = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
        target.rtv.ptr += static_cast<SIZE_T>(rtv_slot) * rtv_increment_;
        device_->CreateRenderTargetView(target.resource.Get(), nullptr, target.rtv);
    }
    target.used_frame = frame_index;
    return true;
}

void PostProcessor::trim_idle_targets(std::uint64_t frame_index, unsigned frames_in_flight) {
    constexpr std::uint64_t kTargetIdleFrames = 600u;
    for (Target* target : {&edges_, &weights_, &antialiased_, &scaled_, &stencil_}) {
        if (target->resource && frame_index > target->used_frame + kTargetIdleFrames) {
            retired_.push_back({std::move(target->resource), frame_index + frames_in_flight});
            *target = {};
        }
    }
    if (traded_ && frame_index > traded_frame_ + kTargetIdleFrames) {
        retired_.push_back({std::move(traded_), frame_index + frames_in_flight});
    }
}

void PostProcessor::transition(ID3D12GraphicsCommandList* list, Target& target, gpu::Use use) {
    if (target.use == use) {
        return;
    }
    BarriersD3D12 uses{list};
    uses.texture(target.resource.Get(), target.use, use);
    uses.issue();
    target.use = use;
}

bool PostProcessor::record_lookup_upload(ID3D12GraphicsCommandList* list,
                                         std::vector<ComPtr<ID3D12Resource>>& staging) {
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
        ComPtr<ID3D12Resource> buffer;
        if (FAILED(device_->CreateCommittedResource(
                &heap, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr, IID_PPV_ARGS(&buffer)))) {
            return false;
        }
        unsigned char* mapped = nullptr;
        const D3D12_RANGE none{0, 0};
        if (FAILED(buffer->Map(0, &none, reinterpret_cast<void**>(&mapped)))) {
            return false;
        }
        for (unsigned row = 0; row < height; ++row) {
            std::memcpy(mapped + static_cast<std::size_t>(row) * aligned,
                        data + static_cast<std::size_t>(row) * pitch, pitch);
        }
        buffer->Unmap(0, nullptr);
        D3D12_TEXTURE_COPY_LOCATION src{};
        src.pResource = buffer.Get();
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
        BarriersD3D12 uses{list};
        uses.texture(destination, gpu::Use::CopyDest, gpu::Use::PixelRead);
        uses.issue();
        staging.push_back(std::move(buffer));
        return true;
    };
    return upload(area_texture_.Get(), areaTexBytes, AREATEX_WIDTH, AREATEX_HEIGHT,
                  AREATEX_PITCH, DXGI_FORMAT_R8G8_UNORM) &&
           upload(search_texture_.Get(), searchTexBytes, SEARCHTEX_WIDTH,
                  SEARCHTEX_HEIGHT, SEARCHTEX_PITCH, DXGI_FORMAT_R8_UNORM);
}

void PostProcessor::draw(ID3D12GraphicsCommandList* list, DescriptorRing& srv_ring,
                         ID3D12PipelineState* pipeline,
                         std::initializer_list<ID3D12Resource*> inputs,
                         const D3D12_VIEWPORT& viewport,
                         const Constants& constants) {
    const DescriptorRing::Table table = srv_ring.allocate(3);
    const UINT increment = device_->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_CPU_DESCRIPTOR_HANDLE slot = table.cpu;
    unsigned written = 0;
    for (ID3D12Resource* input : inputs) {
        D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format = input->GetDesc().Format;
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
    srv_ring.bind(list);
    list->SetGraphicsRootDescriptorTable(1, table.gpu);
    const D3D12_RECT full{
        static_cast<LONG>(viewport.TopLeftX), static_cast<LONG>(viewport.TopLeftY),
        static_cast<LONG>(viewport.TopLeftX + viewport.Width),
        static_cast<LONG>(viewport.TopLeftY + viewport.Height)};
    list->RSSetViewports(1, &viewport);
    list->RSSetScissorRects(1, &full);
    list->DrawInstanced(3, 1, 0, 0);
}

bool PostProcessor::record_aa(ID3D12GraphicsCommandList* list, DescriptorRing& srv_ring,
                              AaRun pass, ID3D12Resource* source,
                              unsigned width, unsigned height,
                              std::uint64_t frame_index, unsigned frames_in_flight) {
    if (pass == AaRun::kNone ||
        !ensure_target(antialiased_, width, height, kColorFormat, 2u, frame_index, frames_in_flight)) {
        return false;
    }
    if (pass == AaRun::kSmaa &&
        !(ensure_target(edges_, width, height, kEdgesFormat, 0u, frame_index, frames_in_flight) &&
          ensure_target(weights_, width, height, kWeightsFormat, 1u, frame_index, frames_in_flight) &&
          ensure_target(stencil_, width, height, kStencilFormat, 0u, frame_index, frames_in_flight))) {
        return false;
    }
    list->SetGraphicsRootSignature(root_signature_.Get());
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    const D3D12_VIEWPORT viewport{
        0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f};
    Constants c{};
    set_post_input_size(c, width, height);
    if (pass == AaRun::kFxaa) {
        transition(list, antialiased_, gpu::Use::ColorWrite);
        list->OMSetRenderTargets(1, &antialiased_.rtv, FALSE, nullptr);
        draw(list, srv_ring, fxaa_.Get(), {source}, viewport, c);
        transition(list, antialiased_, gpu::Use::PixelRead);
        return true;
    }
    constexpr float kZero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    const D3D12_CPU_DESCRIPTOR_HANDLE dsv = stencil_.rtv;
    transition(list, edges_, gpu::Use::ColorWrite);
    list->ClearRenderTargetView(edges_.rtv, kZero, 0, nullptr);
    list->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
    list->OMSetRenderTargets(1, &edges_.rtv, FALSE, &dsv);
    list->OMSetStencilRef(1);
    draw(list, srv_ring, smaa_edges_.Get(), {source}, viewport, c);
    transition(list, edges_, gpu::Use::PixelRead);
    transition(list, weights_, gpu::Use::ColorWrite);
    list->ClearRenderTargetView(weights_.rtv, kZero, 0, nullptr);
    list->OMSetRenderTargets(1, &weights_.rtv, FALSE, &dsv);
    draw(list, srv_ring, smaa_weights_.Get(),
         {edges_.resource.Get(), area_texture_.Get(), search_texture_.Get()},
         viewport, c);
    transition(list, weights_, gpu::Use::PixelRead);
    transition(list, antialiased_, gpu::Use::ColorWrite);
    list->OMSetRenderTargets(1, &antialiased_.rtv, FALSE, nullptr);
    draw(list, srv_ring, smaa_blend_.Get(), {source, weights_.resource.Get()},
         viewport, c);
    transition(list, antialiased_, gpu::Use::PixelRead);
    return true;
}

void PostProcessor::note_aa_run(const PostProcessPlan& plan, AaRun ran) {
    ++status_.frames;
    status_.last_run = ran;
    status_.last_wanted = plan.aa_wanted;
    status_.last_supersampled = plan.supersampled;
    status_.smaa_frames += ran == AaRun::kSmaa;
    status_.fxaa_frames += ran == AaRun::kFxaa;
    if (plan.aa_wanted) {
        ++status_.aa_wanted_frames;
        if (ran == AaRun::kNone) {
            ++status_.aa_skipped_frames;
            set_reason(status_.skip_reason, plan.smaa || plan.fxaa ? "NO MEMORY" : "FAILED");
        }
    }
}

AaRun PostProcessor::record_scene_aa(ID3D12GraphicsCommandList* list, DescriptorRing& srv_ring,
                                     ID3D12Resource* color, D3D12_CPU_DESCRIPTOR_HANDLE color_rtv,
                                     unsigned width, unsigned height, unsigned picture_width,
                                     unsigned picture_height, unsigned display_width,
                                     unsigned display_height, std::uint64_t frame_index,
                                     unsigned frames_in_flight, unsigned samples, const SceneTrade& trade) {
    if (!ready_ || color == nullptr || width == 0u || height == 0u) {
        return AaRun::kNone;
    }
    release_retired(frame_index);
    trim_idle_targets(frame_index, frames_in_flight);
    const PostProcessPlan plan = plan_post_process(picture_width, picture_height, display_width,
                                                   display_height, settings_, status_.smaa_ok, status_.fxaa_ok);
    AaRun pass = plan.smaa ? AaRun::kSmaa : plan.fxaa ? AaRun::kFxaa : AaRun::kNone;
    if (pass == AaRun::kSmaa &&
        !(ensure_target(edges_, width, height, kEdgesFormat, 0u, frame_index, frames_in_flight) &&
          ensure_target(weights_, width, height, kWeightsFormat, 1u, frame_index, frames_in_flight) &&
          ensure_target(stencil_, width, height, kStencilFormat, 0u, frame_index, frames_in_flight))) {
        pass = status_.fxaa_ok ? AaRun::kFxaa : AaRun::kNone;
    }
    // The passes read the scene's own image, traded out, and write every pixel and channel of the image that took its
    // place; or they read a copy of the scene and write its colour back into the scene target.
    const bool traded = static_cast<bool>(trade);
    if (pass != AaRun::kNone && !traded &&
        !ensure_target(antialiased_, width, height, kColorFormat, 2u, frame_index, frames_in_flight)) {
        pass = AaRun::kNone;
    }
    // The last pass's pipeline (over MSAA made at first use): without one the scene is left as it is.
    ID3D12PipelineState* const last_pass = pass == AaRun::kNone ? nullptr
                                           : traded             ? (pass == AaRun::kSmaa ? smaa_blend_.Get() : fxaa_.Get())
                                                                : scene_pass_pipeline(pass == AaRun::kSmaa, samples);
    if (last_pass == nullptr) {
        pass = AaRun::kNone;
    }
    note_aa_run(plan, pass);
    if (pass == AaRun::kNone) {
        return pass;
    }
    if (traded) {
        traded_ = trade(std::move(traded_));
        traded_frame_ = frame_index;
    } else {
        // The scene and the copy change in one call each way.
        BarriersD3D12 to_copy{list};
        to_copy.texture(color, gpu::Use::ColorWrite, gpu::Use::CopySource);
        to_copy.texture(antialiased_.resource.Get(), antialiased_.use, gpu::Use::CopyDest);
        to_copy.issue();
        list->CopyResource(antialiased_.resource.Get(), color);
        BarriersD3D12 back{list};
        back.texture(color, gpu::Use::CopySource, gpu::Use::ColorWrite);
        back.texture(antialiased_.resource.Get(), gpu::Use::CopyDest, gpu::Use::PixelRead);
        back.issue();
        antialiased_.use = gpu::Use::PixelRead;
    }

    ID3D12Resource* source = traded ? traded_.Get() : antialiased_.resource.Get();
    list->SetGraphicsRootSignature(root_signature_.Get());
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    const D3D12_VIEWPORT viewport{
        0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f};
    Constants c{};
    set_post_input_size(c, width, height);
    if (pass == AaRun::kFxaa) {
        list->OMSetRenderTargets(1, &color_rtv, FALSE, nullptr);
        draw(list, srv_ring, last_pass, {source}, viewport, c);
        return pass;
    }
    constexpr float kZero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    const D3D12_CPU_DESCRIPTOR_HANDLE dsv = stencil_.rtv;
    transition(list, edges_, gpu::Use::ColorWrite);
    list->ClearRenderTargetView(edges_.rtv, kZero, 0, nullptr);
    list->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
    list->OMSetRenderTargets(1, &edges_.rtv, FALSE, &dsv);
    list->OMSetStencilRef(1);
    draw(list, srv_ring, smaa_edges_.Get(), {source}, viewport, c);
    transition(list, edges_, gpu::Use::PixelRead);
    transition(list, weights_, gpu::Use::ColorWrite);
    list->ClearRenderTargetView(weights_.rtv, kZero, 0, nullptr);
    list->OMSetRenderTargets(1, &weights_.rtv, FALSE, &dsv);
    draw(list, srv_ring, smaa_weights_.Get(),
         {edges_.resource.Get(), area_texture_.Get(), search_texture_.Get()},
         viewport, c);
    transition(list, weights_, gpu::Use::PixelRead);
    list->OMSetRenderTargets(1, &color_rtv, FALSE, nullptr);
    draw(list, srv_ring, last_pass, {source, weights_.resource.Get()}, viewport, c);
    return pass;
}

// The last pass of the scene's AA, into the scene target: over MSAA every sample takes the result. Those are made at
// first use.
ID3D12PipelineState* PostProcessor::scene_pass_pipeline(bool smaa, unsigned samples) {
    if (samples <= 1u) {
        return smaa ? smaa_blend_rgb_.Get() : fxaa_rgb_.Get();
    }
    ComPtr<ID3D12PipelineState>& out = scene_pass_ms_[smaa ? 1u : 0u][std::min(std::countr_zero(samples), 3)];
    if (!out) {
        constexpr UINT8 kRgb = D3D12_COLOR_WRITE_ENABLE_RED | D3D12_COLOR_WRITE_ENABLE_GREEN |
                               D3D12_COLOR_WRITE_ENABLE_BLUE;
        const bool made = smaa ? create_pipeline(code(bytes(g_post_smaa_blend_vs)), code(bytes(g_post_smaa_blend_keep_ps)),
                                                 kColorFormat, 0, out, kRgb, samples)
                               : create_pipeline(code(bytes(g_post_fullscreen_vs)), code(bytes(g_post_fxaa_ps)), kColorFormat,
                                                 0, out, kRgb, samples);
        if (!made) {
            return nullptr;
        }
    }
    return out.Get();
}

void PostProcessor::self_test(ID3D12CommandQueue* queue) {
    const auto fail = [&](AaRun pass, const char* why) {
        if (pass != AaRun::kFxaa && status_.smaa_ok) {
            status_.smaa_ok = false;
            set_reason(status_.smaa_reason, why);
        }
        if (pass != AaRun::kSmaa && status_.fxaa_ok) {
            status_.fxaa_ok = false;
            set_reason(status_.fxaa_reason, why);
        }
    };
    if (!status_.smaa_ok && !status_.fxaa_ok) {
        return;
    }
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence> fence;
    DescriptorRing ring;
    if (FAILED(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))) ||
        FAILED(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
                                          IID_PPV_ARGS(&list))) ||
        FAILED(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))) ||
        !ring.initialize(device_, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 16u, 1u)) {
        fail(AaRun::kNone, "TEST SETUP");
        return;
    }
    ring.begin_frame(0);
    std::vector<ComPtr<ID3D12Resource>> keep;
    if (status_.smaa_ok && !record_lookup_upload(list.Get(), keep)) {
        fail(AaRun::kSmaa, "LOOKUP");
    }

    constexpr unsigned kSize = kPostSelfTestSize;
    constexpr unsigned kPitch = 256u;  // kSize * 4 rounded up to the copy alignment
    std::array<unsigned char, kPitch * kSize> pattern{};
    write_post_self_test_pattern(pattern.data(), kPitch);
    std::array<unsigned char, kPitch * kSize> scene_words{};
    rgba8_to_ten_bit(pattern.data(), scene_words.data(), kSize, kSize, kPitch);
    D3D12_HEAP_PROPERTIES def{}, up{}, rb{};
    def.Type = D3D12_HEAP_TYPE_DEFAULT;
    up.Type = D3D12_HEAP_TYPE_UPLOAD;
    rb.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC td{};
    td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    td.Width = kSize;
    td.Height = kSize;
    td.DepthOrArraySize = 1;
    td.MipLevels = 1;
    td.Format = kColorFormat;
    td.SampleDesc.Count = 1;
    D3D12_RESOURCE_DESC bd{};
    bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bd.Width = sizeof(pattern);
    bd.Height = 1;
    bd.DepthOrArraySize = 1;
    bd.MipLevels = 1;
    bd.SampleDesc.Count = 1;
    bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> source, upload, readback;
    unsigned char* mapped = nullptr;
    const D3D12_RANGE none{0, 0};
    if (FAILED(create_texture(device_, &def, D3D12_HEAP_FLAG_NONE, &td, gpu::Use::CopyDest,
                                                nullptr, IID_PPV_ARGS(&source))) ||
        FAILED(device_->CreateCommittedResource(&up, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_GENERIC_READ,
                                                nullptr, IID_PPV_ARGS(&upload))) ||
        FAILED(upload->Map(0, &none, reinterpret_cast<void**>(&mapped)))) {
        fail(AaRun::kNone, "TEST SETUP");
        return;
    }
    std::memcpy(mapped, scene_words.data(), scene_words.size());
    upload->Unmap(0, nullptr);
    bd.Width = 2u * sizeof(pattern);
    if (FAILED(device_->CreateCommittedResource(&rb, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST,
                                                nullptr, IID_PPV_ARGS(&readback)))) {
        fail(AaRun::kNone, "TEST SETUP");
        return;
    }
    D3D12_TEXTURE_COPY_LOCATION from{};
    from.pResource = upload.Get();
    from.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    from.PlacedFootprint.Footprint = {kColorFormat, kSize, kSize, 1, kPitch};
    D3D12_TEXTURE_COPY_LOCATION to{};
    to.pResource = source.Get();
    to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
    BarriersD3D12 uses{list.Get()};
    uses.texture(source.Get(), gpu::Use::CopyDest, gpu::Use::PixelRead);
    uses.issue();
    ring.bind(list.Get());

    const AaRun passes[2] = {AaRun::kSmaa, AaRun::kFxaa};
    bool recorded[2] = {false, false};
    for (unsigned i = 0; i < 2u; ++i) {
        const bool ok = passes[i] == AaRun::kSmaa ? status_.smaa_ok : status_.fxaa_ok;
        if (!ok) continue;
        if (!record_aa(list.Get(), ring, passes[i], source.Get(), kSize, kSize, 0u, 0u)) {
            fail(passes[i], "NO MEMORY");
            continue;
        }
        transition(list.Get(), antialiased_, gpu::Use::CopySource);
        D3D12_TEXTURE_COPY_LOCATION src{};
        src.pResource = antialiased_.resource.Get();
        src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION dst{};
        dst.pResource = readback.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint.Offset = i * sizeof(pattern);
        dst.PlacedFootprint.Footprint = {kColorFormat, kSize, kSize, 1, kPitch};
        list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        transition(list.Get(), antialiased_, gpu::Use::PixelRead);
        recorded[i] = true;
    }
    ring.flush();
    if (FAILED(list->Close())) {
        fail(AaRun::kNone, "TEST SETUP");
        return;
    }
    ID3D12CommandList* lists[] = {list.Get()};
    queue->ExecuteCommandLists(1, lists);
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    const bool done = event != nullptr && SUCCEEDED(queue->Signal(fence.Get(), 1)) &&
        SUCCEEDED(fence->SetEventOnCompletion(1, event)) &&
        WaitForSingleObject(event, 5000) == WAIT_OBJECT_0;
    if (event != nullptr) CloseHandle(event);
    if (!done || device_->GetDeviceRemovedReason() != S_OK) {
        // The GPU may still own these: keep them for the processor's life.
        retired_.push_back({std::move(source), UINT64_MAX});
        retired_.push_back({std::move(upload), UINT64_MAX});
        retired_.push_back({std::move(readback), UINT64_MAX});
        for (auto& r : keep) retired_.push_back({std::move(r), UINT64_MAX});
        retired_.push_back({std::move(edges_.resource), UINT64_MAX});
        retired_.push_back({std::move(weights_.resource), UINT64_MAX});
        retired_.push_back({std::move(antialiased_.resource), UINT64_MAX});
        retired_.push_back({std::move(stencil_.resource), UINT64_MAX});
        edges_ = weights_ = antialiased_ = stencil_ = {};
        fail(AaRun::kNone, done ? "DEVICE LOST" : "GPU TIMEOUT");
        return;
    }
    const D3D12_RANGE all{0, static_cast<SIZE_T>(2u * sizeof(pattern))};
    unsigned char* out = nullptr;
    if (FAILED(readback->Map(0, &all, reinterpret_cast<void**>(&out)))) {
        fail(AaRun::kNone, "TEST SETUP");
        return;
    }
    for (unsigned i = 0; i < 2u; ++i) {
        if (!recorded[i]) continue;
        std::array<unsigned char, kPitch * kSize> result_rgba{};
        ten_bit_to_rgba8(out + i * sizeof(pattern), result_rgba.data(), kSize, kSize, kPitch, kPitch);
        const PostSelfTestResult result = check_post_self_test(pattern.data(), result_rgba.data(), kPitch);
        const bool passed = result.passed;
        std::cerr << "[post-process] self-test " << (passes[i] == AaRun::kSmaa ? "smaa" : "fxaa")
                  << " changed=" << result.changed << " stray=" << result.stray << '\n';
        if (!passed) fail(passes[i], "SELF-TEST");
    }
    readback->Unmap(0, &none);
    // The GPU is idle: drop the 32x32 targets now instead of at the first frame.
    edges_ = weights_ = antialiased_ = stencil_ = {};
}

void PostProcessor::record(ID3D12GraphicsCommandList* list,
                           DescriptorRing& srv_ring,
                           ID3D12Resource* source,
                           unsigned source_width,
                           unsigned source_height,
                           D3D12_CPU_DESCRIPTOR_HANDLE target_rtv,
                           const D3D12_VIEWPORT& viewport,
                           std::uint64_t frame_index,
                           unsigned frames_in_flight,
                           const PostOutput& output) {
    release_retired(frame_index);
    trim_idle_targets(frame_index, frames_in_flight);
    // The last write's pipelines and constants: the 8-bit window's, or HDR10's when the target is (prepare_hdr made them).
    const bool hdr = output.hdr && easu_hdr_ && rcas_hdr_ && rcas_scaled_hdr_ && resample_hdr_ && copy_hdr_;
    ID3D12PipelineState* const easu_last = (hdr ? easu_hdr_ : easu_window_).Get();
    ID3D12PipelineState* const rcas_last = (hdr ? rcas_hdr_ : rcas_).Get();
    ID3D12PipelineState* const rcas_scaled_last = (hdr ? rcas_scaled_hdr_ : rcas_scaled_).Get();
    ID3D12PipelineState* const resample_last = (hdr ? resample_hdr_ : resample_window_).Get();
    ID3D12PipelineState* const copy_last = (hdr ? copy_hdr_ : copy_window_).Get();
    PostOutput last_write = output;
    last_write.hdr = hdr;
    const unsigned target_width = static_cast<unsigned>(viewport.Width);
    const unsigned target_height = static_cast<unsigned>(viewport.Height);
    // The scene was antialiased before the 2D layer (record_scene_aa).
    PostProcessSettings scaling = settings_;
    scaling.aa = PostProcessSettings::Aa::kOff;
    PostProcessPlan plan = plan_post_process(
        source_width, source_height, target_width, target_height, scaling,
        status_.smaa_ok, status_.fxaa_ok);

    ID3D12Resource* image = source;

    list->SetGraphicsRootSignature(root_signature_.Get());
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    const D3D12_VIEWPORT window_sized{
        0.0f, 0.0f, static_cast<float>(target_width), static_cast<float>(target_height), 0.0f, 1.0f};
    Constants c{};
    set_post_input_size(c, source_width, source_height);

    // Enlarging on both axes with sharpening: enlargement and RCAS in one pass straight into the
    // target, without a window-sized intermediate.
    if (plan.resample && plan.rcas && rcas_scaled_last != nullptr && target_width >= source_width &&
        target_height >= source_height) {
        Constants s = c;
        set_fsr_rcas_constants(s, settings_.sharpness);
        s.params[0] = static_cast<float>(source_width);
        s.params[1] = static_cast<float>(source_height);
        s.params[2] = static_cast<float>(target_width);
        s.params[3] = static_cast<float>(target_height);
        list->OMSetRenderTargets(1, &target_rtv, FALSE, nullptr);
        s.origin[0] = static_cast<std::int32_t>(viewport.TopLeftX);
        s.origin[1] = static_cast<std::int32_t>(viewport.TopLeftY);
        set_post_output(s, last_write);
        draw(list, srv_ring, rcas_scaled_last, {image}, viewport, s);
        return;
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
            set_post_output(constants, last_write);
        } else {
            transition(list, scaled_, gpu::Use::ColorWrite);
            list->OMSetRenderTargets(1, &scaled_.rtv, FALSE, nullptr);
            out_viewport = window_sized;
            constants.origin[0] = 0;
            constants.origin[1] = 0;
        }
    };

    if (plan.easu) {
        Constants e = c;
        set_fsr_easu_constants(e, static_cast<float>(source_width), static_cast<float>(source_height),
                               static_cast<float>(target_width), static_cast<float>(target_height));
        D3D12_VIEWPORT vp{};
        into(!plan.rcas, vp, e);
        draw(list, srv_ring, plan.rcas ? easu_.Get() : easu_last, {image}, vp, e);
        if (plan.rcas) {
            transition(list, scaled_, gpu::Use::PixelRead);
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
        draw(list, srv_ring, plan.copy ? copy_last : plan.resample_to_target ? resample_last : resample_.Get(), {image}, vp, r);
        if (!plan.resample_to_target) {
            transition(list, scaled_, gpu::Use::PixelRead);
            image = scaled_.resource.Get();
        }
    }

    if (plan.rcas) {
        Constants s = c;
        set_fsr_rcas_constants(s, settings_.sharpness);
        s.params[0] = static_cast<float>(target_width);
        s.params[1] = static_cast<float>(target_height);
        D3D12_VIEWPORT vp{};
        into(true, vp, s);
        draw(list, srv_ring, rcas_last, {image}, vp, s);
    }
}

}  // namespace galaxy::render
