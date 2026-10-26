// SPDX-License-Identifier: GPL-3.0-only
// GameBloom: the game's bloom at the internal resolution (game_bloom.h).
#include "galaxy/gx/game_bloom.h"

#include "galaxy/gx/game_bloom_shape.h"
#include "galaxy/gx/renderer_d3d12.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <iostream>
#include <utility>

#include "bloom_blur_cs.h"
#include "bloom_box_cs.h"
#include "bloom_composite_ps.h"
#include "bloom_composite_vs.h"
#include "bloom_down_cs.h"

namespace galaxy::gx {
namespace {

using Constants = GameBloom::Constants;
static_assert(sizeof(Constants) == 64u);

constexpr DXGI_FORMAT kWorkFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
// The state the chain's textures are read in (its compute passes and the composite).
constexpr D3D12_RESOURCE_STATES kRead =
    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

D3D12_SHADER_BYTECODE code(const void* bytes, std::size_t size) {
    return {bytes, size};
}

}  // namespace

bool GameBloom::initialize(ID3D12Device* device) {
    shutdown();
    device_ = device;
    if (device_ == nullptr) {
        return false;
    }
    D3D12_DESCRIPTOR_RANGE ranges[2]{};
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[0].NumDescriptors = 3;
    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    ranges[1].NumDescriptors = 3;
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
    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
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
        std::cerr << "[bloom] root signature failed\n";
        return false;
    }
    D3D12_COMPUTE_PIPELINE_STATE_DESC cd{};
    cd.pRootSignature = root_signature_.Get();
    const struct {
        Microsoft::WRL::ComPtr<ID3D12PipelineState>* pipeline;
        D3D12_SHADER_BYTECODE shader;
    } passes[] = {
        {&down_, code(g_bloom_down_cs, sizeof(g_bloom_down_cs))},
        {&box_, code(g_bloom_box_cs, sizeof(g_bloom_box_cs))},
        {&blur_, code(g_bloom_blur_cs, sizeof(g_bloom_blur_cs))},
    };
    for (const auto& pass : passes) {
        cd.CS = pass.shader;
        if (FAILED(device_->CreateComputePipelineState(&cd, IID_PPV_ARGS(&*pass.pipeline)))) {
            std::cerr << "[bloom] compute pipeline failed\n";
            return false;
        }
    }
    ready_ = true;
    return true;
}

void GameBloom::shutdown() noexcept {
    ready_ = false;
    kept_ = false;
    down_.Reset();
    box_.Reset();
    blur_.Reset();
    for (auto& pipeline : composite_) pipeline.Reset();
    root_signature_.Reset();
    for (Target* target : {&keep_, &source_, &work_, &box_tmp_, &mask_, &level1_, &level2_in_, &level2_}) {
        *target = {};
    }
    for (Target& target : blur_h_) target = {};
    for (Target& target : blur2_h_) target = {};
    retired_.clear();
    device_ = nullptr;
}

// The kept scene plus the bloom over the frame (RGB; the game's alpha stays), no depth.
ID3D12PipelineState* GameBloom::composite_pipeline(unsigned samples, bool hdr) {
    const unsigned index =
        std::min(static_cast<unsigned>(std::countr_zero(std::max(samples, 1u))), 3u) + (hdr ? 4u : 0u);
    if (composite_[index]) {
        return composite_[index].Get();
    }
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
    pd.pRootSignature = root_signature_.Get();
    pd.VS = code(g_bloom_composite_vs, sizeof(g_bloom_composite_vs));
    pd.PS = code(g_bloom_composite_ps, sizeof(g_bloom_composite_ps));
    D3D12_RENDER_TARGET_BLEND_DESC& blend = pd.BlendState.RenderTarget[0];
    blend.BlendEnable = FALSE;
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
    if (FAILED(device_->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&composite_[index])))) {
        std::cerr << "[bloom] composite pipeline failed for " << (1u << (index & 3u)) << " samples\n";
        return nullptr;
    }
    return composite_[index].Get();
}

bool GameBloom::ensure_target(Target& target, DXGI_FORMAT format, unsigned width, unsigned height, bool unordered,
                              std::uint64_t frame_index, unsigned frames_in_flight, const wchar_t* name) {
    if (target.resource && target.width == width && target.height == height && target.format == format) {
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
    rd.Flags = unordered ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;
    const D3D12_RESOURCE_STATES state = unordered ? kRead : D3D12_RESOURCE_STATE_COPY_DEST;
    if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &rd, state, nullptr,
                                                IID_PPV_ARGS(&target.resource)))) {
        return false;
    }
    target.resource->SetName(name);
    target.width = width;
    target.height = height;
    target.format = format;
    target.state = state;
    return true;
}

void GameBloom::transition(ID3D12GraphicsCommandList* list, Target& target, D3D12_RESOURCE_STATES state) {
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

// The frame's pixels of the EFB, in its own format, into `target` (the normal bloom's kept scene, the simple bloom's
// source).
bool GameBloom::copy_frame(ID3D12GraphicsCommandList* list, const Inputs& in, Target& target, const wchar_t* name,
                           std::uint64_t frame_index, unsigned frames_in_flight) {
    const DXGI_FORMAT format = in.hdr ? kEfbHdrColorFormat : kEfbColorFormat;
    if (!ensure_target(target, format, in.frame_width, in.frame_height, false, frame_index, frames_in_flight, name)) {
        return false;
    }
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = in.efb;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    list->ResourceBarrier(1, &barrier);
    transition(list, target, D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_TEXTURE_COPY_LOCATION to{};
    to.pResource = target.resource.Get();
    to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION from = to;
    from.pResource = in.efb;
    const D3D12_BOX frame{0u, 0u, 0u, in.frame_width, in.frame_height, 1u};
    list->CopyTextureRegion(&to, 0u, 0u, 0u, &from, &frame);
    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    list->ResourceBarrier(1, &barrier);
    transition(list, target, kRead);
    return true;
}

void GameBloom::retire(std::uint64_t frame_index) {
    retired_.erase(std::remove_if(retired_.begin(), retired_.end(),
                                  [&](const Retired& r) { return frame_index > r.release_after_frame; }),
                   retired_.end());
}

bool GameBloom::keep(ID3D12GraphicsCommandList* list, const Inputs& in, std::uint64_t frame_index,
                     unsigned frames_in_flight) {
    retire(frame_index);
    kept_ = ready_ && in.efb != nullptr && in.frame_width != 0u && in.frame_height != 0u &&
            copy_frame(list, in, keep_, L"bloom kept scene", frame_index, frames_in_flight);
    return kept_;
}

bool GameBloom::apply(ID3D12GraphicsCommandList* list, DescriptorRing& ring, const Inputs& in, const Params& p,
                      std::uint64_t frame_index, unsigned frames_in_flight) {
    retire(frame_index);
    const bool normal = p.kind == Kind::Normal;
    const bool kept = std::exchange(kept_, false);
    if (!ready_ || in.efb == nullptr || in.frame_width == 0u || in.frame_height == 0u || (normal && !kept)) {
        return false;
    }
    ID3D12PipelineState* const composite = composite_pipeline(in.samples, in.hdr);
    const bloom::Shape s = bloom::shape_of(in.frame_width, in.frame_height, in.logical_height, in.scale);
    const unsigned blurs1 = normal ? 2u : 1u;
    bool made = composite != nullptr &&
                ensure_target(work_, kWorkFormat, s.w1, s.h1, true, frame_index, frames_in_flight, L"bloom work") &&
                ensure_target(box_tmp_, kWorkFormat, s.w1, s.h1, true, frame_index, frames_in_flight,
                              L"bloom box") &&
                ensure_target(mask_, kWorkFormat, s.w1, s.h1, true, frame_index, frames_in_flight, L"bloom mask") &&
                ensure_target(level1_, kWorkFormat, s.w1, s.h1, true, frame_index, frames_in_flight,
                              L"bloom level 1");
    for (unsigned i = 0; made && i < blurs1; ++i) {
        made = ensure_target(blur_h_[i], kWorkFormat, s.w1, s.h1, true, frame_index, frames_in_flight,
                             L"bloom level 1 blur");
    }
    if (made && normal) {
        made = ensure_target(level2_in_, kWorkFormat, s.w2, s.h2, true, frame_index, frames_in_flight,
                             L"bloom level 2 source") &&
               ensure_target(level2_, kWorkFormat, s.w2, s.h2, true, frame_index, frames_in_flight,
                             L"bloom level 2");
        for (unsigned i = 0; made && i < 3u; ++i) {
            made = ensure_target(blur2_h_[i], kWorkFormat, s.w2, s.h2, true, frame_index, frames_in_flight,
                                 L"bloom level 2 blur");
        }
    }
    if (!made || (!normal && !copy_frame(list, in, source_, L"bloom source", frame_index, frames_in_flight))) {
        return false;
    }

    // The Gaussians standing for the rings (game_bloom_shape.h).
    const bloom::Widths widths = bloom::widths_of(normal, s, in.frame_width, in.frame_height, in.logical_width,
                                                  in.logical_height, p.ring_x, p.ring_y);
    const float* const sigma1x = widths.x1;
    const float* const sigma1y = widths.y1;
    const float* const sigma2x = widths.x2;
    const float* const sigma2y = widths.y2;
    // GALAXY_BLOOM_LOG=1: each kind's shape and widths once; 2: every 600th pass of each kind (which stages bloom).
    static const unsigned log = [] {
        char value[8]{};
        std::size_t length = 0;
        return getenv_s(&length, value, sizeof(value), "GALAXY_BLOOM_LOG") == 0 && length > 1
                   ? static_cast<unsigned>(value[0] - '0')
                   : 0u;
    }();
    static std::uint64_t passes[3]{};
    if (log != 0u && (log == 1u ? passes[static_cast<unsigned>(p.kind)]++ == 0u
                                : passes[static_cast<unsigned>(p.kind)]++ % 600u == 0u)) {
        std::cerr << "[bloom] " << (normal ? "normal" : "simple") << " level 1 " << s.w1 << 'x' << s.h1 << " ("
                  << s.f << " px a texel, footprint " << s.box << "), level 2 " << s.w2 << 'x' << s.h2
                  << "; sigma 1 " << sigma1x[0] << '/' << sigma1y[0] << ' ' << sigma1x[1] << '/' << sigma1y[1]
                  << "; sigma 2 " << sigma2x[0] << '/' << sigma2y[0] << ' ' << sigma2x[1] << '/' << sigma2y[1] << ' '
                  << sigma2x[2] << '/' << sigma2y[2] << "; threshold " << p.threshold << " taps " << p.tap1 << '/'
                  << p.tap2 << " intensity " << p.intensity << " ring " << p.ring_x << '/' << p.ring_y << " frame "
                  << frame_index << '\n';
    }

    const UINT increment = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    const auto table = [&](const std::array<ID3D12Resource*, 3>& srvs, const std::array<DXGI_FORMAT, 3>& formats,
                           const std::array<ID3D12Resource*, 3>& uavs) { return this->table(ring, srvs, formats, uavs); };
    const DXGI_FORMAT efb_format = in.hdr ? kEfbHdrColorFormat : kEfbColorFormat;
    const auto dispatch = [&](ID3D12PipelineState* pipeline, const Constants& c, D3D12_GPU_DESCRIPTOR_HANDLE gpu,
                              unsigned width, unsigned height) {
        D3D12_GPU_DESCRIPTOR_HANDLE uavs = gpu;
        uavs.ptr += 3u * static_cast<UINT64>(increment);
        list->SetPipelineState(pipeline);
        list->SetComputeRoot32BitConstants(0, sizeof(Constants) / 4u, &c, 0);
        list->SetComputeRootDescriptorTable(1, gpu);
        list->SetComputeRootDescriptorTable(2, uavs);
        list->Dispatch((width + 7u) / 8u, (height + 7u) / 8u, 1u);
    };
    list->SetComputeRootSignature(root_signature_.Get());

    // The selection's source: the glowing models drawn over black (normal), the scene (simple).
    D3D12_RESOURCE_BARRIER efb_barrier{};
    efb_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    efb_barrier.Transition.pResource = in.efb;
    efb_barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    efb_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    efb_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    if (normal) {
        list->ResourceBarrier(1, &efb_barrier);
    }
    {
        Constants c{};
        c.size[0] = s.w1;
        c.size[1] = s.h1;
        c.size[2] = s.f;
        c.uv[0] = static_cast<float>(in.frame_width);
        c.uv[1] = static_cast<float>(in.frame_height);
        transition(list, work_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        dispatch(down_.Get(), c,
                 table({normal ? in.efb : source_.resource.Get(), nullptr, nullptr}, {efb_format, {}, {}},
                       {work_.resource.Get(), nullptr, nullptr}),
                 s.w1, s.h1);
        transition(list, work_, kRead);
    }
    if (normal) {
        std::swap(efb_barrier.Transition.StateBefore, efb_barrier.Transition.StateAfter);
        list->ResourceBarrier(1, &efb_barrier);
    }
    // The game's 4 x 4 pixel footprint (across, then down with the selection).
    const bool across = s.box > 1.0f;
    if (across) {
        Constants c{};
        c.size[0] = s.w1;
        c.size[1] = s.h1;
        c.size[3] = 0u;
        c.params[0] = s.box;
        c.params[1] = -1.0f;
        transition(list, box_tmp_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        dispatch(box_.Get(), c,
                 table({work_.resource.Get(), nullptr, nullptr}, {kWorkFormat, {}, {}},
                       {box_tmp_.resource.Get(), nullptr, nullptr}),
                 s.w1, s.h1);
        transition(list, box_tmp_, kRead);
    }
    {
        Constants c{};
        c.size[0] = s.w1;
        c.size[1] = s.h1;
        c.size[3] = 1u;
        c.params[0] = s.box;
        c.params[1] = static_cast<float>(std::min(p.threshold, 255u));
        c.params[2] = static_cast<float>(normal ? 0u : std::min(p.channel, 3u));
        transition(list, mask_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        dispatch(box_.Get(), c,
                 table({(across ? box_tmp_ : work_).resource.Get(), nullptr, nullptr}, {kWorkFormat, {}, {}},
                       {mask_.resource.Get(), nullptr, nullptr}),
                 s.w1, s.h1);
        transition(list, mask_, kRead);
    }
    // Level 1: the rings' Gaussians across, then down, summed at the taps' weight (normal: clamped, as the EFB holds
    // it).
    {
        Constants c{};
        c.size[0] = s.w1;
        c.size[1] = s.h1;
        c.size[2] = blurs1;
        for (unsigned i = 0; i < 3u; ++i) c.sigma[i] = sigma1x[i];
        for (unsigned i = 0; i < blurs1; ++i) transition(list, blur_h_[i], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        dispatch(blur_.Get(), c,
                 table({mask_.resource.Get(), nullptr, nullptr}, {kWorkFormat, {}, {}},
                       {blur_h_[0].resource.Get(), blurs1 > 1u ? blur_h_[1].resource.Get() : nullptr, nullptr}),
                 s.w1, s.h1);
        for (unsigned i = 0; i < blurs1; ++i) transition(list, blur_h_[i], kRead);
        c.size[3] = 1u;
        for (unsigned i = 0; i < 3u; ++i) c.sigma[i] = sigma1y[i];
        c.params[0] =
            (normal ? bloom::kLevel1Taps : bloom::kSimpleTaps) * static_cast<float>(std::min(p.tap1, 255u)) / 255.0f;
        c.params[1] = normal ? 1.0f : 0.0f;
        transition(list, level1_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        dispatch(blur_.Get(), c,
                 table({blur_h_[0].resource.Get(), blurs1 > 1u ? blur_h_[1].resource.Get() : nullptr, nullptr},
                       {kWorkFormat, kWorkFormat, {}}, {level1_.resource.Get(), nullptr, nullptr}),
                 s.w1, s.h1);
        transition(list, level1_, kRead);
    }
    // Level 2 (normal): level 1 in n x n boxes, its three rings' Gaussians.
    if (normal) {
        Constants c{};
        c.size[0] = s.w2;
        c.size[1] = s.h2;
        c.size[2] = s.n;
        c.uv[0] = static_cast<float>(s.w1);
        c.uv[1] = static_cast<float>(s.h1);
        transition(list, level2_in_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        dispatch(down_.Get(), c,
                 table({level1_.resource.Get(), nullptr, nullptr}, {kWorkFormat, {}, {}},
                       {level2_in_.resource.Get(), nullptr, nullptr}),
                 s.w2, s.h2);
        transition(list, level2_in_, kRead);
        c = {};
        c.size[0] = s.w2;
        c.size[1] = s.h2;
        c.size[2] = 3u;
        for (unsigned i = 0; i < 3u; ++i) c.sigma[i] = sigma2x[i];
        for (Target& t : blur2_h_) transition(list, t, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        dispatch(blur_.Get(), c,
                 table({level2_in_.resource.Get(), nullptr, nullptr}, {kWorkFormat, {}, {}},
                       {blur2_h_[0].resource.Get(), blur2_h_[1].resource.Get(), blur2_h_[2].resource.Get()}),
                 s.w2, s.h2);
        for (Target& t : blur2_h_) transition(list, t, kRead);
        c.size[3] = 1u;
        for (unsigned i = 0; i < 3u; ++i) c.sigma[i] = sigma2y[i];
        c.params[0] = bloom::kLevel2Taps * static_cast<float>(std::min(p.tap2, 255u)) / 255.0f;
        c.params[1] = 1.0f;
        transition(list, level2_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        dispatch(blur_.Get(), c,
                 table({blur2_h_[0].resource.Get(), blur2_h_[1].resource.Get(), blur2_h_[2].resource.Get()},
                       {kWorkFormat, kWorkFormat, kWorkFormat}, {level2_.resource.Get(), nullptr, nullptr}),
                 s.w2, s.h2);
        transition(list, level2_, kRead);
    }

    // The kept scene plus the bloom, over the frame.
    Constants c{};
    c.params[0] = static_cast<float>(std::min(p.intensity, 255u)) / 255.0f;
    c.params[1] = std::max(p.strength, 0.0f);
    c.params[2] = normal ? 1.0f : 2.0f;
    c.uv[0] = 1.0f / static_cast<float>(s.f * s.w1);
    c.uv[1] = 1.0f / static_cast<float>(s.f * s.h1);
    c.uv[2] = 1.0f / static_cast<float>(s.f * s.n * std::max(s.w2, 1u));
    c.uv[3] = 1.0f / static_cast<float>(s.f * s.n * std::max(s.h2, 1u));
    draw_composite(list, ring, in, composite, normal ? keep_ : source_, level1_, normal ? level2_ : level1_, c);
    return true;
}

bool GameBloom::restore(ID3D12GraphicsCommandList* list, DescriptorRing& ring, const Inputs& in,
                        std::uint64_t frame_index, unsigned frames_in_flight) {
    static_cast<void>(frames_in_flight);
    retire(frame_index);
    const bool kept = std::exchange(kept_, false);
    if (!ready_ || !kept || in.frame_width == 0u || in.frame_height == 0u) {
        return false;
    }
    ID3D12PipelineState* const composite = composite_pipeline(in.samples, in.hdr);
    if (composite == nullptr) {
        return false;
    }
    // Kind 0: the scene alone, the levels unread.
    draw_composite(list, ring, in, composite, keep_, keep_, keep_, Constants{});
    return true;
}

// One descriptor table a pass: three SRVs (t0-t2), three UAVs (u0-u2); unused slots null.
D3D12_GPU_DESCRIPTOR_HANDLE GameBloom::table(DescriptorRing& ring, const std::array<ID3D12Resource*, 3>& srvs,
                                             const std::array<DXGI_FORMAT, 3>& formats,
                                             const std::array<ID3D12Resource*, 3>& uavs) {
    const UINT increment = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    const DescriptorRing::Table t = ring.allocate(6u);
    for (unsigned i = 0; i < 6u; ++i) {
        D3D12_CPU_DESCRIPTOR_HANDLE h = t.cpu;
        h.ptr += static_cast<SIZE_T>(i) * increment;
        if (i < 3u) {
            D3D12_SHADER_RESOURCE_VIEW_DESC view{};
            view.Format = srvs[i] != nullptr ? formats[i] : kWorkFormat;
            view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            view.Texture2D.MipLevels = 1;
            device_->CreateShaderResourceView(srvs[i], &view, h);
        } else {
            D3D12_UNORDERED_ACCESS_VIEW_DESC view{};
            view.Format = kWorkFormat;
            view.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            device_->CreateUnorderedAccessView(uavs[i - 3u], nullptr, &view, h);
        }
    }
    return t.gpu;
}

// The scene (t0) plus the bloom from the levels (t1, t2) over the frame, into the EFB's render target.
void GameBloom::draw_composite(ID3D12GraphicsCommandList* list, DescriptorRing& ring, const Inputs& in,
                               ID3D12PipelineState* pipeline, const Target& scene, const Target& first,
                               const Target& second, Constants c) {
    c.params[3] = in.hdr ? 0.0f : 1.0f;  // 8 bits: the bloom rounded stochastically
    const D3D12_GPU_DESCRIPTOR_HANDLE gpu =
        table(ring, {scene.resource.Get(), first.resource.Get(), second.resource.Get()},
              {scene.format, first.format, second.format}, {nullptr, nullptr, nullptr});
    list->SetGraphicsRootSignature(root_signature_.Get());
    list->SetGraphicsRoot32BitConstants(0, sizeof(Constants) / 4u, &c, 0);
    list->SetGraphicsRootDescriptorTable(1, gpu);
    list->SetPipelineState(pipeline);
    list->OMSetRenderTargets(1, &in.rtv, FALSE, nullptr);
    const D3D12_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(in.frame_width), static_cast<float>(in.frame_height),
                                  0.0f, 1.0f};
    const D3D12_RECT scissor{0, 0, static_cast<LONG>(in.frame_width), static_cast<LONG>(in.frame_height)};
    list->RSSetViewports(1, &viewport);
    list->RSSetScissorRects(1, &scissor);
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list->DrawInstanced(3, 1, 0, 0);
}

}  // namespace galaxy::gx
