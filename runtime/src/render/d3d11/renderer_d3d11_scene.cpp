// SPDX-License-Identifier: GPL-3.0-only
// RendererD3D11's scene target (renderer_d3d11.h): its sets and passes, clears, resolves, the target passes and the
// pictures draws sample, copies of the frame, the output and wide scenes, where the renderer's parts meet the frame,
// screen probes and the test captures.
//
// Direct3D 11 has no render passes: the scene pass is the scene target bound with its draws recorded (gpu::Encoder),
// replayed when it ends or the renderer draws in it (flush_draws). Every call here that reads or writes a target itself
// ends the pass first or runs the recorded draws, so the context sees each call in the stream's order.
#include "galaxy/render/d3d11/renderer_d3d11.h"

#include "galaxy/content_viewport.h"
#include "galaxy/experimental_ultrawide_aspect.h"
#include "galaxy/platform_time.h"
#include "galaxy/render/game_bloom_shape.h"
#include "galaxy/render/texture_store.h"
#include "galaxy/render_api.h"

#include <nebula_frame_list.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace galaxy::render {

namespace {

constexpr UINT kTargetBinds = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
constexpr UINT kDepthBinds = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;

D3D11_VIEWPORT whole_viewport(unsigned width, unsigned height) noexcept {
    return D3D11_VIEWPORT{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f};
}

gpu::Rect whole_rect(unsigned width, unsigned height) noexcept {
    return gpu::rect(0, 0, static_cast<std::int32_t>(width), static_cast<std::int32_t>(height));
}

// A CPU-readable copy of `width` x `height` pixels of `format`, kept while its size and format stay.
bool ensure_staging(d3d11::ComPtr<ID3D11Texture2D>& staging, unsigned width, unsigned height, DXGI_FORMAT format) {
    if (staging) {
        D3D11_TEXTURE2D_DESC had{};
        staging->GetDesc(&had);
        if (had.Width == width && had.Height == height && had.Format == format) {
            return true;
        }
        staging.Reset();
    }
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1u;
    desc.ArraySize = 1u;
    desc.Format = format;
    desc.SampleDesc.Count = 1u;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    return SUCCEEDED(d3d11::Context::get().device()->CreateTexture2D(&desc, nullptr, &staging));
}

// The staging copy's pixels as RGBA8 rows (a 10-bit picture converted), or empty when it cannot be read.
std::vector<std::uint8_t> read_staging(ID3D11DeviceContext1* context, ID3D11Texture2D* staging, unsigned width, unsigned height) {
    std::vector<std::uint8_t> rgba;
    D3D11_TEXTURE2D_DESC desc{};
    staging->GetDesc(&desc);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(staging, 0u, D3D11_MAP_READ, 0u, &mapped))) {
        return rgba;
    }
    rgba.resize(static_cast<std::size_t>(width) * height * 4u);
    const auto* rows = static_cast<const std::uint8_t*>(mapped.pData);
    if (desc.Format == DXGI_FORMAT_R10G10B10A2_UNORM) {
        ten_bit_to_rgba8(rows, rgba.data(), width, height, mapped.RowPitch, width * 4u);
    } else {
        for (unsigned y = 0; y < height; ++y) {
            std::memcpy(rgba.data() + static_cast<std::size_t>(y) * width * 4u, rows + static_cast<std::size_t>(y) * mapped.RowPitch,
                        static_cast<std::size_t>(width) * 4u);
        }
    }
    context->Unmap(staging, 0u);
    return rgba;
}

// RGBA8 rows written as a binary PPM off the render thread.
void write_ppm_async(std::string path, const std::vector<std::uint8_t>& rgba, unsigned width, unsigned height) {
    std::vector<std::uint8_t> rgb(static_cast<std::size_t>(width) * height * 3u);
    for (std::size_t i = 0, n = rgb.size() / 3u; i < n; ++i) {
        rgb[i * 3u] = rgba[i * 4u];
        rgb[i * 3u + 1u] = rgba[i * 4u + 1u];
        rgb[i * 3u + 2u] = rgba[i * 4u + 2u];
    }
    std::thread([path = std::move(path), rgb = std::move(rgb), width, height] {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << "P6\n" << width << ' ' << height << "\n255\n";
        out.write(reinterpret_cast<const char*>(rgb.data()), static_cast<std::streamsize>(rgb.size()));
    }).detach();
}

}  // namespace

// ---- scene target sets ----

bool RendererD3D11::create_scene_set(SceneSet& set, const FrameMap& map, unsigned samples, unsigned set_width,
                                     unsigned set_height, bool target) {
    d3d11::Context& context = d3d11::Context::get();
    const auto width = std::max(1u, static_cast<std::uint32_t>(std::lround(static_cast<double>(set_width) * map.sx)));
    const auto height = std::max(1u, static_cast<std::uint32_t>(std::lround(static_cast<double>(set_height) * map.sy)));
    const DXGI_FORMAT color_format = target ? draw_layout_.target_color : draw_layout_.scene_color;
    gpu::TextureRef color = context.create_texture(width, height, 1u, color_format, kTargetBinds, "scene target color");
    gpu::TextureRef depth = context.create_texture(width, height, 1u, draw_layout_.depth, kDepthBinds, "scene target depth");
    if (!color || !depth) {
        std::cerr << "[Renderer] scene target " << width << 'x' << height << " allocation failed\n";
        return false;
    }
    gpu::TextureRef ms_color;
    gpu::TextureRef ms_depth;
    if (samples > 1u) {
        ms_color = context.create_texture(width, height, 1u, color_format, kTargetBinds, "scene target color MSAA", samples);
        ms_depth = context.create_texture(width, height, 1u, draw_layout_.depth, kDepthBinds, "scene target depth MSAA", samples);
        if (!ms_color || !ms_depth) {
            std::cerr << "[Renderer] scene target MSAA " << samples << "x allocation failed; single-sample\n";
            ms_color.reset();
            ms_depth.reset();
            samples = 1u;
        }
    }
    set.color = std::move(color);
    set.depth = std::move(depth);
    set.ms_color = std::move(ms_color);
    set.ms_depth = std::move(ms_depth);
    set.samples = samples;
    set.depth_dirty = false;
    set.color_dirty = false;
    set.wide = false;
    return true;
}

// A set's targets go with the frame that may still read them.
void RendererD3D11::destroy_scene_set(SceneSet& set) {
    retire(std::move(set.color));
    retire(std::move(set.depth));
    retire(std::move(set.ms_color));
    retire(std::move(set.ms_depth));
    set.samples = 1u;
}

bool RendererD3D11::create_scene_targets(const FrameMap& map, unsigned samples) {
    SceneSet made;
    if (!create_scene_set(made, map, samples)) {
        return false;
    }
    if (scene_.color) {
        destroy_scene_set(scene_);
    }
    scene_ = std::move(made);
    scene_map_ = map;
    if (scene_.samples > 1u) {
        std::cout << "[Renderer] scene target MSAA " << scene_.samples << "x\n";
    }
    return true;
}

unsigned RendererD3D11::wanted_msaa_samples() const {
    return std::min(std::bit_floor(std::clamp(get_render_config().msaa_samples, 1u, 8u)), draw_layout_.max_samples);
}

// Before the scene target's single-sample colour is read: the multisampled colour drawn since the last resolve.
void RendererD3D11::resolve_scene_color() {
    SceneSet& set = scene_;
    if (set.samples <= 1u || !set.color_dirty || !set.ms_color || !set.color) {
        return;
    }
    end_scene_pass();
    const GpuPassScope scope{*this, GpuPass::Resolve};
    context()->ResolveSubresource(set.color->texture.Get(), 0u, set.ms_color->texture.Get(), 0u, set.color->format);
    set.color_dirty = false;
}

// Before the scene target's single-sample depth is read: each pixel's first sample of the multisampled depth (the
// stencil is not resolved, as on the other renderers).
void RendererD3D11::resolve_scene_depth() {
    SceneSet& set = scene_;
    if (set.samples <= 1u || !set.depth_dirty || !set.ms_depth || !set.depth) {
        return;
    }
    end_scene_pass();
    const GpuPassScope scope{*this, GpuPass::Resolve};
    ID3D11DeviceContext1* const ctx = context();
    ctx->OMSetRenderTargets(0u, nullptr, set.depth->depth.Get());
    utility_draw(Utility::DepthResolve, set.ms_depth->view.Get(), nullptr, 0u, whole_viewport(set.depth->width, set.depth->height),
                 whole_rect(set.depth->width, set.depth->height), 0u, DepthUse::Resolve);
    ctx->OMSetRenderTargets(0u, nullptr, nullptr);
    set.depth_dirty = false;
}

FrameMap RendererD3D11::output_frame_for_window() const {
    const auto aspect = galaxy::experimental_effective_host_aspect();
    const galaxy::ContentViewport content = galaxy::fit_content_viewport(
        static_cast<int>(host_window::backbuffer_width), static_cast<int>(host_window::backbuffer_height),
        aspect ? aspect->content_aspect : galaxy::kDesignContentAspect);
    if (content.width <= 0 || content.height <= 0) {
        return FrameMap{};
    }
    return FrameMap(static_cast<unsigned>(content.width), static_cast<unsigned>(content.height));
}

// A changed internal resolution, between frames (renderer_d3d12.cpp apply_window_size): the GPU finishes with the old
// targets and every scaled copy first.
void RendererD3D11::apply_window_size() {
    if (!scene_.color || output_scene_active_ || scene_.wide) {
        return;
    }
    if (host_window::move_size_active.load(std::memory_order_acquire)) {
        return;
    }
    const FrameMap output = output_frame_for_window();
    if (output.empty()) {
        return;
    }
    const RenderConfig config = get_render_config();
    const auto [render_width, render_height] =
        internal_extent(output.width, output.height, render_internal_height(config), D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION,
                        host_window::upscaler_render_fraction(config, output.width, output.height));
    const FrameMap wanted(render_width, render_height);
    const unsigned samples = wanted_msaa_samples();
    if (wanted == scene_map_ && samples == scene_.samples) {
        return;
    }
    // The scale's memory (colour 4 bytes a pixel, depth 8, the display pictures 12) must fit 90% of the budget.
    if (wanted.width * wanted.height > scene_map_.width * scene_map_.height) {
        std::uint64_t usage = 0u;
        std::uint64_t budget = 0u;
        const double needed = 24.0 * (static_cast<double>(wanted.width) * wanted.height -
                                      static_cast<double>(scene_map_.width) * scene_map_.height);
        if (video_memory(usage, budget) && budget != 0u && static_cast<double>(usage) + needed > 0.9 * static_cast<double>(budget)) {
            if (frame_index_ % 600u == 0u) {
                std::cerr << "[Renderer] " << wanted.width << "x" << wanted.height << " exceeds the video-memory budget; staying at "
                          << scene_map_.width << "x" << scene_map_.height << '\n';
            }
            return;
        }
    }
    wait_for_gpu_idle();
    frame_pass_targets_.clear();
    for (Frame& frame : frames_) {
        frame.retired.clear();
        frame.retired_buffers.clear();
    }
    const FrameMap previous = scene_map_;
    if (!create_scene_targets(wanted, samples)) {
        return;
    }
    reset_draw_bindings();
    bind_scene();
    std::cout << "[Renderer] internal resolution " << previous.width << "x" << previous.height << " -> " << wanted.width << "x"
              << wanted.height;
    if (scene_.samples > 1u) {
        std::cout << " MSAA " << scene_.samples << 'x';
    }
    std::cout << '\n';
}

// ---- the scene pass ----

void RendererD3D11::bind_pass_targets() {
    ID3D11DeviceContext1* const ctx = context();
    ID3D11RenderTargetView* const rtv = scene_.rtv();
    ID3D11DepthStencilView* dsv = scene_.dsv();
    // The frame's depth sampled by this pass's draws (open_frame_for_pass) stays testable through the read-only view:
    // Direct3D 11 would unbind a view of a texture bound for writing.
    if (frame_pass_depth_key_ != 0u && scene_.samples <= 1u && scene_.depth && scene_.depth->depth_read) {
        if (const auto it = pictures_.find(frame_pass_depth_key_); it != pictures_.end() && it->second.viewed == scene_.depth) {
            dsv = scene_.depth->depth_read.Get();
        }
    }
    ctx->OMSetRenderTargets(1u, &rtv, dsv);
    const unsigned width = scene_.color ? scene_.color->width : 1u;
    const unsigned height = scene_.color ? scene_.color->height : 1u;
    const D3D11_VIEWPORT viewport =
        bound_.viewport_valid ? D3D11_VIEWPORT{bound_.viewport.x, bound_.viewport.y, bound_.viewport.width, bound_.viewport.height,
                                               bound_.viewport.min_depth, bound_.viewport.max_depth}
                              : whole_viewport(width, height);
    ctx->RSSetViewports(1u, &viewport);
    const gpu::Rect scissor = bound_.scissor_valid ? bound_.scissor : whole_rect(width, height);
    const D3D11_RECT rect{scissor.left, scissor.top, scissor.right, scissor.bottom};
    ctx->RSSetScissorRects(1u, &rect);
    // Whatever the walker bound before is bound again by its next draw.
    encoder_.reset();
}

void RendererD3D11::begin_scene_pass(const char*) {
    if (scene_pass_open_ || !scene_.color) {
        return;
    }
    bind_pass_targets();
    scene_pass_open_ = true;
}

void RendererD3D11::end_scene_pass(const char* caller) {
    if (!scene_pass_open_) {
        return;
    }
    flush_draws();
    // The targets let go: a part may sample what the pass drew into.
    context()->OMSetRenderTargets(0u, nullptr, nullptr);
    scene_pass_open_ = false;
    scene_pass_closed_by_ = caller;
}

void RendererD3D11::bind_scene() {
    if (!scene_.color) {
        return;
    }
    const auto width = static_cast<float>(scene_.color->width);
    const auto height = static_cast<float>(scene_.color->height);
    bound_.viewport = gpu::ViewportRect{0.0f, 0.0f, width, height, 0.0f, 1.0f};
    bound_.viewport_valid = true;
    bound_.scissor = whole_rect(scene_.color->width, scene_.color->height);
    bound_.scissor_valid = true;
    if (scene_pass_open_) {
        // The draws recorded so far run with the rectangles they were recorded with; the next ones with these.
        flush_draws();
        bind_pass_targets();
    }
}

// As renderer_d3d12.cpp clear_scene: every channel of the whole target by the clear calls, a rectangle of every channel
// by ClearView, some channels by the clear's draw with a write mask; the whole depth by ClearDepthStencilView (stencil
// 0), a rectangle of it by the depth clear's draw (stencil 0 too). Into the multisampled targets with MSAA.
void RendererD3D11::clear_scene(std::uint32_t clear_rgba, bool color_enable, bool alpha_enable, bool depth_enable,
                                unsigned rect_x, unsigned rect_y, unsigned rect_w, unsigned rect_h, std::uint8_t pixel_format) {
    if (!scene_.color) {
        return;
    }
    const auto scene_w = static_cast<std::int32_t>(scene_.color->width);
    const auto scene_h = static_cast<std::int32_t>(scene_.color->height);
    gpu::Rect rect = gpu::rect(0, 0, scene_w, scene_h);
    if (rect_w != 0u && rect_h != 0u) {
        const ScaledSceneRect scaled = rect_pixels(
            static_cast<std::int32_t>(rect_x), static_cast<std::int32_t>(rect_y), static_cast<std::int32_t>(rect_x + rect_w),
            static_cast<std::int32_t>(rect_y + rect_h), scene_map_);
        if (scaled.right <= scaled.left || scaled.bottom <= scaled.top) {
            return;
        }
        rect = gpu::rect(scaled.left, scaled.top, scaled.right, scaled.bottom);
    }
    const bool whole = rect.left == 0 && rect.top == 0 && rect.right == scene_w && rect.bottom == scene_h;
    const SceneClearValues clear = make_scene_clear_values(clear_rgba, pixel_format);
    const bool has_alpha = pixel_format == 1u;
    const bool every_channel = color_enable && (alpha_enable || !has_alpha);
    // The draws before the clear run first.
    flush_draws();
    ID3D11DeviceContext1* const ctx = context();
    ID3D11RenderTargetView* const rtv = scene_.rtv();
    ID3D11DepthStencilView* const dsv = scene_.dsv();
    const D3D11_RECT box{rect.left, rect.top, rect.right, rect.bottom};
    bool drew = false;
    if (color_enable || alpha_enable) {
        scene_.color_dirty |= scene_.samples > 1u;
        if (every_channel && whole) {
            ctx->ClearRenderTargetView(rtv, clear.color.data());
        } else if (every_channel) {
            ctx->ClearView(rtv, clear.color.data(), &box, 1u);
        } else {
            const std::uint8_t mask = color_enable ? D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN |
                                                         D3D11_COLOR_WRITE_ENABLE_BLUE
                                                   : D3D11_COLOR_WRITE_ENABLE_ALPHA;
            ctx->OMSetRenderTargets(1u, &rtv, nullptr);
            utility_draw(Utility::Clear, nullptr, clear.color.data(), sizeof(float) * 4u, whole_viewport(scene_w, scene_h), rect, mask);
            drew = true;
        }
    }
    if (depth_enable) {
        scene_.depth_dirty |= scene_.samples > 1u;
        if (whole) {
            ctx->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, clear.reversed_depth, 0u);
        } else {
            const float value[4] = {clear.reversed_depth, 0.0f, 0.0f, 0.0f};
            ctx->OMSetRenderTargets(0u, nullptr, dsv);
            utility_draw(Utility::ClearDepth, nullptr, value, sizeof(value), whole_viewport(scene_w, scene_h), rect, 0u, DepthUse::Clear);
            drew = true;
        }
    }
    if (drew) {
        if (scene_pass_open_) {
            bind_pass_targets();
        } else {
            ctx->OMSetRenderTargets(0u, nullptr, nullptr);
        }
    }
}

void RendererD3D11::set_draw_aux(unsigned) {
    if (!scene_pass_open_) {
        begin_scene_pass();
    }
}

// The scene target and the output scene target trade places.
void RendererD3D11::swap_scene_sets() {
    end_scene_pass();
    std::swap(scene_, alt_scene_);
    std::swap(scene_map_, alt_scene_map_);
    output_scene_active_ = !output_scene_active_;
    reset_draw_bindings();
}

// The scene target and the target passes' set trade places (begin_target_pass).
void RendererD3D11::swap_target_set() {
    end_scene_pass();
    std::swap(scene_, target_scene_);
    std::swap(scene_map_, target_scene_map_);
    target_pass_active_ = !target_pass_active_;
    reset_draw_bindings();
}

bool RendererD3D11::ensure_output_set(const FrameMap& output) {
    output_scene_used_frame_ = frame_index_;
    if (alt_scene_.color && alt_scene_map_ == output) {
        return true;
    }
    if (alt_scene_.color) {
        destroy_scene_set(alt_scene_);
    }
    if (!create_scene_set(alt_scene_, output)) {
        alt_scene_map_ = FrameMap{};
        return false;
    }
    alt_scene_map_ = output;
    return true;
}

// As renderer_d3d12.cpp begin_output_scene without a temporal upscaler: the whole scene target resampled into the output
// scene target with the display copy's identity settings, which then takes its place with its depth at the far plane.
bool RendererD3D11::begin_output_scene(bool resolve_msaa_only) {
    if (output_scene_active_ || !scene_.color) {
        return false;
    }
    const FrameMap output = output_frame_for_window();
    if (resolve_msaa_only) {
        if (scene_.samples <= 1u || output != scene_map_) {
            return false;
        }
    } else {
        const RenderConfig cfg = get_render_config();
        const bool full = cfg.hud_output_resolution || render_internal_height(cfg) != 0u || sharp_2d_requested();
        if (!full || output.empty() || output.width < scene_map_.width || output == scene_map_) {
            return false;
        }
    }
    if (output.empty() || !ensure_output_set(output)) {
        return false;
    }
    const FramePassGeometry sampling = frame_pass_geometry(output.width, output.height, scene_map_);
    record_conversion_blit(*alt_scene_.color, output.width, output.height, kCopyLayoutDisplay, sampling.src_x, sampling.src_y,
                           sampling.step_x, sampling.step_y);
    swap_scene_sets();
    bind_scene();
    clear_scene(0u, false, false, true);
    return true;
}

void RendererD3D11::end_output_scene() {
    if (!output_scene_active_) {
        return;
    }
    swap_scene_sets();
    bind_scene();
}

// ---- target passes and pictures ----

bool RendererD3D11::begin_target_pass(std::uint64_t key, std::uint32_t width, std::uint32_t height) {
    if (target_pass_active_ || width == 0u || height == 0u) {
        return false;
    }
    TargetSlot& slot = target_slots_[key];
    const FrameMap map(width, height);
    if (!slot.set.color || !(slot.map == map)) {
        if (slot.set.color || slot.set.depth) {
            destroy_scene_set(slot.set);
        }
        // The pass's first clear (RenderCore) fills a new set.
        if (!create_scene_set(slot.set, map, 1u, kFrameUnits, kFrameUnits, true)) {
            target_slots_.erase(key);
            return false;
        }
        slot.map = map;
        slot.width = width;
        slot.height = height;
    }
    slot.used = frame_index_;
    target_key_ = key;
    std::swap(target_scene_, slot.set);
    target_scene_map_ = slot.map;
    swap_target_set();
    bind_scene();
    return true;
}

void RendererD3D11::end_target_pass(std::uint8_t channels, TextureStore& textures) {
    if (!target_pass_active_) {
        return;
    }
    swap_target_set();
    if (frame_pass_color_key_ != 0u || frame_pass_depth_key_ != 0u) {
        close_frame_for_pass(textures);
    }
    // The picture is sampled until the key's next pass.
    TargetSlot& slot = target_slots_[target_key_];
    std::swap(slot.set, target_scene_);
    slot.map = target_scene_map_;
    target_scene_map_ = FrameMap{};
    (void)publish_picture(target_key_, slot.width, slot.height, slot.set.color, channels, textures);
    sweep_target_slots();
    bind_scene();
}

void RendererD3D11::retire_target_slot(TargetSlot& slot) {
    destroy_scene_set(slot.set);
}

// Keys unused for kTargetIdleFrames frames give back their targets (their pictures stay).
void RendererD3D11::sweep_target_slots() {
    if (target_slots_.empty()) {
        return;
    }
    const std::uint64_t idle = target_idle_frames();
    for (auto it = target_slots_.begin(); it != target_slots_.end();) {
        if (it->second.used + idle < frame_index_ && !(target_pass_active_ && it->first == target_key_)) {
            retire_target_slot(it->second);
            it = target_slots_.erase(it);
        } else {
            ++it;
        }
    }
}

bool RendererD3D11::target_picture(std::uint64_t key, TextureHandle& out) const {
    const auto it = pictures_.find(key);
    if (it == pictures_.end() || !it->second.viewed) {
        return false;
    }
    out = TextureHandle{};
    out.resource = gpu::raw(it->second.viewed);
    out.srv_index = it->second.view;
    out.width = it->second.viewed->width;
    out.height = it->second.viewed->height;
    out.mip_levels = 1u;
    return true;
}

void RendererD3D11::drop_picture(Picture& picture, TextureStore& textures) {
    textures.retire_view(picture.view, std::move(picture.viewed));
    retire(std::move(picture.sampled));
    picture.view = ~0u;
}

// A picture's view slot names the texture, or with channels the other renderers' views repeat (ALPHA, RED, RGB) a copy
// of it with those channels (target_channel_swizzle): Direct3D 11 views cannot repeat a channel. The copy is the
// conversion shader's at one pixel a pixel, which writes exactly the channels a swizzled view reads.
bool RendererD3D11::publish_picture(std::uint64_t key, std::uint32_t width, std::uint32_t height, gpu::TextureRef texture,
                                    std::uint8_t channels, TextureStore& textures) {
    if (!texture) {
        return false;
    }
    Picture& picture = pictures_[key];
    const bool same = picture.view != ~0u && picture.viewed == texture && picture.channels == channels && picture.width == width &&
                      picture.height == height;
    if (same && !picture.sampled) {
        return false;
    }
    if (!same) {
        if (picture.view != ~0u) {
            drop_picture(picture, textures);
        }
        picture.view = textures.allocate_view();
        picture.viewed = std::move(texture);
        picture.width = width;
        picture.height = height;
        picture.channels = channels;
    }
    const std::array<std::uint8_t, 4> swizzle = target_channel_swizzle(channels);
    if (swizzle != std::array<std::uint8_t, 4>{0, 1, 2, 3}) {
        // Made again each time: the texture's contents are this pass's.
        const d3d11::Texture& whole = *picture.viewed;
        if (!picture.sampled) {
            picture.sampled =
                d3d11::Context::get().create_texture(whole.width, whole.height, 1u, whole.format, kTargetBinds, "picture channels");
            if (!picture.sampled) {
                throw std::runtime_error("a picture's channel copy could not be made");
            }
        }
        conversion_draw(whole.view.Get(), *picture.sampled, whole.width, whole.height, channels, 0.0f, 0.0f, 1.0f, 1.0f);
    }
    if (same) {
        return false;
    }
    textures.backend().write_view(picture.view, picture.sampled ? picture.sampled.get() : picture.viewed.get(), 1u);
    return true;
}

void RendererD3D11::free_target(std::uint32_t id, TextureStore& textures) {
    for (auto it = pictures_.begin(); it != pictures_.end();) {
        if (static_cast<std::uint32_t>(it->first) == id) {
            drop_picture(it->second, textures);
            it = pictures_.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = frame_pass_targets_.begin(); it != frame_pass_targets_.end();) {
        if (static_cast<std::uint32_t>(it->first) == id) {
            retire(std::move(it->second.texture));
            it = frame_pass_targets_.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = target_slots_.begin(); it != target_slots_.end();) {
        if (static_cast<std::uint32_t>(it->first) == id && !(target_pass_active_ && it->first == target_key_)) {
            retire_target_slot(it->second);
            it = target_slots_.erase(it);
        } else {
            ++it;
        }
    }
}

// A texture the GPU may still read: released once the frame that can name it is past.
void RendererD3D11::retire(gpu::TextureRef texture) {
    if (texture) {
        frames_[frame_slot_].retired.push_back(std::move(texture));
    }
}

// ---- copies of the scene target ----

void RendererD3D11::conversion_draw(ID3D11ShaderResourceView* source, d3d11::Texture& dest, unsigned dest_w, unsigned dest_h,
                                    std::uint8_t copy_format, float src_x, float src_y, float step_x, float step_y) {
    end_scene_pass();
    // A display copy one pixel to one picture pixel without gamma: its filter alone.
    const bool display_filter = copy_format == kCopyLayoutDisplay && step_x == 1.0f && step_y == 1.0f;
    const SceneConversionShaderConstants constants = make_scene_conversion_constants(copy_format, src_x, src_y, step_x, step_y);
    ID3D11DeviceContext1* const ctx = context();
    ID3D11RenderTargetView* const target = dest.target.Get();
    ctx->OMSetRenderTargets(1u, &target, nullptr);
    utility_draw(display_filter ? Utility::DisplayFilter : Utility::Convert, source, &constants, sizeof(constants),
                 whole_viewport(dest_w, dest_h), whole_rect(dest_w, dest_h));
    ctx->OMSetRenderTargets(0u, nullptr, nullptr);
}

// The scene target's colour drawn into `dest`: `copy_format` the channels the picture keeps, the source rectangle from
// (src_x, src_y) in steps of (step_x, step_y) frame pixels a picture pixel.
void RendererD3D11::record_conversion_blit(d3d11::Texture& dest, unsigned dest_w, unsigned dest_h, std::uint8_t copy_format,
                                           float src_x, float src_y, float step_x, float step_y) {
    resolve_scene_color();
    conversion_draw(scene_.color->view.Get(), dest, dest_w, dest_h, copy_format, src_x, src_y, step_x, step_y);
}

bool RendererD3D11::can_take_scene() const noexcept {
    return scene_.color && !output_scene_active_ && !target_pass_active_ && scene_.samples <= 1u && !scene_.wide;
}

bool RendererD3D11::take_scene(std::uint64_t key, gpu::TextureRef fresh, TextureStore& textures, gpu::TextureRef& released) {
    end_scene_pass();
    if (const auto it = pictures_.find(key); it != pictures_.end()) {
        released = it->second.viewed;
    }
    gpu::TextureRef picture = swap_scene_color(std::move(fresh));
    const FrameExtent size = frame_pass_extent(scene_map_);
    return publish_picture(key, size.width, size.height, std::move(picture), 0u, textures);
}

void RendererD3D11::open_frame_for_pass(std::uint64_t color_key, std::uint8_t channels, std::uint64_t depth_key,
                                        TextureStore& textures) {
    end_scene_pass();
    const FrameExtent size = frame_pass_extent(scene_map_);
    if (color_key != 0u) {
        resolve_scene_color();
        (void)publish_picture(color_key, size.width, size.height, scene_.color, channels, textures);
    }
    if (depth_key != 0u) {
        resolve_scene_depth();
        (void)publish_picture(depth_key, size.width, size.height, scene_.depth, 0u, textures);
    }
    frame_pass_color_key_ = color_key;
    frame_pass_depth_key_ = depth_key;
}

void RendererD3D11::close_frame_for_pass(TextureStore& textures) {
    end_scene_pass();
    for (const std::uint64_t key : {frame_pass_color_key_, frame_pass_depth_key_}) {
        if (key == 0u) {
            continue;
        }
        if (const auto it = pictures_.find(key); it != pictures_.end()) {
            drop_picture(it->second, textures);
            pictures_.erase(it);
        }
    }
    frame_pass_color_key_ = 0u;
    frame_pass_depth_key_ = 0u;
    reset_draw_bindings();
}

// The frame so far becomes a picture, and `fresh` (or a new target like it) the scene target's colour.
gpu::TextureRef RendererD3D11::swap_scene_color(gpu::TextureRef fresh) {
    const GpuPassScope scope{*this, GpuPass::DisplayCopy};
    end_scene_pass();
    resolve_scene_color();
    const d3d11::Texture& picture = *scene_.color;
    if (fresh && (fresh->width != picture.width || fresh->height != picture.height || fresh->format != picture.format ||
                  fresh->samples != 1u || !fresh->target || !fresh->view)) {
        retire(std::move(fresh));
    }
    if (!fresh) {
        fresh = d3d11::Context::get().create_texture(picture.width, picture.height, 1u, picture.format, kTargetBinds,
                                                     "scene target color");
        if (!fresh) {
            throw std::runtime_error("scene target colour unavailable");
        }
    }
    gpu::TextureRef handed = std::move(scene_.color);
    scene_.color = std::move(fresh);
    reset_draw_bindings();
    bind_scene();
    return handed;
}

gpu::TextureRef RendererD3D11::take_frame_picture(gpu::TextureRef fresh) {
    if (scene_.wide) {
        end_wide_scene();
    }
    end_scene_pass();
    return swap_scene_color(std::move(fresh));
}

bool RendererD3D11::draw_frame_pass(const FramePassRequest& params, TextureStore& textures) {
    const GpuPassScope scope{*this, GpuPass::FramePass};
    const FrameExtent extent = frame_pass_extent(scene_map_);
    const unsigned w = extent.width;
    const unsigned h = extent.height;
    if (w == 0u || h == 0u || !scene_.color) {
        return false;
    }
    // Keyed by the texture and its channels.
    const std::uint64_t dest_key = static_cast<std::uint64_t>(params.key) | (static_cast<std::uint64_t>(params.channels) << 48u);
    FramePassTarget& dest = frame_pass_targets_[dest_key];
    if (!dest.texture || dest.width != w || dest.height != h) {
        retire(std::move(dest.texture));
        dest.texture = d3d11::Context::get().create_texture(w, h, 1u, draw_layout_.scene_color, kTargetBinds, "frame pass");
        if (!dest.texture) {
            frame_pass_targets_.erase(dest_key);
            static std::atomic<unsigned> reported{0u};
            if (reported.fetch_add(1u, std::memory_order_relaxed) < 8u) {
                std::cerr << "[frame-pass] destination allocation failed size=" << w << 'x' << h << "; copy skipped\n";
            }
            return false;
        }
        dest.width = w;
        dest.height = h;
    }
    const FramePassGeometry sampling = frame_pass_geometry(w, h, scene_map_);
    record_conversion_blit(*dest.texture, w, h, params.channels, sampling.src_x, sampling.src_y, sampling.step_x, sampling.step_y);
    return publish_picture(params.key, w, h, dest.texture, 0u, textures);
}

// ---- the renderer's parts ----

// SMAA or FXAA of the 3D scene in the scene target, before the 2D layer draws (renderer_d3d12.cpp record_scene_aa).
void RendererD3D11::record_scene_aa() {
    if (!post_.ready() || !scene_.color || post_.settings().aa == PostProcessSettings::Aa::kOff) {
        return;
    }
    const GpuPassScope scope{*this, GpuPass::SceneAa};
    end_scene_pass();
    resolve_scene_color();
    // The picture the display copy takes, against its size on screen, decides AUTO.
    const FrameMap shown = output_frame_for_window();
    const d3d11::Texture& color = *scene_.color;
    (void)post_.record_scene_aa(context(), color, scene_.rtv(), color.width, color.height, scene_map_.width, scene_map_.height,
                                std::max(shown.width, 1u), std::max(shown.height, 1u));
    scene_.color_dirty |= scene_.samples > 1u;
    reset_draw_bindings();
}

// The occlusion of the opaque scene (renderer_d3d12.cpp record_scene_ao): it reads the single-sample depth and
// multiplies into the scene target's colour.
void RendererD3D11::record_scene_ao() {
    if (!ao_.ready() || !scene_.depth || !scene_.color) {
        return;
    }
    const GpuPassScope scope{*this, GpuPass::SceneAo};
    end_scene_pass();
    resolve_scene_depth();
    d3d11::AmbientOcclusion::Inputs in{};
    in.depth = scene_.depth.get();
    in.width = scene_.depth->width;
    in.height = scene_.depth->height;
    in.color = scene_.rtv();
    in.samples = scene_.samples;
    in.hdr = scene_.wide;
    in.ssao = get_render_config().ambient_occlusion_ssao;
    in.objects = &scene_ao_objects_;
    in.ring = &matrix_ring_;
    in.view = scene_ao_view_;
    (void)ao_.record(context(), in);
    scene_.color_dirty |= scene_.samples > 1u;
    reset_draw_bindings();
}

// The game's bloom on the scene target's single-sample colour (MSAA resolves it first); the composite lands on every
// sample of a multisampled scene target (renderer_d3d12.cpp game_bloom).
void RendererD3D11::game_bloom(std::uint32_t kind, std::uint32_t a, std::uint32_t b) {
    if (!bloom_.ready() || !scene_.color) {
        return;
    }
    const float strength = static_cast<float>(std::min(get_render_config().bloom, kMaxBloom)) / 100.0f;
    if (kind == NEBULA_PASS_BLOOM_SIMPLE && scene_.samples <= 1u &&
        bloom::adds_nothing(a & 0xFFu, (a >> 8u) & 0xFFu, (a >> 16u) & 0xFFu, a >> 24u, strength)) {
        return;  // a simple bloom that adds nothing leaves the frame as it is
    }
    const GpuPassScope scope{*this, GpuPass::Bloom};
    end_scene_pass();
    resolve_scene_color();
    ID3D11DeviceContext1* const ctx = context();
    d3d11::GameBloom::Inputs in{};
    in.scene_target = scene_.color.get();
    in.rtv = scene_.rtv();
    in.samples = scene_.samples;
    in.hdr = scene_.wide;
    in.scale = static_cast<float>(scene_map_.height / kAuthoredLines);
    in.frame_width = std::min(scene_map_.width, scene_.color->width);
    in.frame_height = std::min(scene_map_.height, scene_.color->height);
    if (kind == NEBULA_PASS_BLOOM_HOLD) {
        if (scene_.samples <= 1u) {
            // The scene and the bloom's image trade places instead of the scene being copied. The game draws the bloom's
            // objects on black into the new target, over the same depth.
            gpu::TextureRef picture = swap_scene_color(bloom_.take_kept_image(ctx, in));
            bloom_.keep_picture(std::move(picture), in);
            clear_scene(0u, true, true, false);
            return;
        }
        (void)bloom_.keep(ctx, in);
        reset_draw_bindings();
        return;
    }
    d3d11::GameBloom::Params p{};
    p.threshold = a & 0xFFu;
    p.intensity = a >> 24u;
    if (kind == NEBULA_PASS_BLOOM_NORMAL) {
        p.kind = d3d11::GameBloom::Kind::Normal;
        p.tap1 = (a >> 8u) & 0xFFu;
        p.tap2 = (a >> 16u) & 0xFFu;
        p.ring_x = std::bit_cast<float>(b);
    } else {
        p.kind = d3d11::GameBloom::Kind::Simple;
        p.channel = (a >> 8u) & 0xFFu;
        p.tap1 = (a >> 16u) & 0xFFu;
        p.ring_y = std::bit_cast<float>(b);
    }
    if (!(p.ring_x > 0.0f && p.ring_x < 16.0f) || !(p.ring_y > 0.0f && p.ring_y < 16.0f)) {
        p.ring_x = p.ring_y = 1.0f;
    }
    p.strength = strength;
    if (kind == NEBULA_PASS_BLOOM_NORMAL && scene_.samples <= 1u &&
        bloom::adds_nothing(p.threshold, 0u, p.tap1, p.intensity, p.strength) && bloom_.kept_by_trade(in)) {
        // The normal bloom adds nothing and its scene was kept by a trade: the scene trades back.
        bloom_.set_aside(swap_scene_color(bloom_.take_kept_back()), in);
        return;
    }
    // The simple bloom's source is the frame: the scene and the source trade places instead of the scene being copied,
    // where the composite then writes the whole target from it.
    if (kind == NEBULA_PASS_BLOOM_SIMPLE && scene_.samples <= 1u && in.frame_width == scene_.color->width &&
        in.frame_height == scene_.color->height && bloom_.can_take_source(in)) {
        bloom_.give_source(swap_scene_color(bloom_.take_source_image(ctx, in)), in);
        in.scene_target = scene_.color.get();
        in.rtv = scene_.rtv();
    }
    if (bloom_.apply(ctx, in, p)) {
        scene_.color_dirty |= scene_.samples > 1u;
    }
    reset_draw_bindings();
}

// The wide scene: the 3D scene in a wide colour target over the scene target's depth, its camera at the end
// (renderer_d3d12.cpp begin_wide_scene, end_wide_scene).
bool RendererD3D11::begin_wide_scene(bool wanted, const SceneClearValues* initial_clear) {
    if (!wanted || scene_.wide || !scene_camera_.ready() || !scene_.color || output_scene_active_ || target_pass_active_) {
        return false;
    }
    end_scene_pass();
    wide_scene_used_frame_ = frame_index_;
    SceneSet& set = wide_scene_;
    const std::uint32_t width = scene_.color->width;
    const std::uint32_t height = scene_.color->height;
    if (!set.color || set.color->width != width || set.color->height != height || set.samples != scene_.samples) {
        retire(std::move(set.color));
        retire(std::move(set.ms_color));
        d3d11::Context& device = d3d11::Context::get();
        gpu::TextureRef color = device.create_texture(width, height, 1u, draw_layout_.wide_color, kTargetBinds, "wide scene color");
        gpu::TextureRef ms_color;
        if (scene_.samples > 1u) {
            ms_color = device.create_texture(width, height, 1u, draw_layout_.wide_color, kTargetBinds, "wide scene color MSAA",
                                             scene_.samples);
        }
        if (!color || (scene_.samples > 1u && !ms_color)) {
            std::cerr << "[scene-camera] wide scene " << width << 'x' << height << " allocation failed\n";
            return false;
        }
        set.color = std::move(color);
        set.ms_color = std::move(ms_color);
        set.samples = scene_.samples;
        set.wide = true;
    }
    set.depth = scene_.depth;
    set.ms_depth = scene_.ms_depth;
    const GpuPassScope scope{*this, GpuPass::Camera};
    if (initial_clear != nullptr) {
        // A full clear right before the first scene draw has no picture to carry: the wide colour and the shared depth
        // clear together.
        set.depth_dirty = scene_.depth_dirty;
        std::swap(scene_, set);
        context()->ClearRenderTargetView(scene_.rtv(), initial_clear->color.data());
        context()->ClearDepthStencilView(scene_.dsv(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, initial_clear->reversed_depth, 0u);
        scene_.color_dirty = scene_.samples > 1u;
        scene_.depth_dirty = scene_.samples > 1u;
        reset_draw_bindings();
        bind_scene();
        return true;
    }
    // What the scene target holds goes on under the scene.
    resolve_scene_color();
    d3d11::SceneCamera::WidenInputs in{};
    in.picture = scene_.color.get();
    in.rtv = set.rtv();
    in.width = width;
    in.height = height;
    in.samples = scene_.samples;
    (void)scene_camera_.widen(context(), in);
    set.color_dirty = scene_.samples > 1u;
    set.depth_dirty = scene_.depth_dirty;
    std::swap(scene_, set);
    reset_draw_bindings();
    bind_scene();
    return true;
}

void RendererD3D11::end_wide_scene() {
    if (!scene_.wide) {
        return;
    }
    const GpuPassScope scope{*this, GpuPass::Camera};
    end_scene_pass();
    resolve_scene_depth();
    resolve_scene_color();
    SceneSet& set = wide_scene_;  // the scene target's own set, waiting
    std::swap(scene_, set);
    scene_.depth_dirty = set.depth_dirty;
    const bool resolve_output = set.samples > 1u && output_frame_for_window() == scene_map_ && ensure_output_set(scene_map_);
    if (resolve_output) {
        // All 3D samples reach the tone map, whose averaged result goes straight to the single-sample output scene target.
        swap_scene_sets();
        const SceneClearValues far_values = make_scene_clear_values(0u, 0u);
        context()->ClearDepthStencilView(scene_.dsv(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, far_values.reversed_depth, 0u);
    }
    const std::int64_t now = galaxy::qpc_now();
    const float seconds = camera_metered_qpc_ != 0
                              ? static_cast<float>(static_cast<double>(now - camera_metered_qpc_) /
                                                   static_cast<double>(galaxy::qpc_frequency()))
                              : 2.0f;
    camera_metered_qpc_ = now;
    d3d11::SceneCamera::Inputs in{};
    in.wide = set.color.get();
    in.wide_ms = set.samples > 1u ? set.ms_color.get() : nullptr;
    in.depth = set.depth.get();
    in.rtv = scene_.rtv();
    in.width = set.color->width;
    in.height = set.color->height;
    in.samples = set.samples;
    in.resolve = resolve_output;
    // After a load or a long pause the eye starts open.
    in.seconds = seconds > 1.0f ? -1.0f : seconds;
    (void)scene_camera_.record(context(), in);
    scene_.color_dirty |= scene_.samples > 1u;
    reset_draw_bindings();
    bind_scene();
}

// ---- screen probes ----

bool RendererD3D11::record_screen_probes(std::size_t slot, std::span<const ScreenProbe> probes, bool) {
    if (slot >= probe_slots_.size()) {
        probe_slots_.resize(slot + 1u);
    }
    ProbeSlot& out = probe_slots_[slot];
    if (out.fence != 0u || probes.empty() || !scene_.depth) {
        return false;
    }
    const auto count = static_cast<std::uint32_t>(probes.size());
    ID3D11Device1* const device = d3d11::Context::get().device();
    if (out.capacity < count) {
        out = ProbeSlot{};
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = count * sizeof(ScreenProbePixel);
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        desc.StructureByteStride = sizeof(ScreenProbePixel);
        bool made = SUCCEEDED(device->CreateBuffer(&desc, nullptr, &out.points)) &&
                    SUCCEEDED(device->CreateShaderResourceView(out.points.Get(), nullptr, &out.points_view));
        desc.ByteWidth = count * sizeof(float);
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
        desc.CPUAccessFlags = 0u;
        desc.StructureByteStride = sizeof(float);
        made = made && SUCCEEDED(device->CreateBuffer(&desc, nullptr, &out.values)) &&
               SUCCEEDED(device->CreateUnorderedAccessView(out.values.Get(), nullptr, &out.values_view));
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0u;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.MiscFlags = 0u;
        desc.StructureByteStride = 0u;
        made = made && SUCCEEDED(device->CreateBuffer(&desc, nullptr, &out.readback));
        if (!made) {
            out = ProbeSlot{};
            throw std::runtime_error("the screen probe buffers could not be made");
        }
        out.capacity = count;
    }
    ID3D11DeviceContext1* const ctx = context();
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(ctx->Map(out.points.Get(), 0u, D3D11_MAP_WRITE_DISCARD, 0u, &mapped))) {
        return false;
    }
    for (std::uint32_t i = 0; i < count; ++i) {
        const ScreenProbePixel pixel = screen_probe_pixel(probes[i], scene_map_);
        std::memcpy(static_cast<std::uint8_t*>(mapped.pData) + i * sizeof(ScreenProbePixel), &pixel, sizeof(pixel));
    }
    ctx->Unmap(out.points.Get(), 0u);

    end_scene_pass();
    resolve_scene_depth();  // depth alone
    if (SUCCEEDED(ctx->Map(utility_constants_.Get(), 0u, D3D11_MAP_WRITE_DISCARD, 0u, &mapped))) {
        std::memcpy(mapped.pData, &count, sizeof(count));
        ctx->Unmap(utility_constants_.Get(), 0u);
    }
    ID3D11Buffer* const constants = utility_constants_.Get();
    ID3D11ShaderResourceView* const views[2] = {scene_.depth->view.Get(), out.points_view.Get()};
    ID3D11UnorderedAccessView* const values = out.values_view.Get();
    ctx->CSSetShader(probe_cs_.Get(), nullptr, 0u);
    ctx->CSSetConstantBuffers(0u, 1u, &constants);
    ctx->CSSetShaderResources(0u, 2u, views);
    ctx->CSSetUnorderedAccessViews(0u, 1u, &values, nullptr);
    ctx->Dispatch((count + kScreenProbeGroup - 1u) / kScreenProbeGroup, 1u, 1u);
    ID3D11ShaderResourceView* const no_views[2] = {};
    ID3D11UnorderedAccessView* const no_values = nullptr;
    ctx->CSSetShaderResources(0u, 2u, no_views);
    ctx->CSSetUnorderedAccessViews(0u, 1u, &no_values, nullptr);
    const D3D11_BOX box{0u, 0u, 0u, count * static_cast<UINT>(sizeof(float)), 1u, 1u};
    ctx->CopySubresourceRegion(out.readback.Get(), 0u, 0u, 0u, 0u, out.values.Get(), 0u, &box);
    out.count = count;
    out.fence = kProbesInFrame;  // fenced at end_frame
    return true;
}

const float* RendererD3D11::screen_probes_finished(std::size_t slot) {
    if (slot >= probe_slots_.size()) {
        return nullptr;
    }
    ProbeSlot& out = probe_slots_[slot];
    if (out.fence == 0u || out.fence == kProbesInFrame || completed_fence_value() < out.fence) {
        return nullptr;
    }
    out.fence = 0u;
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context()->Map(out.readback.Get(), 0u, D3D11_MAP_READ, 0u, &mapped))) {
        return nullptr;
    }
    const auto* values = static_cast<const float*>(mapped.pData);
    out.answers.assign(values, values + out.count);
    context()->Unmap(out.readback.Get(), 0u);
    return out.answers.data();
}

// ---- test captures ----

void RendererD3D11::set_debug_capture_paths(std::string scene_ppm_path, std::string backbuffer_ppm_path) {
    scene_capture_path_ = std::move(scene_ppm_path);
    backbuffer_capture_path_ = std::move(backbuffer_ppm_path);
}

bool RendererD3D11::debug_copy_scene_to_readback() {
    debug_log_readback();
    if (scene_capture_path_.empty() || !scene_capture_pending_path_.empty() || !scene_.color ||
        scene_.color->format != draw_layout_.scene_color) {
        return false;
    }
    const unsigned w = std::min(scene_map_.width, scene_.color->width);
    const unsigned h = std::min(scene_map_.height, scene_.color->height);
    end_scene_pass();
    resolve_scene_color();
    if (!ensure_staging(scene_capture_.staging, w, h, scene_.color->format)) {
        return false;
    }
    const D3D11_BOX box{0u, 0u, 0u, w, h, 1u};
    context()->CopySubresourceRegion(scene_capture_.staging.Get(), 0u, 0u, 0u, 0u, scene_.color->texture.Get(), 0u, &box);
    scene_capture_.fence = 0u;
    scene_capture_pending_path_ = scene_capture_path_;
    scene_capture_width_ = w;
    scene_capture_height_ = h;
    return true;
}

void RendererD3D11::debug_log_readback() {
    if (scene_capture_pending_path_.empty() || scene_capture_.fence == 0u || completed_fence_value() < scene_capture_.fence) {
        return;
    }
    const std::vector<std::uint8_t> rgba =
        read_staging(context(), scene_capture_.staging.Get(), scene_capture_width_, scene_capture_height_);
    if (!rgba.empty()) {
        write_ppm_async(std::move(scene_capture_pending_path_), rgba, scene_capture_width_, scene_capture_height_);
    }
    scene_capture_pending_path_.clear();
    scene_capture_.fence = 0u;
}

bool RendererD3D11::debug_copy_selected_display_to_readback(const DisplayTexture& source) {
    if (backbuffer_capture_path_.empty() || !display_capture_pending_path_.empty() || !source.texture || source.width == 0u ||
        source.height == 0u) {
        return false;
    }
    const d3d11::Texture& texture = *source.texture;
    // The picture's rows (a handed-over scene target is taller); the formats the display pictures have.
    if ((texture.format != DXGI_FORMAT_R10G10B10A2_UNORM && texture.format != DXGI_FORMAT_R8G8B8A8_UNORM) ||
        texture.width < source.width || texture.height < source.height || texture.samples != 1u) {
        return false;
    }
    if (!ensure_staging(display_capture_.staging, source.width, source.height, texture.format)) {
        return false;
    }
    end_scene_pass();
    const D3D11_BOX box{0u, 0u, 0u, source.width, source.height, 1u};
    context()->CopySubresourceRegion(display_capture_.staging.Get(), 0u, 0u, 0u, 0u, texture.texture.Get(), 0u, &box);
    display_capture_.fence = 0u;
    display_capture_pending_path_ = backbuffer_capture_path_ + ".display.ppm";
    display_capture_width_ = source.width;
    display_capture_height_ = source.height;
    return true;
}

void RendererD3D11::debug_log_display_readback() {
    if (display_capture_pending_path_.empty() || display_capture_.fence == 0u || completed_fence_value() < display_capture_.fence) {
        return;
    }
    const std::vector<std::uint8_t> rgba =
        read_staging(context(), display_capture_.staging.Get(), display_capture_width_, display_capture_height_);
    if (!rgba.empty()) {
        write_ppm_async(std::move(display_capture_pending_path_), rgba, display_capture_width_, display_capture_height_);
    }
    display_capture_pending_path_.clear();
    display_capture_.fence = 0u;
}

void RendererD3D11::debug_log_backbuffer_readback() {
    debug_log_display_readback();
    if (backbuffer_capture_pending_path_.empty() || backbuffer_capture_.fence == 0u ||
        completed_fence_value() < backbuffer_capture_.fence) {
        return;
    }
    const std::vector<std::uint8_t> rgba =
        read_staging(context(), backbuffer_capture_.staging.Get(), backbuffer_capture_width_, backbuffer_capture_height_);
    if (!rgba.empty()) {
        write_ppm_async(std::move(backbuffer_capture_pending_path_), rgba, backbuffer_capture_width_, backbuffer_capture_height_);
    }
    backbuffer_capture_pending_path_.clear();
    backbuffer_capture_.fence = 0u;
}

}  // namespace galaxy::render
