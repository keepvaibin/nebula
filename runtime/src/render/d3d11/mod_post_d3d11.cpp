// SPDX-License-Identifier: GPL-3.0-only
// draw.post and draw.native on Direct3D 11 (docs/modding/services/draw.md): a mod's post program drawn full screen over
// the frame (RenderCore::walk_post), and a mod's native function given the device, the immediate context and the target
// at its draw point (RenderCore::walk_native).
#include "galaxy/render/d3d11/renderer_d3d11.h"

#include "galaxy/render/mod_post.h"

#include <nebula_frame_list.h>

#include <algorithm>
#include <cstring>
#include <iostream>

namespace galaxy::render {

// The program's two stages, made the first time it is drawn; a stage the driver refuses leaves the entry without
// them (said once a program).
const RendererD3D11::ModPostShaders* RendererD3D11::mod_post_stages(const ModPostProgram& program) {
    // Programs freed since the last look leave the cache. The context keeps what it still draws with alive.
    const std::uint64_t epoch = mod_post_epoch();
    if (epoch != mod_post_epoch_) {
        mod_post_epoch_ = epoch;
        std::erase_if(mod_post_shaders_, [](const ModPostShaders& kept) { return mod_post_program(kept.program) == nullptr; });
    }
    for (const ModPostShaders& kept : mod_post_shaders_) {
        if (kept.program == program.id) {
            return &kept;
        }
    }
    ID3D11Device1* const device = d3d11::Context::get().device();
    ModPostShaders made{program.id, nullptr, nullptr};
    const HRESULT vertex = device->CreateVertexShader(program.vertex.data(), program.vertex.size(), nullptr, &made.vertex);
    const HRESULT pixel = FAILED(vertex) ? vertex : device->CreatePixelShader(program.pixel.data(), program.pixel.size(), nullptr, &made.pixel);
    if (FAILED(vertex) || FAILED(pixel)) {
        std::cerr << "[draw.post] program " << program.id << " refused by the driver " << std::hex
                  << static_cast<unsigned long>(FAILED(vertex) ? vertex : pixel) << std::dec << '\n';
        made.vertex.Reset();
        made.pixel.Reset();
    }
    mod_post_shaders_.push_back(std::move(made));
    return &mod_post_shaders_.back();
}

// The layout of mod_post.h: the block at b0, the frame, depth and motion at t0 to t2, linear and point clamp at s0 and
// s1. Into the scene target's pass views (the multisampled colour with MSAA), every sample of a pixel the same value.
void RendererD3D11::record_mod_post(const ModPostProgram& program, ModPostConstants constants, const gpu::TextureRef& picture,
                                    bool depth_known) {
    if (!picture || !picture->view || !scene_.color || mod_post_failed_) {
        return;
    }
    ID3D11DeviceContext1* const ctx = context();
    if (!mod_post_constants_) {
        D3D11_BUFFER_DESC desc{};
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        desc.ByteWidth = sizeof(ModPostConstants);
        if (FAILED(d3d11::Context::get().device()->CreateBuffer(&desc, nullptr, &mod_post_constants_))) {
            std::cerr << "[draw.post] constant buffer failed: post programs draw nothing\n";
            mod_post_failed_ = true;
            return;
        }
    }
    const ModPostShaders* const stages = mod_post_stages(program);
    if (stages == nullptr || !stages->vertex || !stages->pixel) {
        return;
    }
    const GpuPassScope pass{*this, GpuPass::FramePass};
    end_scene_pass();
    const unsigned width = scene_.color->width;
    const unsigned height = scene_.color->height;
    constants.size[0] = static_cast<float>(width);
    constants.size[1] = static_cast<float>(height);
    constants.inv_size[0] = 1.0f / static_cast<float>(width);
    constants.inv_size[1] = 1.0f / static_cast<float>(height);
    constants.flags = 0u;

    // The scene's depth: the scene target's own (multisampled: its resolve), or with the output scene target in place
    // (the 2D layer at the window's size) the internal one it was drawn at, which covers the same view. None inside a
    // target pass (its own camera). No motion on Direct3D 11 (set_mod_post_motion(false)): t2 stays empty.
    ID3D11ShaderResourceView* depth = nullptr;
    if (depth_known && (program.reads & kModPostReadsDepth) != 0u && !target_pass_active_) {
        if (!output_scene_active_) {
            resolve_scene_depth();
            depth = scene_.depth ? scene_.depth->view.Get() : nullptr;
        } else if (alt_scene_.depth) {
            // The scene ended with its samples' depth unresolved (begin_output_scene carries its colour only): that set
            // in place for its resolve, then the output scene target back.
            if (alt_scene_.samples > 1u && alt_scene_.depth_dirty) {
                swap_scene_sets();
                resolve_scene_depth();
                swap_scene_sets();
            }
            if (alt_scene_.samples <= 1u || !alt_scene_.depth_dirty) {
                depth = alt_scene_.depth->view.Get();
            }
        }
    }
    if (depth != nullptr) {
        constants.flags |= kModPostHasDepth;
    }
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(ctx->Map(mod_post_constants_.Get(), 0u, D3D11_MAP_WRITE_DISCARD, 0u, &mapped))) {
        return;
    }
    std::memcpy(mapped.pData, &constants, sizeof(constants));
    ctx->Unmap(mod_post_constants_.Get(), 0u);

    ID3D11RenderTargetView* const rtv = scene_.rtv();
    ctx->OMSetRenderTargets(1u, &rtv, nullptr);
    ID3D11Buffer* const block = mod_post_constants_.Get();
    ctx->PSSetConstantBuffers(0u, 1u, &block);
    ID3D11ShaderResourceView* const views[3] = {picture->view.Get(), depth, nullptr};
    ctx->PSSetShaderResources(0u, 3u, views);
    ID3D11SamplerState* const samplers[2] = {linear_clamp_.Get(), point_clamp_.Get()};
    ctx->PSSetSamplers(0u, 2u, samplers);
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(stages->vertex.Get(), nullptr, 0u);
    ctx->PSSetShader(stages->pixel.Get(), nullptr, 0u);
    ctx->RSSetState(utility_raster_.Get());
    const D3D11_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f};
    ctx->RSSetViewports(1u, &viewport);
    const D3D11_RECT scissor{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    ctx->RSSetScissorRects(1u, &scissor);
    ctx->OMSetBlendState(write_masks_[0xFu].Get(), nullptr, 0xFFFFFFFFu);
    ctx->OMSetDepthStencilState(depth_uses_[static_cast<std::size_t>(DepthUse::None)].Get(), 0u);
    ctx->Draw(3u, 0u);
    // The inputs let go (the picture is a target again later, the depth the scene's), the targets too.
    ID3D11ShaderResourceView* const none[3] = {};
    ctx->PSSetShaderResources(0u, 3u, none);
    ctx->OMSetRenderTargets(0u, nullptr, nullptr);
    scene_.color_dirty |= scene_.samples > 1u;
    // The draws after it bind their own state again; the scene pass opens with their first.
    reset_draw_bindings();
}

// The function draws on a context state of its own, made the first time (the device's interface and feature level):
// whatever it binds stays out of the renderer's state, which comes back whole after it.
void RendererD3D11::record_native_call(const NebulaNativeCall& call, LumaDrawNativeFrame& frame, bool scene_point) {
    if (call.fn == nullptr || !scene_.color || native_state_failed_) {
        return;
    }
    ID3D11Device1* const device = d3d11::Context::get().device();
    if (!native_state_) {
        const D3D_FEATURE_LEVEL level = device->GetFeatureLevel();
        if (FAILED(device->CreateDeviceContextState(0u, &level, 1u, D3D11_SDK_VERSION, __uuidof(ID3D11Device1), nullptr,
                                                    &native_state_))) {
            std::cerr << "[draw.native] context state failed: native functions are not called\n";
            native_state_failed_ = true;
            return;
        }
    }
    const GpuPassScope pass{*this, GpuPass::FramePass};
    // The draws recorded so far run first, and the scene's targets are let go.
    end_scene_pass();
    const bool msaa = scene_.samples > 1u && scene_.ms_color;
    const d3d11::Texture& target = msaa ? *scene_.ms_color : *scene_.color;
    const d3d11::Texture* const depth = scene_point ? (msaa ? scene_.ms_depth.get() : scene_.depth.get()) : nullptr;
    ID3D11DeviceContext1* const ctx = context();
    frame.backend = LUMA_DRAW_NATIVE_D3D11;
    frame.width = target.width;
    frame.height = target.height;
    frame.samples = std::max(scene_.samples, 1u);
    frame.d3d11.device = device;
    frame.d3d11.context = ctx;
    frame.d3d11.target = target.texture.Get();
    frame.d3d11.target_rtv = target.target.Get();
    frame.d3d11.target_format = static_cast<std::uint32_t>(target.format);
    if (depth != nullptr && depth->depth) {
        frame.d3d11.depth = depth->texture.Get();
        frame.d3d11.depth_dsv = depth->depth.Get();
        frame.d3d11.depth_format = static_cast<std::uint32_t>(depth->format);
    }
    d3d11::ComPtr<ID3DDeviceContextState> renderer_state;
    ctx->SwapDeviceContextState(native_state_.Get(), &renderer_state);
    call.fn(call.user, &frame);
    // Nothing the function bound stays referenced by its state until the next call.
    ctx->ClearState();
    ctx->SwapDeviceContextState(renderer_state.Get(), nullptr);
    // What the function drew is in the targets it was given: the multisampled ones' resolves are stale.
    if (msaa) {
        scene_.color_dirty = true;
        scene_.depth_dirty = scene_.depth_dirty || depth != nullptr;
    }
    reset_draw_bindings();
}

void RendererD3D11::release_mod_post() {
    mod_post_shaders_.clear();
    mod_post_epoch_ = 0u;
    mod_post_constants_.Reset();
    mod_post_failed_ = false;
    native_state_.Reset();
    native_state_failed_ = false;
}

}  // namespace galaxy::render
