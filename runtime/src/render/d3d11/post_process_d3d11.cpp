// SPDX-License-Identifier: GPL-3.0-only
// PostProcessor: the Direct3D 11 presentation chain (post_process_d3d11.h), pass for pass the D3D12 one (post_process.cpp).
#include "galaxy/render/d3d11/post_process_d3d11.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <iostream>

// SMAA lookup textures (third_party/smaa, MIT).
#include "AreaTex.h"
#include "SearchTex.h"

// Pass shaders, compiled from post_process.hlsl by fxc at build time (vs_5_0 / ps_5_0: the D3D12 chain's own).
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

namespace galaxy::render::d3d11 {
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

// The scene's pictures (RenderLayout::scene_color): the self-test's, as the scene's.
constexpr DXGI_FORMAT kColorFormat = DXGI_FORMAT_R10G10B10A2_UNORM;
// SMAA's blending weights: one in each channel, the alpha's as fine as the rest.
constexpr DXGI_FORMAT kWeightsFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
constexpr DXGI_FORMAT kEdgesFormat = DXGI_FORMAT_R8G8_UNORM;
// SMAA's edge marks: its stencil alone is used, never its depth, so the smallest format with a stencil.
constexpr DXGI_FORMAT kStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
constexpr std::uint64_t kTargetIdleFrames = 600u;

bool make(ID3D11Device1* device, ShaderBytes code, ComPtr<ID3D11VertexShader>& out) {
    return SUCCEEDED(device->CreateVertexShader(code.data, code.size, nullptr, &out));
}

bool make(ID3D11Device1* device, ShaderBytes code, ComPtr<ID3D11PixelShader>& out) {
    return SUCCEEDED(device->CreatePixelShader(code.data, code.size, nullptr, &out));
}

template <std::size_t N>
void set_reason(std::array<char, N>& out, const char* text) noexcept {
    std::snprintf(out.data(), out.size(), "%s", text);
}

// The frame being recorded: the frame fence's next value (one signal a frame).
std::uint64_t current_frame() noexcept {
    return Context::get().submitted_value() + 1u;
}

D3D11_VIEWPORT whole_viewport(unsigned width, unsigned height) noexcept {
    return {0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f};
}

// Targets and inputs off the pipeline, so the renderer's next use of either binding is not nulled by the runtime.
void unbind(ID3D11DeviceContext1* context) {
    ID3D11ShaderResourceView* const none[3] = {};
    context->PSSetShaderResources(0, 3, none);
    context->OMSetRenderTargets(0, nullptr, nullptr);
}

}  // namespace

bool PostProcessor::create_smaa_shaders(unsigned quality, ComPtr<ID3D11PixelShader>& edges,
                                        ComPtr<ID3D11VertexShader>& weights_vs, ComPtr<ID3D11PixelShader>& weights) {
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
    return make(device_, kEdgesPs[q], edges) && make(device_, kWeightsVs[q], weights_vs) &&
           make(device_, kWeightsPs[q], weights);
}

void PostProcessor::apply_settings(const PostProcessSettings& settings) {
    PostProcessSettings next = settings;
    next.smaa_quality = std::min<std::uint8_t>(next.smaa_quality, 3u);
    if (ready_ && status_.smaa_ok && next.smaa_quality != settings_.smaa_quality) {
        ComPtr<ID3D11PixelShader> edges, weights;
        ComPtr<ID3D11VertexShader> weights_vs;
        if (!create_smaa_shaders(next.smaa_quality, edges, weights_vs, weights)) {
            std::cerr << "[post-process] SMAA quality " << unsigned(next.smaa_quality)
                      << " shaders failed; keeping " << unsigned(settings_.smaa_quality) << '\n';
            next.smaa_quality = settings_.smaa_quality;
        } else {
            // Direct3D 11 keeps the old shaders alive while a frame in flight uses them.
            smaa_edges_ = std::move(edges);
            smaa_weights_vs_ = std::move(weights_vs);
            smaa_weights_ = std::move(weights);
        }
    }
    settings_ = next;
    status_.live_aa = static_cast<std::uint8_t>(settings_.aa);
    status_.live_quality = settings_.smaa_quality;
}

bool PostProcessor::initialize(ID3D11Device1* device, ID3D11DeviceContext1* context,
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
    if (device_ == nullptr || context == nullptr) {
        return false;
    }

    D3D11_BUFFER_DESC cb{};
    cb.ByteWidth = sizeof(PostConstants);
    cb.Usage = D3D11_USAGE_DYNAMIC;
    cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    D3D11_SAMPLER_DESC sd{};
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    sd.MaxAnisotropy = 1;
    D3D11_SAMPLER_DESC point = sd;
    sd.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    point.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    D3D11_BLEND_DESC bd{};
    bd.RenderTarget[0].RenderTargetWriteMask =
        D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE;
    // stencil_mark_ writes 1 where the pixel shader keeps the pixel; stencil_test_ draws only where the stencil is 1.
    D3D11_DEPTH_STENCIL_DESC mark{};
    mark.DepthEnable = FALSE;
    mark.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    mark.DepthFunc = D3D11_COMPARISON_ALWAYS;
    mark.StencilEnable = TRUE;
    mark.StencilReadMask = 0xFF;
    mark.StencilWriteMask = 0xFF;
    mark.FrontFace = {D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_REPLACE, D3D11_COMPARISON_ALWAYS};
    mark.BackFace = mark.FrontFace;
    D3D11_DEPTH_STENCIL_DESC test = mark;
    test.FrontFace = {D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, D3D11_COMPARISON_EQUAL};
    test.BackFace = test.FrontFace;

    // The scaling passes are required; each AA pass is optional and only reported when it cannot be built.
    const bool scaling =
        SUCCEEDED(device_->CreateBuffer(&cb, nullptr, &constants_)) &&
        SUCCEEDED(device_->CreateSamplerState(&sd, &linear_)) &&
        SUCCEEDED(device_->CreateSamplerState(&point, &point_)) &&
        SUCCEEDED(device_->CreateRasterizerState(&rd, &rasterizer_)) &&
        make(device_, bytes(g_post_fullscreen_vs), fullscreen_vs_) &&
        make(device_, bytes(g_post_fsr_easu_ps), easu_) &&
        make(device_, bytes(g_post_fsr_rcas_ps), rcas_) &&
        make(device_, bytes(g_post_fsr_rcas_scaled_ps), rcas_scaled_) &&
        make(device_, bytes(g_post_resample_ps), resample_) &&
        make(device_, bytes(g_post_copy_ps), copy_);
    if (!scaling) {
        std::cerr << "[post-process] pass creation failed\n";
        shutdown();
        return false;
    }
    const bool rgb = SUCCEEDED(device_->CreateBlendState(&bd, &rgb_));
    const bool smaa_passes =
        rgb && SUCCEEDED(device_->CreateDepthStencilState(&mark, &stencil_mark_)) &&
        SUCCEEDED(device_->CreateDepthStencilState(&test, &stencil_test_)) &&
        make(device_, bytes(g_post_smaa_edges_vs), smaa_edges_vs_) &&
        create_smaa_shaders(settings_.smaa_quality, smaa_edges_, smaa_weights_vs_, smaa_weights_) &&
        make(device_, bytes(g_post_smaa_blend_vs), smaa_blend_vs_) &&
        make(device_, bytes(g_post_smaa_blend_ps), smaa_blend_) &&
        make(device_, bytes(g_post_smaa_blend_keep_ps), smaa_blend_keep_);
    const bool fxaa_pass = rgb && make(device_, bytes(g_post_fxaa_ps), fxaa_);

    // The lookup textures are immutable: their bytes go in with the texture, without an upload pass.
    const auto lookup = [&](unsigned width, unsigned height, unsigned pitch, DXGI_FORMAT format, const void* data,
                            ComPtr<ID3D11ShaderResourceView>& out) {
        D3D11_TEXTURE2D_DESC td{};
        td.Width = width;
        td.Height = height;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = format;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_IMMUTABLE;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        const D3D11_SUBRESOURCE_DATA init{data, pitch, 0};
        ComPtr<ID3D11Texture2D> texture;
        return SUCCEEDED(device_->CreateTexture2D(&td, &init, &texture)) &&
               SUCCEEDED(device_->CreateShaderResourceView(texture.Get(), nullptr, &out));
    };
    const bool lookup_textures =
        lookup(AREATEX_WIDTH, AREATEX_HEIGHT, AREATEX_PITCH, DXGI_FORMAT_R8G8_UNORM, areaTexBytes, area_texture_) &&
        lookup(SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT, SEARCHTEX_PITCH, DXGI_FORMAT_R8_UNORM, searchTexBytes,
               search_texture_);
    ready_ = true;

    const AaAvailability available = aa_availability(smaa_passes, lookup_textures, fxaa_pass);
    set_reason(status_.smaa_reason, available.smaa_reason);
    set_reason(status_.fxaa_reason, available.fxaa_reason);
    status_.smaa_ok = available.smaa_ok;
    status_.fxaa_ok = available.fxaa_ok;
    self_test(context);
    status_.initialized = true;
    std::cerr << "[post-process] self-test smaa=" << (status_.smaa_ok ? "pass" : "fail")
              << (status_.smaa_ok ? "" : " smaa-reason=") << status_.smaa_reason.data()
              << " fxaa=" << (status_.fxaa_ok ? "pass" : "fail")
              << (status_.fxaa_ok ? "" : " fxaa-reason=") << status_.fxaa_reason.data() << '\n';
    return true;
}

void PostProcessor::shutdown() noexcept {
    ready_ = false;
    status_.initialized = false;
    status_.smaa_ok = false;
    status_.fxaa_ok = false;
    constants_.Reset();
    linear_.Reset();
    point_.Reset();
    rasterizer_.Reset();
    rgb_.Reset();
    stencil_mark_.Reset();
    stencil_test_.Reset();
    fullscreen_vs_.Reset();
    easu_.Reset();
    rcas_.Reset();
    rcas_scaled_.Reset();
    resample_.Reset();
    copy_.Reset();
    fxaa_.Reset();
    smaa_edges_vs_.Reset();
    smaa_edges_.Reset();
    smaa_weights_vs_.Reset();
    smaa_weights_.Reset();
    smaa_blend_vs_.Reset();
    smaa_blend_.Reset();
    smaa_blend_keep_.Reset();
    area_texture_.Reset();
    search_texture_.Reset();
    edges_ = {};
    weights_ = {};
    stencil_ = {};
    antialiased_ = {};
    scaled_ = {};
    traded_.reset();
    device_ = nullptr;
}

bool PostProcessor::ensure_target(Target& target, unsigned width, unsigned height, DXGI_FORMAT format,
                                  std::uint64_t frame) {
    if (target.texture && target.texture->width == width && target.texture->height == height &&
        target.texture->format == format) {
        target.used = frame;
        return true;
    }
    // The old picture goes with its last reference: Direct3D 11 keeps it while a frame in flight reads it.
    target = {};
    const bool depth = format == kStencilFormat;
    const std::uint32_t bind = depth ? D3D11_BIND_DEPTH_STENCIL : D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    target.texture = Context::get().create_texture(width, height, 1u, format, bind, "post-process target");
    if (!target.texture || (depth ? !target.texture->depth : !target.texture->target || !target.texture->view)) {
        target = {};
        return false;
    }
    target.used = frame;
    return true;
}

void PostProcessor::trim_idle_targets(std::uint64_t frame) noexcept {
    for (Target* target : {&edges_, &weights_, &stencil_, &antialiased_, &scaled_}) {
        if (target->texture && frame > target->used + kTargetIdleFrames) {
            *target = {};
        }
    }
    if (traded_ && frame > traded_frame_ + kTargetIdleFrames) {
        traded_.reset();
    }
}

void PostProcessor::bind_passes(ID3D11DeviceContext1* context) {
    context->IASetInputLayout(nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->RSSetState(rasterizer_.Get());
    context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFFu);
    context->OMSetDepthStencilState(nullptr, 0u);
    ID3D11Buffer* const buffers[1] = {constants_.Get()};
    context->VSSetConstantBuffers(0, 1, buffers);
    context->PSSetConstantBuffers(0, 1, buffers);
    ID3D11SamplerState* const samplers[2] = {linear_.Get(), point_.Get()};
    context->PSSetSamplers(0, 2, samplers);
}

// The pass's target is bound before it: an input that was the last pass's target is off the output by then.
void PostProcessor::draw(ID3D11DeviceContext1* context, ID3D11VertexShader* vs, ID3D11PixelShader* ps,
                         std::initializer_list<ID3D11ShaderResourceView*> inputs, const D3D11_VIEWPORT& viewport,
                         const PostConstants& constants) {
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(constants_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        return;
    }
    std::memcpy(mapped.pData, &constants, sizeof(constants));
    context->Unmap(constants_.Get(), 0);
    std::array<ID3D11ShaderResourceView*, 3> views{};
    std::copy(inputs.begin(), inputs.begin() + std::min<std::size_t>(inputs.size(), views.size()), views.begin());
    context->PSSetShaderResources(0, static_cast<UINT>(views.size()), views.data());
    context->VSSetShader(vs, nullptr, 0);
    context->PSSetShader(ps, nullptr, 0);
    context->RSSetViewports(1, &viewport);
    context->Draw(3, 0);
}

void PostProcessor::run_aa(ID3D11DeviceContext1* context, AaRun pass, ID3D11ShaderResourceView* source,
                           ID3D11RenderTargetView* target, unsigned width, unsigned height, bool whole) {
    bind_passes(context);
    const D3D11_VIEWPORT viewport = whole_viewport(width, height);
    PostConstants c{};
    set_post_input_size(c, width, height);
    if (pass == AaRun::kFxaa) {
        context->OMSetRenderTargets(1, &target, nullptr);
        context->OMSetBlendState(whole ? nullptr : rgb_.Get(), nullptr, 0xFFFFFFFFu);
        draw(context, fullscreen_vs_.Get(), fxaa_.Get(), {source}, viewport, c);
        unbind(context);
        return;
    }
    constexpr float kZero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    ID3D11RenderTargetView* const edges = edges_.texture->target.Get();
    ID3D11RenderTargetView* const weights = weights_.texture->target.Get();
    ID3D11DepthStencilView* const stencil = stencil_.texture->depth.Get();
    context->ClearRenderTargetView(edges, kZero);
    context->ClearDepthStencilView(stencil, D3D11_CLEAR_STENCIL, 1.0f, 0);
    context->OMSetRenderTargets(1, &edges, stencil);
    context->OMSetDepthStencilState(stencil_mark_.Get(), 1u);
    draw(context, smaa_edges_vs_.Get(), smaa_edges_.Get(), {source}, viewport, c);
    context->ClearRenderTargetView(weights, kZero);
    context->OMSetRenderTargets(1, &weights, stencil);
    context->OMSetDepthStencilState(stencil_test_.Get(), 1u);
    draw(context, smaa_weights_vs_.Get(), smaa_weights_.Get(),
         {edges_.texture->view.Get(), area_texture_.Get(), search_texture_.Get()}, viewport, c);
    context->OMSetRenderTargets(1, &target, nullptr);
    context->OMSetDepthStencilState(nullptr, 0u);
    context->OMSetBlendState(whole ? nullptr : rgb_.Get(), nullptr, 0xFFFFFFFFu);
    draw(context, smaa_blend_vs_.Get(), whole ? smaa_blend_.Get() : smaa_blend_keep_.Get(),
         {source, weights_.texture->view.Get()}, viewport, c);
    unbind(context);
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

AaRun PostProcessor::record_scene_aa(ID3D11DeviceContext1* context, const Texture& color,
                                     ID3D11RenderTargetView* target, unsigned width, unsigned height,
                                     unsigned picture_width, unsigned picture_height, unsigned display_width,
                                     unsigned display_height, const SceneTrade& trade) {
    if (!ready_ || target == nullptr || width == 0u || height == 0u) {
        return AaRun::kNone;
    }
    const std::uint64_t frame = current_frame();
    trim_idle_targets(frame);
    const PostProcessPlan plan = plan_post_process(picture_width, picture_height, display_width, display_height,
                                                   settings_, status_.smaa_ok, status_.fxaa_ok);
    AaRun pass = plan.smaa ? AaRun::kSmaa : plan.fxaa ? AaRun::kFxaa : AaRun::kNone;
    // SMAA's targets before the scene's image may change hands: once traded, a pass must run.
    if (pass == AaRun::kSmaa &&
        !(ensure_target(edges_, width, height, kEdgesFormat, frame) &&
          ensure_target(weights_, width, height, kWeightsFormat, frame) &&
          ensure_target(stencil_, width, height, kStencilFormat, frame))) {
        pass = status_.fxaa_ok ? AaRun::kFxaa : AaRun::kNone;
    }
    // The passes read the scene's own image, traded out, and write every pixel and channel of the image that took its
    // place; or they read a copy of the scene and write its colour back into the scene target.
    const bool traded = static_cast<bool>(trade);
    if (pass != AaRun::kNone && !traded && !ensure_target(antialiased_, width, height, color.format, frame)) {
        pass = AaRun::kNone;
    }
    note_aa_run(plan, pass);
    if (pass == AaRun::kNone) {
        return pass;
    }
    ID3D11ShaderResourceView* source = nullptr;
    ID3D11RenderTargetView* out = target;
    if (traded) {
        Traded swapped = trade(std::move(traded_));
        traded_ = std::move(swapped.image);
        traded_frame_ = frame;
        source = traded_->view.Get();
        out = swapped.target;
        // Every pixel and channel is written next: a tiled GPU neither loads nor keeps the old contents.
        context->DiscardView(out);
    } else {
        const D3D11_BOX box{0u, 0u, 0u, width, height, 1u};
        context->CopySubresourceRegion(antialiased_.texture->texture.Get(), 0, 0, 0, 0, color.texture.Get(), 0, &box);
        source = antialiased_.texture->view.Get();
    }
    run_aa(context, pass, source, out, width, height, traded);
    return pass;
}

void PostProcessor::self_test(ID3D11DeviceContext1* context) {
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
    constexpr unsigned kSize = kPostSelfTestSize;
    constexpr unsigned kPitch = kSize * 4u;
    std::array<unsigned char, kPitch * kSize> pattern{};
    write_post_self_test_pattern(pattern.data(), kPitch);
    std::array<unsigned char, kPitch * kSize> scene_words{};
    rgba8_to_ten_bit(pattern.data(), scene_words.data(), kSize, kSize, kPitch);

    // The pattern as a scene picture, the passes' whole-target result (the traded path's last write) and its readback.
    D3D11_TEXTURE2D_DESC td{};
    td.Width = kSize;
    td.Height = kSize;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = kColorFormat;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    const D3D11_SUBRESOURCE_DATA init{scene_words.data(), kPitch, 0};
    D3D11_TEXTURE2D_DESC rd = td;
    rd.Usage = D3D11_USAGE_DEFAULT;
    rd.BindFlags = D3D11_BIND_RENDER_TARGET;
    D3D11_TEXTURE2D_DESC sd = td;
    sd.Usage = D3D11_USAGE_STAGING;
    sd.BindFlags = 0;
    sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> source, result, readback;
    ComPtr<ID3D11ShaderResourceView> source_view;
    ComPtr<ID3D11RenderTargetView> result_target;
    if (FAILED(device_->CreateTexture2D(&td, &init, &source)) ||
        FAILED(device_->CreateShaderResourceView(source.Get(), nullptr, &source_view)) ||
        FAILED(device_->CreateTexture2D(&rd, nullptr, &result)) ||
        FAILED(device_->CreateRenderTargetView(result.Get(), nullptr, &result_target)) ||
        FAILED(device_->CreateTexture2D(&sd, nullptr, &readback))) {
        fail(AaRun::kNone, "TEST SETUP");
        return;
    }
    if (status_.smaa_ok &&
        !(ensure_target(edges_, kSize, kSize, kEdgesFormat, 0u) && ensure_target(weights_, kSize, kSize, kWeightsFormat, 0u) &&
          ensure_target(stencil_, kSize, kSize, kStencilFormat, 0u))) {
        fail(AaRun::kSmaa, "NO MEMORY");
    }

    const AaRun passes[2] = {AaRun::kSmaa, AaRun::kFxaa};
    for (const AaRun pass : passes) {
        if (!(pass == AaRun::kSmaa ? status_.smaa_ok : status_.fxaa_ok)) {
            continue;
        }
        run_aa(context, pass, source_view.Get(), result_target.Get(), kSize, kSize, true);
        context->CopyResource(readback.Get(), result.Get());
        // Map waits for the GPU; a lost device fails it.
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
            fail(AaRun::kNone, device_->GetDeviceRemovedReason() != S_OK ? "DEVICE LOST" : "TEST SETUP");
            break;
        }
        std::array<unsigned char, kPitch * kSize> result_rgba{};
        ten_bit_to_rgba8(static_cast<const unsigned char*>(mapped.pData), result_rgba.data(), kSize, kSize,
                         mapped.RowPitch, kPitch);
        context->Unmap(readback.Get(), 0);
        const PostSelfTestResult checked = check_post_self_test(pattern.data(), result_rgba.data(), kPitch);
        std::cerr << "[post-process] self-test " << (pass == AaRun::kSmaa ? "smaa" : "fxaa")
                  << " changed=" << checked.changed << " stray=" << checked.stray << '\n';
        if (!checked.passed) {
            fail(pass, "SELF-TEST");
        }
    }
    // Drop the 32x32 targets now instead of at the first frame.
    edges_ = weights_ = stencil_ = {};
}

void PostProcessor::record(ID3D11DeviceContext1* context, const Texture& source, unsigned source_width,
                           unsigned source_height, ID3D11RenderTargetView* target, const D3D11_VIEWPORT& viewport,
                           const PostOutput& output) {
    if (!ready_ || target == nullptr) {
        return;
    }
    const std::uint64_t frame = current_frame();
    trim_idle_targets(frame);
    const unsigned target_width = static_cast<unsigned>(viewport.Width);
    const unsigned target_height = static_cast<unsigned>(viewport.Height);
    // The scene was antialiased before the 2D layer (record_scene_aa).
    PostProcessSettings scaling = settings_;
    scaling.aa = PostProcessSettings::Aa::kOff;
    PostProcessPlan plan = plan_post_process(source_width, source_height, target_width, target_height, scaling,
                                             status_.smaa_ok, status_.fxaa_ok);

    ID3D11ShaderResourceView* image = source.view.Get();
    bind_passes(context);
    const D3D11_VIEWPORT window_sized = whole_viewport(target_width, target_height);
    PostConstants c{};
    set_post_input_size(c, source_width, source_height);

    // Enlarging on both axes with sharpening: enlargement and RCAS in one pass straight into the target, without a
    // window-sized intermediate.
    if (plan.resample && plan.rcas && target_width >= source_width && target_height >= source_height) {
        PostConstants s = c;
        set_fsr_rcas_constants(s, settings_.sharpness);
        s.params[0] = static_cast<float>(source_width);
        s.params[1] = static_cast<float>(source_height);
        s.params[2] = static_cast<float>(target_width);
        s.params[3] = static_cast<float>(target_height);
        s.origin[0] = static_cast<std::int32_t>(viewport.TopLeftX);
        s.origin[1] = static_cast<std::int32_t>(viewport.TopLeftY);
        set_post_output(s, output);
        context->OMSetRenderTargets(1, &target, nullptr);
        draw(context, fullscreen_vs_.Get(), rcas_scaled_.Get(), {image}, viewport, s);
        unbind(context);
        return;
    }

    // Window-sized intermediate for a scaling step that RCAS then reads.
    const bool needs_scaled = (plan.easu || plan.resample) && plan.rcas;
    if (needs_scaled && !ensure_target(scaled_, target_width, target_height, source.format, frame)) {
        plan.rcas = false;
        plan.resample = !plan.easu;
        plan.resample_to_target = plan.resample;
    }

    const auto into = [&](bool to_target, D3D11_VIEWPORT& out_viewport, PostConstants& constants) {
        if (to_target) {
            context->OMSetRenderTargets(1, &target, nullptr);
            out_viewport = viewport;
            constants.origin[0] = static_cast<std::int32_t>(viewport.TopLeftX);
            constants.origin[1] = static_cast<std::int32_t>(viewport.TopLeftY);
            set_post_output(constants, output);
        } else {
            ID3D11RenderTargetView* const scaled = scaled_.texture->target.Get();
            // Written whole next: a tiled GPU neither loads nor keeps the old contents.
            context->DiscardView(scaled);
            context->OMSetRenderTargets(1, &scaled, nullptr);
            out_viewport = window_sized;
            constants.origin[0] = 0;
            constants.origin[1] = 0;
        }
    };

    if (plan.easu) {
        PostConstants e = c;
        set_fsr_easu_constants(e, static_cast<float>(source_width), static_cast<float>(source_height),
                               static_cast<float>(target_width), static_cast<float>(target_height));
        D3D11_VIEWPORT vp{};
        into(!plan.rcas, vp, e);
        draw(context, fullscreen_vs_.Get(), easu_.Get(), {image}, vp, e);
        if (plan.rcas) {
            image = scaled_.texture->view.Get();
        }
    } else if (plan.resample) {
        PostConstants r = c;
        r.params[0] = static_cast<float>(source_width);
        r.params[1] = static_cast<float>(source_height);
        r.params[2] = static_cast<float>(target_width);
        r.params[3] = static_cast<float>(target_height);
        D3D11_VIEWPORT vp{};
        into(plan.resample_to_target, vp, r);
        draw(context, fullscreen_vs_.Get(), plan.copy ? copy_.Get() : resample_.Get(), {image}, vp, r);
        if (!plan.resample_to_target) {
            image = scaled_.texture->view.Get();
        }
    }

    if (plan.rcas) {
        PostConstants s = c;
        set_fsr_rcas_constants(s, settings_.sharpness);
        s.params[0] = static_cast<float>(target_width);
        s.params[1] = static_cast<float>(target_height);
        D3D11_VIEWPORT vp{};
        into(true, vp, s);
        draw(context, fullscreen_vs_.Get(), rcas_.Get(), {image}, vp, s);
    }
    unbind(context);
}

}  // namespace galaxy::render::d3d11
