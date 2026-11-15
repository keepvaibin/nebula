// SPDX-License-Identifier: GPL-3.0-only
// The game's bloom on Direct3D 11 (d3d11/game_bloom_d3d11.h), pass for pass game_bloom.cpp's.
#include "galaxy/render/d3d11/game_bloom_d3d11.h"

#include "galaxy/render/game_bloom_shape.h"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <utility>

#include "bloom_blur_cs.h"
#include "bloom_box_cs.h"
#include "bloom_composite_ps.h"
#include "bloom_composite_vs.h"
#include "bloom_down_cs.h"

namespace galaxy::render::d3d11 {
namespace {

using Constants = GameBloom::Constants;
static_assert(sizeof(Constants) == 64u);

constexpr DXGI_FORMAT kWorkFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
constexpr std::uint32_t kWorkBind = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
// A kept scene or source: sampled, and the scene target's next texture after a trade.
constexpr std::uint32_t kPictureBind = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
constexpr std::uint64_t kIdleFrames = 600u;

ID3D11ShaderResourceView* srv_of(const Texture* texture) noexcept {
    return texture != nullptr ? texture->view.Get() : nullptr;
}

ID3D11UnorderedAccessView* uav_of(const Texture* texture) noexcept {
    return texture != nullptr ? texture->storage.Get() : nullptr;
}

// A texture the scene target can continue on: its size and format, drawn into and sampled, one sample.
bool fits_scene(const Texture* texture, const GameBloom::Inputs& in) noexcept {
    return texture != nullptr && in.scene_target != nullptr && texture->width == in.frame_width &&
           texture->height == in.frame_height && texture->format == in.scene_target->format &&
           texture->samples == 1u && texture->target && texture->view;
}

}  // namespace

bool GameBloom::initialize(ID3D11Device1* device) {
    shutdown();
    if (device == nullptr) {
        return false;
    }
    D3D11_BUFFER_DESC cb{};
    cb.ByteWidth = sizeof(Constants);
    cb.Usage = D3D11_USAGE_DYNAMIC;
    cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    D3D11_SAMPLER_DESC sampler{};
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampler.MaxLOD = D3D11_FLOAT32_MAX;
    D3D11_RASTERIZER_DESC raster{};
    raster.FillMode = D3D11_FILL_SOLID;
    raster.CullMode = D3D11_CULL_NONE;
    raster.DepthClipEnable = TRUE;
    D3D11_DEPTH_STENCIL_DESC depth{};
    depth.DepthEnable = FALSE;
    depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    depth.DepthFunc = D3D11_COMPARISON_ALWAYS;
    if (FAILED(device->CreateComputeShader(g_bloom_down_cs, sizeof(g_bloom_down_cs), nullptr, &down_)) ||
        FAILED(device->CreateComputeShader(g_bloom_box_cs, sizeof(g_bloom_box_cs), nullptr, &box_)) ||
        FAILED(device->CreateComputeShader(g_bloom_blur_cs, sizeof(g_bloom_blur_cs), nullptr, &blur_)) ||
        FAILED(device->CreateVertexShader(g_bloom_composite_vs, sizeof(g_bloom_composite_vs), nullptr, &composite_vs_)) ||
        FAILED(device->CreatePixelShader(g_bloom_composite_ps, sizeof(g_bloom_composite_ps), nullptr, &composite_ps_)) ||
        FAILED(device->CreateBuffer(&cb, nullptr, &constants_)) ||
        FAILED(device->CreateSamplerState(&sampler, &linear_)) ||
        FAILED(device->CreateRasterizerState(&raster, &raster_)) ||
        FAILED(device->CreateDepthStencilState(&depth, &no_depth_))) {
        std::cerr << "[bloom] Direct3D 11 shaders or states failed\n";
        shutdown();
        return false;
    }
    ready_ = true;
    return true;
}

void GameBloom::shutdown() noexcept {
    ready_ = false;
    kept_ = false;
    source_given_ = false;
    down_.Reset();
    box_.Reset();
    blur_.Reset();
    composite_vs_.Reset();
    composite_ps_.Reset();
    constants_.Reset();
    linear_.Reset();
    raster_.Reset();
    no_depth_.Reset();
    for (Target* target : {&keep_, &source_, &work_, &box_tmp_, &mask_, &level1_, &level2_in_, &level2_}) {
        *target = {};
    }
    for (Target& target : blur_h_) target = {};
    for (Target& target : blur2_h_) target = {};
}

bool GameBloom::ensure_target(Target& target, DXGI_FORMAT format, unsigned width, unsigned height, std::uint32_t bind,
                              const char* name) {
    target.used = clock_;
    if (target.texture && target.texture->width == width && target.texture->height == height &&
        target.texture->format == format) {
        return true;
    }
    target.texture = Context::get().create_texture(width, height, 1u, format, bind, name);
    return target.texture != nullptr;
}

// The frame's pixels of the scene target, in its own format, into `target` (the normal bloom's kept scene, the simple
// bloom's source).
bool GameBloom::copy_frame(ID3D11DeviceContext1* context, const Inputs& in, Target& target, const char* name) {
    if (!ensure_target(target, in.scene_target->format, in.frame_width, in.frame_height, kPictureBind, name)) {
        return false;
    }
    const D3D11_BOX frame{0u, 0u, 0u, in.frame_width, in.frame_height, 1u};
    context->CopySubresourceRegion(target.texture->texture.Get(), 0u, 0u, 0u, 0u, in.scene_target->texture.Get(), 0u,
                                   &frame);
    return true;
}

std::shared_ptr<Texture> GameBloom::take_image(Target& target, const Inputs& in) {
    std::shared_ptr<Texture> image;
    if (fits_scene(target.texture.get(), in)) {
        image = std::move(target.texture);
    }
    target = {};
    return image;
}

std::shared_ptr<Texture> GameBloom::take_kept_image(ID3D11DeviceContext1* context, const Inputs& in) {
    static_cast<void>(context);
    return take_image(keep_, in);
}

void GameBloom::keep_picture(std::shared_ptr<Texture> picture, const Inputs& in) {
    static_cast<void>(in);
    keep_.texture = std::move(picture);
    keep_.used = clock_;
    kept_ = keep_.texture != nullptr;
}

bool GameBloom::kept_by_trade(const Inputs& in) const {
    return kept_ && fits_scene(keep_.texture.get(), in);
}

std::shared_ptr<Texture> GameBloom::take_kept_back() {
    kept_ = false;
    std::shared_ptr<Texture> image = std::move(keep_.texture);
    keep_ = {};
    return image;
}

void GameBloom::set_aside(std::shared_ptr<Texture> picture, const Inputs& in) {
    keep_picture(std::move(picture), in);
    kept_ = false;
}

bool GameBloom::can_take_source(const Inputs& in) {
    static_cast<void>(in);
    return ready_;
}

std::shared_ptr<Texture> GameBloom::take_source_image(ID3D11DeviceContext1* context, const Inputs& in) {
    static_cast<void>(context);
    return take_image(source_, in);
}

void GameBloom::give_source(std::shared_ptr<Texture> picture, const Inputs& in) {
    static_cast<void>(in);
    source_.texture = std::move(picture);
    source_.used = clock_;
    source_given_ = source_.texture != nullptr;
}

void GameBloom::trim_idle() {
    ++clock_;
    const auto trim = [&](Target& target) {
        if (target.texture && clock_ > target.used + kIdleFrames) {
            target = {};
        }
    };
    for (Target* target : {&keep_, &source_, &work_, &box_tmp_, &mask_, &level1_, &level2_in_, &level2_}) {
        trim(*target);
    }
    for (Target& target : blur_h_) trim(target);
    for (Target& target : blur2_h_) trim(target);
}

bool GameBloom::keep(ID3D11DeviceContext1* context, const Inputs& in) {
    kept_ = ready_ && in.scene_target != nullptr && in.frame_width != 0u && in.frame_height != 0u &&
            copy_frame(context, in, keep_, "bloom kept scene");
    return kept_;
}

void GameBloom::set_constants(ID3D11DeviceContext1* context, const Constants& c) {
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (SUCCEEDED(context->Map(constants_.Get(), 0u, D3D11_MAP_WRITE_DISCARD, 0u, &mapped))) {
        std::memcpy(mapped.pData, &c, sizeof(c));
        context->Unmap(constants_.Get(), 0u);
    }
}

// One compute pass: t0-t2 read, u0-u2 written. The last pass's inputs are let go before its outputs are bound, and its
// outputs before they are read (Direct3D 11 nulls a view bound for reading and writing at once).
void GameBloom::dispatch(ID3D11DeviceContext1* context, ID3D11ComputeShader* shader, const Constants& c,
                         const std::array<Texture*, 3>& srvs, const std::array<Texture*, 3>& uavs, unsigned width,
                         unsigned height) {
    set_constants(context, c);
    ID3D11ShaderResourceView* const none[3] = {};
    context->CSSetShaderResources(0u, 3u, none);
    ID3D11UnorderedAccessView* const outputs[3] = {uav_of(uavs[0]), uav_of(uavs[1]), uav_of(uavs[2])};
    context->CSSetUnorderedAccessViews(0u, 3u, outputs, nullptr);
    ID3D11ShaderResourceView* const inputs[3] = {srv_of(srvs[0]), srv_of(srvs[1]), srv_of(srvs[2])};
    context->CSSetShaderResources(0u, 3u, inputs);
    context->CSSetShader(shader, nullptr, 0u);
    context->Dispatch((width + 7u) / 8u, (height + 7u) / 8u, 1u);
}

bool GameBloom::apply(ID3D11DeviceContext1* context, const Inputs& in, const Params& p) {
    const bool normal = p.kind == Kind::Normal;
    const bool kept = std::exchange(kept_, false);
    const bool given = std::exchange(source_given_, false);
    if (!ready_ || in.scene_target == nullptr || in.rtv == nullptr || in.frame_width == 0u || in.frame_height == 0u ||
        (normal && (!kept || !keep_.texture)) || (given && !source_.texture)) {
        return false;
    }
    if (bloom::adds_nothing(p.threshold, normal ? 0u : p.channel, p.tap1, p.intensity, p.strength)) {
        // The scene alone (kind 0), no level made: the kept scene, the given source, or (multisampled, where the
        // composite writes every sample from the resolved frame) a copy of the frame. A single-sample simple bloom
        // leaves the frame as it is.
        if (!normal && !given && (in.samples <= 1u || !copy_frame(context, in, source_, "bloom source"))) {
            return false;
        }
        const Texture& scene = normal ? *keep_.texture : *source_.texture;
        draw_composite(context, in, scene, scene, scene, Constants{});
        return true;
    }
    const bloom::Shape s = bloom::shape_of(in.frame_width, in.frame_height, in.scale);
    const unsigned blurs1 = normal ? 2u : 1u;
    bool made = ensure_target(work_, kWorkFormat, s.w1, s.h1, kWorkBind, "bloom work") &&
                ensure_target(box_tmp_, kWorkFormat, s.w1, s.h1, kWorkBind, "bloom box") &&
                ensure_target(mask_, kWorkFormat, s.w1, s.h1, kWorkBind, "bloom mask") &&
                ensure_target(level1_, kWorkFormat, s.w1, s.h1, kWorkBind, "bloom level 1");
    for (unsigned i = 0; made && i < blurs1; ++i) {
        made = ensure_target(blur_h_[i], kWorkFormat, s.w1, s.h1, kWorkBind, "bloom level 1 blur");
    }
    if (made && normal) {
        made = ensure_target(level2_in_, kWorkFormat, s.w2, s.h2, kWorkBind, "bloom level 2 source") &&
               ensure_target(level2_, kWorkFormat, s.w2, s.h2, kWorkBind, "bloom level 2");
        for (unsigned i = 0; made && i < 3u; ++i) {
            made = ensure_target(blur2_h_[i], kWorkFormat, s.w2, s.h2, kWorkBind, "bloom level 2 blur");
        }
    }
    if (!made || (!normal && !given && !copy_frame(context, in, source_, "bloom source"))) {
        if (given) {
            // The scene target is the source's last texture: the frame comes back from its picture (kind 0).
            draw_composite(context, in, *source_.texture, *source_.texture, *source_.texture, Constants{});
            return true;
        }
        return false;
    }

    // The Gaussians standing for the rings (game_bloom_shape.h).
    const bloom::Widths widths = bloom::widths_of(normal, s, in.frame_width, in.frame_height, p.ring_x, p.ring_y);
    // The scene target is read by the first pass: no render target stays bound over it.
    context->OMSetRenderTargets(0u, nullptr, nullptr);
    context->CSSetConstantBuffers(0u, 1u, constants_.GetAddressOf());
    context->CSSetSamplers(0u, 1u, linear_.GetAddressOf());
    Texture* const work = work_.texture.get();
    Texture* const box_tmp = box_tmp_.texture.get();
    Texture* const mask = mask_.texture.get();
    Texture* const level1 = level1_.texture.get();
    Texture* const blur0 = blur_h_[0].texture.get();
    Texture* const blur1 = blurs1 > 1u ? blur_h_[1].texture.get() : nullptr;

    // The selection's source: the glowing models drawn over black (normal), the scene (simple).
    {
        Constants c{};
        c.size[0] = s.w1;
        c.size[1] = s.h1;
        c.size[2] = s.f;
        c.uv[0] = static_cast<float>(in.frame_width);
        c.uv[1] = static_cast<float>(in.frame_height);
        dispatch(context, down_.Get(), c, {normal ? in.scene_target : source_.texture.get(), nullptr, nullptr},
                 {work, nullptr, nullptr}, s.w1, s.h1);
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
        dispatch(context, box_.Get(), c, {work, nullptr, nullptr}, {box_tmp, nullptr, nullptr}, s.w1, s.h1);
    }
    {
        Constants c{};
        c.size[0] = s.w1;
        c.size[1] = s.h1;
        c.size[3] = 1u;
        c.params[0] = s.box;
        c.params[1] = static_cast<float>(std::min(p.threshold, 255u));
        c.params[2] = static_cast<float>(normal ? 0u : std::min(p.channel, 3u));
        dispatch(context, box_.Get(), c, {across ? box_tmp : work, nullptr, nullptr}, {mask, nullptr, nullptr}, s.w1,
                 s.h1);
    }
    // Level 1: the rings' Gaussians across, then down, summed at the taps' weight (normal: clamped, as the scene target
    // holds it).
    {
        Constants c{};
        c.size[0] = s.w1;
        c.size[1] = s.h1;
        c.size[2] = blurs1;
        for (unsigned i = 0; i < 3u; ++i) c.sigma[i] = widths.x1[i];
        dispatch(context, blur_.Get(), c, {mask, nullptr, nullptr}, {blur0, blur1, nullptr}, s.w1, s.h1);
        c.size[3] = 1u;
        for (unsigned i = 0; i < 3u; ++i) c.sigma[i] = widths.y1[i];
        c.params[0] =
            (normal ? bloom::kLevel1Taps : bloom::kSimpleTaps) * static_cast<float>(std::min(p.tap1, 255u)) / 255.0f;
        c.params[1] = normal ? 1.0f : 0.0f;
        dispatch(context, blur_.Get(), c, {blur0, blur1, nullptr}, {level1, nullptr, nullptr}, s.w1, s.h1);
    }
    // Level 2 (normal): level 1 in n x n boxes, its three rings' Gaussians.
    if (normal) {
        Texture* const level2_in = level2_in_.texture.get();
        const std::array<Texture*, 3> blur2 = {blur2_h_[0].texture.get(), blur2_h_[1].texture.get(),
                                               blur2_h_[2].texture.get()};
        Constants c{};
        c.size[0] = s.w2;
        c.size[1] = s.h2;
        c.size[2] = s.n;
        c.uv[0] = static_cast<float>(s.w1);
        c.uv[1] = static_cast<float>(s.h1);
        dispatch(context, down_.Get(), c, {level1, nullptr, nullptr}, {level2_in, nullptr, nullptr}, s.w2, s.h2);
        c = {};
        c.size[0] = s.w2;
        c.size[1] = s.h2;
        c.size[2] = 3u;
        for (unsigned i = 0; i < 3u; ++i) c.sigma[i] = widths.x2[i];
        dispatch(context, blur_.Get(), c, {level2_in, nullptr, nullptr}, blur2, s.w2, s.h2);
        c.size[3] = 1u;
        for (unsigned i = 0; i < 3u; ++i) c.sigma[i] = widths.y2[i];
        c.params[0] = bloom::kLevel2Taps * static_cast<float>(std::min(p.tap2, 255u)) / 255.0f;
        c.params[1] = 1.0f;
        dispatch(context, blur_.Get(), c, blur2, {level2_.texture.get(), nullptr, nullptr}, s.w2, s.h2);
    }
    // The compute stage lets go of every view: the scene target is drawn into next, the levels read.
    ID3D11ShaderResourceView* const no_srvs[3] = {};
    ID3D11UnorderedAccessView* const no_uavs[3] = {};
    context->CSSetShaderResources(0u, 3u, no_srvs);
    context->CSSetUnorderedAccessViews(0u, 3u, no_uavs, nullptr);

    // The kept scene plus the bloom, over the frame.
    Constants c{};
    c.params[0] = static_cast<float>(std::min(p.intensity, 255u)) / 255.0f;
    c.params[1] = std::max(p.strength, 0.0f);
    c.params[2] = normal ? 1.0f : 2.0f;
    c.uv[0] = 1.0f / static_cast<float>(s.f * s.w1);
    c.uv[1] = 1.0f / static_cast<float>(s.f * s.h1);
    c.uv[2] = 1.0f / static_cast<float>(s.f * s.n * std::max(s.w2, 1u));
    c.uv[3] = 1.0f / static_cast<float>(s.f * s.n * std::max(s.h2, 1u));
    draw_composite(context, in, normal ? *keep_.texture : *source_.texture, *level1,
                   normal ? *level2_.texture : *level1, c);
    return true;
}

bool GameBloom::restore(ID3D11DeviceContext1* context, const Inputs& in) {
    const bool kept = std::exchange(kept_, false);
    if (!ready_ || !kept || !keep_.texture || in.rtv == nullptr || in.frame_width == 0u || in.frame_height == 0u) {
        return false;
    }
    // Kind 0: the scene alone, the levels unread.
    draw_composite(context, in, *keep_.texture, *keep_.texture, *keep_.texture, Constants{});
    return true;
}

// The scene (t0) plus the bloom from the levels (t1, t2) over the frame, into the scene target's render target; the
// views let go after.
void GameBloom::draw_composite(ID3D11DeviceContext1* context, const Inputs& in, const Texture& scene,
                               const Texture& first, const Texture& second, const Constants& c) {
    set_constants(context, c);
    context->OMSetRenderTargets(1u, &in.rtv, nullptr);
    ID3D11ShaderResourceView* const inputs[3] = {scene.view.Get(), first.view.Get(), second.view.Get()};
    context->PSSetShaderResources(0u, 3u, inputs);
    context->VSSetConstantBuffers(0u, 1u, constants_.GetAddressOf());
    context->PSSetConstantBuffers(0u, 1u, constants_.GetAddressOf());
    context->PSSetSamplers(0u, 1u, linear_.GetAddressOf());
    context->IASetInputLayout(nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(composite_vs_.Get(), nullptr, 0u);
    context->PSSetShader(composite_ps_.Get(), nullptr, 0u);
    context->RSSetState(raster_.Get());
    context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFFu);
    context->OMSetDepthStencilState(no_depth_.Get(), 0u);
    const D3D11_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(in.frame_width), static_cast<float>(in.frame_height),
                                  0.0f, 1.0f};
    context->RSSetViewports(1u, &viewport);
    context->Draw(3u, 0u);
    ID3D11ShaderResourceView* const none[3] = {};
    context->PSSetShaderResources(0u, 3u, none);
    context->OMSetRenderTargets(0u, nullptr, nullptr);
}

}  // namespace galaxy::render::d3d11
