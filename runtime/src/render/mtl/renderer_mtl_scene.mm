// SPDX-License-Identifier: GPL-3.0-only
// RendererMtl: the scene target's sets, the scene pass, clears, target passes and pictures, the renderer's own
// pipelines. As vk/renderer_vk_frame.cpp's first half; where Vulkan changes an image's layout Metal needs nothing.
#include "renderer_mtl_internal.h"

#include "galaxy/experimental_ultrawide_aspect.h"
#include "galaxy/platform_time.h"
#include "galaxy/render/texture_store.h"
#include "render/core/frame_core.h"

#include <nebula_frame_list.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <iostream>
#include <stdexcept>

namespace galaxy::render {

using namespace mtl_detail;

std::size_t RendererMtl::UtilityKeyHasher::operator()(const UtilityKey& key) const noexcept {
    std::uint64_t hash = 1469598103934665603ull;
    const auto mix = [&hash](std::uint64_t value) {
        hash ^= value;
        hash *= 1099511628211ull;
    };
    mix(static_cast<std::uint64_t>(key.vertex));
    mix(static_cast<std::uint64_t>(key.fragment));
    for (const mtl::Format format : key.colors) {
        mix(static_cast<std::uint64_t>(format));
    }
    mix(key.color_count);
    mix(key.write_mask);
    mix(key.samples);
    mix(key.depth ? 1u : 0u);
    return static_cast<std::size_t>(hash);
}

// ---- the renderer's own pipelines ----

mtl::Object RendererMtl::utility_function(mtl::UtilityShader shader) {
    mtl::Object& function = utility_functions_[static_cast<std::size_t>(shader)];
    if (function != nullptr) {
        return function;
    }
    @autoreleasepool {
        MTLCompileOptions* const options = [MTLCompileOptions new];
        options.fastMathEnabled = NO;
        options.languageVersion = MTLLanguageVersion2_4;
        NSError* error = nil;
        id<MTLLibrary> const library = [metal_device() newLibraryWithSource:@(mtl::utility_msl(shader)) options:options error:&error];
        id<MTLFunction> const made = library != nil ? [library newFunctionWithName:@"main0"] : nil;
        if (made == nil) {
            throw std::runtime_error(std::string("a renderer shader did not compile: ") +
                                     (error != nil ? error.localizedDescription.UTF8String : "no main0"));
        }
        function = own(made);
    }
    return function;
}

mtl::Object RendererMtl::utility_pipeline(const UtilityKey& key) {
    if (const auto it = utility_pipelines_.find(key); it != utility_pipelines_.end()) {
        return it->second;
    }
    @autoreleasepool {
        MTLRenderPipelineDescriptor* const descriptor = [MTLRenderPipelineDescriptor new];
        descriptor.vertexFunction = (__bridge id<MTLFunction>)utility_function(key.vertex);
        descriptor.fragmentFunction = (__bridge id<MTLFunction>)utility_function(key.fragment);
        for (unsigned i = 0; i < key.color_count; ++i) {
            descriptor.colorAttachments[i].pixelFormat = static_cast<MTLPixelFormat>(key.colors[i]);
            descriptor.colorAttachments[i].writeMask = i == 0u ? key.write_mask : MTLColorWriteMaskNone;
        }
        if (key.depth) {
            descriptor.depthAttachmentPixelFormat = static_cast<MTLPixelFormat>(draw_layout_.depth);
            descriptor.stencilAttachmentPixelFormat = static_cast<MTLPixelFormat>(draw_layout_.depth);
        }
        descriptor.rasterSampleCount = key.samples;
        NSError* error = nil;
        id<MTLRenderPipelineState> const made = [metal_device() newRenderPipelineStateWithDescriptor:descriptor error:&error];
        if (made == nil) {
            throw std::runtime_error(std::string("a renderer pipeline could not be made: ") +
                                     (error != nil ? error.localizedDescription.UTF8String : ""));
        }
        const mtl::Object pipeline = own(made);
        utility_pipelines_.emplace(key, pipeline);
        return pipeline;
    }
}

mtl::Object RendererMtl::scene_pass_pipeline(mtl::UtilityShader fragment, std::uint8_t write_mask) {
    UtilityKey key;
    key.vertex = mtl::UtilityShader::BlitVs;
    key.fragment = fragment;
    key.colors[0] = target_pass_active_ ? draw_layout_.target_color : scene_.wide ? draw_layout_.wide_color : draw_layout_.scene_color;
    key.color_count = 1u;
    const unsigned aux = scene_pass_aux_;
    if (aux == 1u) {
        key.colors[key.color_count++] = draw_layout_.motion;
    } else if (aux == 2u || aux == 3u) {
        key.colors[key.color_count++] = draw_layout_.reactive;
    }
    if (aux == 3u) {
        key.colors[key.color_count++] = draw_layout_.layer;
        key.colors[key.color_count++] = draw_layout_.layer;
    }
    key.write_mask = write_mask;
    key.samples = static_cast<std::uint8_t>(aux != 0u ? 1u : scene_.samples);
    key.depth = true;
    return utility_pipeline(key);
}

void RendererMtl::utility_draw(mtl::Object encoder, mtl::Object pipeline, DepthUse depth, std::uint32_t width,
                               std::uint32_t height, const gpu::Rect& scissor, const void* constants,
                               std::size_t constant_bytes, mtl::Object texture, const gpu::Rect* viewport) {
    const std::int64_t left = std::clamp<std::int64_t>(scissor.left, 0, width);
    const std::int64_t top = std::clamp<std::int64_t>(scissor.top, 0, height);
    const std::int64_t right = std::clamp<std::int64_t>(scissor.right, left, width);
    const std::int64_t bottom = std::clamp<std::int64_t>(scissor.bottom, top, height);
    if (right <= left || bottom <= top) {
        return;
    }
    id<MTLRenderCommandEncoder> const pass = pass_of(encoder);
    [pass setRenderPipelineState:(__bridge id<MTLRenderPipelineState>)pipeline];
    [pass setDepthStencilState:(__bridge id<MTLDepthStencilState>)depth_uses_[static_cast<std::size_t>(depth)]];
    [pass setCullMode:MTLCullModeNone];
    [pass setTriangleFillMode:MTLTriangleFillModeFill];
    [pass setDepthClipMode:MTLDepthClipModeClip];
    [pass setDepthBias:0.0f slopeScale:0.0f clamp:0.0f];
    if (viewport != nullptr) {
        [pass setViewport:MTLViewport{static_cast<double>(viewport->left), static_cast<double>(viewport->top),
                                      static_cast<double>(viewport->right - viewport->left),
                                      static_cast<double>(viewport->bottom - viewport->top), 0.0, 1.0}];
    } else {
        [pass setViewport:MTLViewport{0.0, 0.0, static_cast<double>(width), static_cast<double>(height), 0.0, 1.0}];
    }
    [pass setScissorRect:MTLScissorRect{static_cast<NSUInteger>(left), static_cast<NSUInteger>(top),
                                        static_cast<NSUInteger>(right - left), static_cast<NSUInteger>(bottom - top)}];
    if (constants != nullptr && constant_bytes != 0u) {
        // The shaders' constants are one block of 64 bytes at most, read by either function.
        std::uint8_t block[64]{};
        std::memcpy(block, constants, std::min(constant_bytes, sizeof(block)));
        [pass setVertexBytes:block length:sizeof(block) atIndex:mtl::kUtilityPushBuffer];
        [pass setFragmentBytes:block length:sizeof(block) atIndex:mtl::kUtilityPushBuffer];
    }
    if (texture != nullptr) {
        [pass setFragmentTexture:texture_of(texture) atIndex:0];
        [pass setFragmentSamplerState:(__bridge id<MTLSamplerState>)linear_clamp_ atIndex:1];
    }
    [pass drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
}

mtl::Object RendererMtl::begin_color_pass(mtl::Object color, bool clear, const char* name) {
    @autoreleasepool {
        MTLRenderPassDescriptor* const descriptor = [MTLRenderPassDescriptor renderPassDescriptor];
        descriptor.colorAttachments[0].texture = texture_of(color);
        descriptor.colorAttachments[0].loadAction = clear ? MTLLoadActionClear : MTLLoadActionDontCare;
        descriptor.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 1.0);
        descriptor.colorAttachments[0].storeAction = MTLStoreActionStore;
        id<MTLRenderCommandEncoder> const pass = [commands_of(commands_) renderCommandEncoderWithDescriptor:descriptor];
        if (name != nullptr) {
            pass.label = @(name);
        }
        return own(pass);
    }
}

void RendererMtl::end_own_pass(mtl::Object encoder) {
    [pass_of(encoder) endEncoding];
    mtl::release(encoder);
}

void RendererMtl::clear_textures(const mtl::Texture* const* textures, const float (*values)[4], unsigned count) {
    @autoreleasepool {
        MTLRenderPassDescriptor* const descriptor = [MTLRenderPassDescriptor renderPassDescriptor];
        for (unsigned i = 0; i < count; ++i) {
            descriptor.colorAttachments[i].texture = texture_of(*textures[i]);
            descriptor.colorAttachments[i].loadAction = MTLLoadActionClear;
            descriptor.colorAttachments[i].clearColor = MTLClearColorMake(values[i][0], values[i][1], values[i][2], values[i][3]);
            descriptor.colorAttachments[i].storeAction = MTLStoreActionStore;
        }
        id<MTLRenderCommandEncoder> const pass = [commands_of(commands_) renderCommandEncoderWithDescriptor:descriptor];
        pass.label = @"clear";
        [pass endEncoding];
    }
}

// ---- scene sets ----

bool RendererMtl::create_scene_set(SceneSet& set, const FrameMap& map, unsigned samples, unsigned set_width,
                                   unsigned set_height, bool target) {
    mtl::Context& context = mtl::Context::get();
    const auto width = static_cast<std::uint32_t>(std::lround(static_cast<double>(set_width) * map.sx));
    const auto height = static_cast<std::uint32_t>(std::lround(static_cast<double>(set_height) * map.sy));
    gpu::TextureRef color = context.create_texture(
        width, height, 1u, target ? draw_layout_.target_color : draw_layout_.scene_color,
        mtl::kUseTarget | mtl::kUseSample | (scene_set_storage_ ? mtl::kUseWrite : 0u), mtl::Storage::Private,
        "scene target color");
    gpu::TextureRef depth = context.create_texture(width, height, 1u, draw_layout_.depth, mtl::kUseTarget | mtl::kUseSample,
                                                   mtl::Storage::Private, "scene target depth");
    if (!color || !depth) {
        std::cerr << "[Renderer] scene target " << width << 'x' << height << " allocation failed\n";
        return false;
    }
    gpu::TextureRef ms_color;
    gpu::TextureRef ms_depth;
    if (samples > 1u) {
        ms_color = context.create_texture(width, height, 1u, draw_layout_.scene_color, mtl::kUseTarget, mtl::Storage::Private,
                                          "scene target color MSAA", samples);
        ms_depth = context.create_texture(width, height, 1u, draw_layout_.depth, mtl::kUseTarget | mtl::kUseSample,
                                          mtl::Storage::Private, "scene target depth MSAA", samples);
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
    set.depth_serial = ++scene_depth_serial_;
    set.wide = false;
    return true;
}

// Releases a set's targets once the GPU is past the commands recorded so far.
void RendererMtl::destroy_scene_set(SceneSet& set) {
    retire(std::move(set.color));
    retire(std::move(set.depth));
    retire(std::move(set.ms_color));
    retire(std::move(set.ms_depth));
    set.samples = 1u;
}

bool RendererMtl::create_scene_targets(const FrameMap& map, unsigned samples) {
    SceneSet made;
    if (!create_scene_set(made, map, samples)) {
        return false;
    }
    if (scene_.color) {
        destroy_scene_set(scene_);
    }
    scene_ = std::move(made);
    scene_map_ = map;
    pending_depth_clear_ = 0u;
    if (scene_.samples > 1u) {
        std::cout << "[Renderer] scene target MSAA " << scene_.samples << "x\n";
    }
    return true;
}

unsigned RendererMtl::wanted_msaa_samples() const {
    // A temporal upscaler antialiases the scene itself.
    if (temporal_mode()) {
        return 1u;
    }
    return std::min(std::bit_floor(std::clamp(get_render_config().msaa_samples, 1u, 8u)), draw_layout_.max_samples);
}

// Before the scene target's single-sample depth is read: the multisampled depth drawn since the last resolve, each
// pixel's first sample. The colour resolves as each scene pass ends. The scene pass is closed.
void RendererMtl::resolve_scene_depth() {
    SceneSet& set = scene_;
    if (set.samples <= 1u || !set.depth_dirty) {
        return;
    }
    const GpuPassScope scope{*this, GpuPass::Resolve};
    @autoreleasepool {
        MTLRenderPassDescriptor* const descriptor = [MTLRenderPassDescriptor renderPassDescriptor];
        descriptor.depthAttachment.texture = texture_of(set.depth);
        descriptor.depthAttachment.loadAction = MTLLoadActionDontCare;
        descriptor.depthAttachment.storeAction = MTLStoreActionStore;
        descriptor.stencilAttachment.texture = texture_of(set.depth);
        descriptor.stencilAttachment.loadAction = MTLLoadActionDontCare;
        descriptor.stencilAttachment.storeAction = MTLStoreActionDontCare;
        id<MTLRenderCommandEncoder> const pass = [commands_of(commands_) renderCommandEncoderWithDescriptor:descriptor];
        pass.label = @"depth resolve";
        UtilityKey key;
        key.fragment = mtl::UtilityShader::DepthResolvePs;
        key.depth = true;
        const std::uint32_t width = set.depth->width;
        const std::uint32_t height = set.depth->height;
        utility_draw((__bridge void*)pass, utility_pipeline(key), DepthUse::Write, width, height,
                     gpu::rect(0, 0, static_cast<std::int32_t>(width), static_cast<std::int32_t>(height)), nullptr, 0u,
                     set.ms_depth->texture);  // the shader loads each pixel's first sample
        [pass endEncoding];
    }
    set.depth_dirty = false;
    reset_draw_bindings();
}

bool RendererMtl::video_memory(std::uint64_t& usage, std::uint64_t& budget) const noexcept {
    return mtl::Context::get().device_memory_budget(usage, budget);
}

// The scale-dependent memory a new internal resolution adds must fit 90% of the device's budget (the other
// renderers' rule): scene colour (4 bytes a pixel), depth with stencil (8) and, at most, the display picture pair plus
// one full-picture copy (12).
bool RendererMtl::scene_fits_memory(const FrameMap& map) const {
    std::uint64_t usage = 0u;
    std::uint64_t budget = 0u;
    if (!mtl::Context::get().device_memory_budget(usage, budget) || budget == 0u) {
        return true;
    }
    const auto bytes = [](const FrameMap& m) { return (4.0 + 8.0 + 12.0) * m.width * m.height; };
    const double needed = bytes(map) - bytes(scene_map_);
    return static_cast<double>(usage) + needed <= 0.9 * static_cast<double>(budget);
}

// A changed internal resolution, between frames: the GPU finishes with the old targets and every scaled copy.
void RendererMtl::apply_window_size() {
    if (scene_.wide) {
        return;
    }
    if (host_window::move_size_active.load(std::memory_order_acquire)) {
        return;
    }
    const FrameMap output = output_frame_for_window();
    if (output.empty()) {
        return;
    }
    FrameMap wanted = output;
    upscale_output_ = wanted;
    const auto config = get_render_config();
    const auto [render_width, render_height] = internal_extent(
        wanted.width, wanted.height, render_internal_height(config), mtl::Context::get().caps().max_texture_size,
        host_window::upscaler_render_fraction(config, wanted.width, wanted.height));
    wanted = FrameMap(render_width, render_height);
    const unsigned samples = wanted_msaa_samples();
    // Not while the output scene target holds the scene target's place (its scale is the window's).
    if ((wanted == scene_map_ && samples == scene_.samples) || !scene_.color || output_scene_active_) {
        return;
    }
    if (wanted.width * wanted.height > scene_map_.width * scene_map_.height && !scene_fits_memory(wanted)) {
        if (galaxy::steady_ms() >= window_size_refused_ms_) {
            window_size_refused_ms_ = galaxy::steady_ms() + 10'000u;
            std::cerr << "[Renderer] " << wanted.width << "x" << wanted.height << " exceeds the video-memory budget; staying at "
                      << scene_map_.width << "x" << scene_map_.height << '\n';
        }
        return;
    }
    wait_for_gpu_idle();
    frame_pass_targets_.clear();
    for (Frame& frame : frames_) {
        frame.retired.clear();
        frame.retired_buffers.clear();
        for (mtl::Object& object : frame.retired_objects) {
            mtl::release(object);
        }
        frame.retired_objects.clear();
    }
    const FrameMap previous = scene_map_;
    if (!create_scene_targets(wanted, samples)) {
        return;
    }
    reset_draw_bindings();
    std::cout << "[Renderer] internal resolution " << previous.width << "x" << previous.height << " -> " << wanted.width << "x"
              << wanted.height << '\n';
}

// ---- the scene pass ----

mtl::Object RendererMtl::scene_pass_descriptor(unsigned aux, const SceneClearValues* clear, bool discard_color,
                                               unsigned clear_depth, mtl::Object visibility) {
    MTLRenderPassDescriptor* const descriptor = [MTLRenderPassDescriptor renderPassDescriptor];
    const bool multisampled = scene_.samples > 1u && aux == 0u;
    // Which of the pass's targets begin cleared: everything (a whole clear), the depth alone (`clear_depth`: the
    // colour loads, the stencil is kept), or nothing. A colour that is about to be replaced whole is not loaded.
    const bool depth_only = clear != nullptr && clear_depth != 0u && aux == 0u && !target_pass_active_;
    const bool drop_color = discard_color && aux == 0u && !target_pass_active_;
    const bool clear_all = clear != nullptr && !depth_only && !drop_color;
    MTLRenderPassColorAttachmentDescriptor* const color = descriptor.colorAttachments[0];
    color.texture = texture_of(multisampled ? scene_.ms_color : scene_.color);
    if (multisampled) {
        color.resolveTexture = texture_of(scene_.color);
        color.storeAction = MTLStoreActionStoreAndMultisampleResolve;
    } else {
        color.storeAction = MTLStoreActionStore;
    }
    color.loadAction = clear_all ? MTLLoadActionClear : drop_color ? MTLLoadActionDontCare : MTLLoadActionLoad;
    if (clear_all) {
        color.clearColor = MTLClearColorMake(clear->color[0], clear->color[1], clear->color[2], clear->color[3]);
    }
    id<MTLTexture> const depth = texture_of(multisampled ? scene_.ms_depth : scene_.depth);
    descriptor.depthAttachment.texture = depth;
    descriptor.depthAttachment.loadAction = clear_all || depth_only ? MTLLoadActionClear : MTLLoadActionLoad;
    descriptor.depthAttachment.clearDepth = clear != nullptr ? clear->reversed_depth : 0.0;  // reversed: the far plane is 0
    descriptor.depthAttachment.storeAction = MTLStoreActionStore;
    descriptor.stencilAttachment.texture = depth;
    descriptor.stencilAttachment.loadAction = clear_all ? MTLLoadActionClear : MTLLoadActionLoad;
    descriptor.stencilAttachment.clearStencil = 0u;
    descriptor.stencilAttachment.storeAction = MTLStoreActionStore;
    if (aux != 0u) {
        // A temporal scene's second target clears to zero with the scene; the layer's two keep what
        // begin_layer_window cleared them to.
        MTLRenderPassColorAttachmentDescriptor* const second = descriptor.colorAttachments[1];
        second.texture = texture_of(aux == 1u ? motion_.motion : motion_.reactive);
        second.loadAction = clear_all ? MTLLoadActionClear : MTLLoadActionLoad;
        second.clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 0.0);
        second.storeAction = MTLStoreActionStore;
        if (aux == 3u) {
            descriptor.colorAttachments[2].texture = texture_of(motion_.layer_color);
            descriptor.colorAttachments[2].loadAction = MTLLoadActionLoad;
            descriptor.colorAttachments[2].storeAction = MTLStoreActionStore;
            descriptor.colorAttachments[3].texture = texture_of(motion_.layer_trans);
            descriptor.colorAttachments[3].loadAction = MTLLoadActionLoad;
            descriptor.colorAttachments[3].storeAction = MTLStoreActionStore;
        }
    }
    if (visibility != nullptr) {
        descriptor.visibilityResultBuffer = buffer_of(visibility);
    }
    // The caller's now (it takes it back with a transfer).
    return (__bridge_retained void*)descriptor;
}

void RendererMtl::begin_scene_pass(const SceneClearValues* clear, const char* caller) {
    begin_scene_pass_with(0u, clear, false, caller);
}

void RendererMtl::begin_scene_pass_with(unsigned aux, const SceneClearValues* clear, bool discard_color, const char* caller,
                                        unsigned clear_depth) {
    if (scene_pass_open_) {
        if (scene_pass_aux_ == aux) {
            return;
        }
        end_scene_pass();
    }
    if (depth_clear_pending()) {
        if (clear != nullptr && (clear_depth != 0u || !discard_color || aux != 0u)) {
            pending_depth_clear_ = 0u;  // this opening clears the depth itself
        } else if (clear == nullptr && aux == 0u && !discard_color) {
            clear = &pending_depth_values_;  // the colour loads, the depth clears
            clear_depth = std::exchange(pending_depth_clear_, 0u);
        } else {
            // This opening cannot clear only the depth: one that does opens and ends first.
            const SceneClearValues values = pending_depth_values_;
            const unsigned mode = std::exchange(pending_depth_clear_, 0u);
            begin_scene_pass_with(0u, &values, false, "settle_depth_clear", mode);
            end_scene_pass("settle_depth_clear");
        }
    }
    scene_pass_aux_ = aux;
    // Loading a pass, tone mapping and occlusion compositing do not modify depth; native depth-writing draws and
    // explicit clears make the resolved depth stale.
    scene_.depth_dirty |= clear != nullptr && scene_.samples > 1u;
    // Timed: the scene pass's draws are the scene's, whichever part begins the pass again to draw its result.
    if (profile_frame_ && list_pass_ != GpuPass::Scene) {
        next_command_buffer(GpuPass::Scene);
    }
    // Points tested in this pass (test_screen_probes) put their results here; a single-sample pass only.
    if (scene_.samples == 1u && (!pass_visibility_ || pass_visibility_used_ != 0u)) {
        Frame& frame = *frame_;
        if (frame.visibility_used == frame.visibility.size()) {
            frame.visibility.push_back(mtl::Context::get().create_buffer(std::uint64_t{kPassVisibilityResults} * 8u,
                                                                        mtl::Storage::Shared, "probe results"));
        }
        pass_visibility_ = frame.visibility[frame.visibility_used++];
        pass_visibility_used_ = 0u;
    } else if (scene_.samples != 1u) {
        pass_visibility_.reset();
    }
    pass_width_ = scene_.color->width;
    pass_height_ = scene_.color->height;
    @autoreleasepool {
        MTLRenderPassDescriptor* const descriptor = (__bridge_transfer MTLRenderPassDescriptor*)scene_pass_descriptor(
            aux, clear, discard_color, clear_depth, pass_visibility_ ? pass_visibility_->buffer : nullptr);
        id<MTLRenderCommandEncoder> pass = nil;
        if (parallel_scene_pass_) {
            id<MTLParallelRenderCommandEncoder> const parallel =
                [commands_of(commands_) parallelRenderCommandEncoderWithDescriptor:descriptor];
            if (parallel != nil) {
                parallel.label = @(caller);
                scene_parallel_ = own(parallel);
                pass = [parallel renderCommandEncoder];
            }
        } else {
            pass = [commands_of(commands_) renderCommandEncoderWithDescriptor:descriptor];
        }
        if (pass == nil) {
            throw std::runtime_error("the scene pass could not begin");
        }
        pass.label = @(caller);
        pass_ = own(pass);
    }
    scene_pass_open_ = true;
    ++gpu_window_.passes;
    for (GpuWindow::Open& open : gpu_window_.opens) {
        if (open.count == 0u) {
            open = GpuWindow::Open{scene_pass_closed_by_, caller, clear != nullptr, 1u};
            break;
        }
        if (open.cleared == (clear != nullptr) && std::strcmp(open.closed, scene_pass_closed_by_) == 0 &&
            std::strcmp(open.opened, caller) == 0) {
            ++open.count;
            break;
        }
    }
    attach_scene_encoder();
}

void RendererMtl::attach_scene_encoder() {
    const unsigned aux = scene_pass_aux_;
    if (bindless_ && mirrored_store_ != nullptr) {
        // Every texture a draw may read through the array is named to the pass, but those it draws into.
        const mtl::Object targets[] = {scene_.color->texture,
                                       scene_.depth->texture,
                                       scene_.ms_color ? scene_.ms_color->texture : nullptr,
                                       scene_.ms_depth ? scene_.ms_depth->texture : nullptr,
                                       aux != 0u && motion_.motion ? motion_.motion->texture : nullptr,
                                       aux != 0u && motion_.reactive ? motion_.reactive->texture : nullptr,
                                       aux == 3u && motion_.layer_color ? motion_.layer_color->texture : nullptr,
                                       aux == 3u && motion_.layer_trans ? motion_.layer_trans->texture : nullptr};
        TextureBackend& backend = mirrored_store_->backend();
        backend.use_textures(pass_, targets, std::size(targets));
        backend.set_open_pass(pass_);
    }
    // A new encoder has nothing bound: the walker's bindings are set again by its next draw, and the scene's
    // viewport and scissor now.
    encoder_.attach(pass_, pass_width_, pass_height_);
    if (bound_.viewport_valid) {
        [pass_of(pass_) setViewport:MTLViewport{bound_.viewport.x, bound_.viewport.y, bound_.viewport.width,
                                                bound_.viewport.height, bound_.viewport.min_depth, bound_.viewport.max_depth}];
    }
    if (bound_.scissor_valid) {
        const std::int64_t left = std::clamp<std::int64_t>(bound_.scissor.left, 0, pass_width_);
        const std::int64_t top = std::clamp<std::int64_t>(bound_.scissor.top, 0, pass_height_);
        const std::int64_t right = std::clamp<std::int64_t>(bound_.scissor.right, left, pass_width_);
        const std::int64_t bottom = std::clamp<std::int64_t>(bound_.scissor.bottom, top, pass_height_);
        if (right > left && bottom > top) {
            [pass_of(pass_) setScissorRect:MTLScissorRect{static_cast<NSUInteger>(left), static_cast<NSUInteger>(top),
                                                          static_cast<NSUInteger>(right - left),
                                                          static_cast<NSUInteger>(bottom - top)}];
        }
    }
}

void RendererMtl::end_scene_pass(const char* caller) {
    if (!scene_pass_open_) {
        return;
    }
    if (mirrored_store_ != nullptr) {
        mirrored_store_->backend().set_open_pass(nullptr);
    }
    [pass_of(pass_) endEncoding];
    drop(pass_);
    if (scene_parallel_ != nullptr) {
        [(__bridge id<MTLParallelRenderCommandEncoder>)scene_parallel_ endEncoding];
        drop(scene_parallel_);
    }
    encoder_.attach(nullptr, 0u, 0u);
    scene_pass_open_ = false;
    scene_pass_closed_by_ = caller;
    if (!held_probe_slots_.empty()) {
        read_held_probes();
    }
    gpu_pass(current_pass_);
}

void RendererMtl::reset_draw_bindings() noexcept {
    bound_ = Bound{};
    encoder_.reset();
}

void RendererMtl::bind_scene() {
    const auto width = static_cast<float>(scene_.color->width);
    const auto height = static_cast<float>(scene_.color->height);
    bound_.viewport = gpu::ViewportRect{0.0f, 0.0f, width, height, 0.0f, 1.0f};
    bound_.viewport_valid = true;
    bound_.scissor = gpu::rect(0, 0, static_cast<std::int32_t>(scene_.color->width), static_cast<std::int32_t>(scene_.color->height));
    bound_.scissor_valid = true;
    if (scene_pass_open_) {
        [pass_of(pass_) setViewport:MTLViewport{0.0, 0.0, width, height, 0.0, 1.0}];
        [pass_of(pass_) setScissorRect:MTLScissorRect{0u, 0u, scene_.color->width, scene_.color->height}];
        encoder_.forget_rects();
    }
}

void RendererMtl::clear_scene(std::uint32_t clear_rgba, bool color_enable, bool alpha_enable, bool depth_enable,
                              unsigned rect_x, unsigned rect_y, unsigned rect_w, unsigned rect_h, std::uint8_t pixel_format) {
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
    if (depth_enable && depth_clear_pending()) {
        if (whole) {
            pending_depth_clear_ = 0u;  // replaced
        } else {
            settle_depth_clear();
        }
    }
    if (depth_enable && !held_probe_slots_.empty()) {
        // Held probes read the depth before the clear replaces it; the pass is closed for the clear's own opening.
        end_scene_pass();
    }
    const SceneClearValues clear = make_scene_clear_values(clear_rgba, pixel_format);
    scene_.depth_dirty |= depth_enable && scene_.samples > 1u;
    // The whole scene target, every channel, while the pass is closed (a copy-clear): the pass begins cleared.
    const bool has_alpha = pixel_format == 1u;
    const bool every_channel = color_enable && (alpha_enable || !has_alpha);
    if (!scene_pass_open_ && every_channel && depth_enable && whole) {
        begin_scene_pass(&clear);
        return;
    }
    const char* kind = !whole ? "clear_scene(part)"
                       : every_channel ? (depth_enable ? "clear_scene(colour+depth)" : "clear_scene(colour)")
                       : color_enable || alpha_enable ? (depth_enable ? "clear_scene(channels+depth)" : "clear_scene(channels)")
                       : "clear_scene(depth)";
    // The whole depth while the pass is closed: the pass opens with the depth cleared instead of loading what the
    // clear replaces (the stencil is kept, as the depth clear's draw keeps it); the whole colour is not loaded when
    // the clear replaces it.
    const bool open_cleared_depth = !scene_pass_open_ && whole && depth_enable && !target_pass_active_;
    begin_scene_pass_with(0u, open_cleared_depth ? &clear : nullptr, !scene_pass_open_ && every_channel && whole, kind,
                          open_cleared_depth ? 1u : 0u);
    if (color_enable || alpha_enable) {
        if (!has_alpha) {
            alpha_enable = true;
        }
        // A full-target triangle with the channels' write mask, cut to the rectangle.
        const NSUInteger mask = color_enable && alpha_enable ? kWriteAll : color_enable ? kWriteRgb : NSUInteger{MTLColorWriteMaskAlpha};
        utility_draw(pass_, scene_pass_pipeline(mtl::UtilityShader::ClearPs, static_cast<std::uint8_t>(mask)), DepthUse::None,
                     pass_width_, pass_height_, rect, clear.color.data(), sizeof(float) * 4u, nullptr);
        reset_draw_bindings();
        bind_scene();
    }
    if (depth_enable && !open_cleared_depth) {
        clear_depth_in_pass(clear.reversed_depth, rect);
    }
}

// In the open scene pass (aux 0): the depth clear's draw, which keeps the stencil.
void RendererMtl::clear_depth_in_pass(float reversed_depth, const gpu::Rect& rect) {
    const float value[4] = {reversed_depth, 0.0f, 0.0f, 0.0f};
    utility_draw(pass_, scene_pass_pipeline(mtl::UtilityShader::ClearDepthPs, 0u), DepthUse::Write, pass_width_, pass_height_,
                 rect, value, sizeof(value), nullptr);
    reset_draw_bindings();
    bind_scene();
}

bool RendererMtl::defer_scene_depth_clear(std::uint32_t clear_rgba) {
    if (!scene_pass_open_ || held_probe_slots_.empty() || target_pass_active_) {
        return false;
    }
    pending_depth_ = scene_depth_attachment();
    pending_depth_values_ = make_scene_clear_values(clear_rgba, 0u);
    pending_depth_clear_ = 1u;
    return true;
}

// The waiting depth clear now: in the open pass once no held probe needs the depth it replaces; otherwise the pass
// ends (the held probes read) and the next opening clears it, at once when no pass was open.
void RendererMtl::settle_depth_clear() {
    if (!depth_clear_pending()) {
        return;
    }
    if (scene_pass_open_ && held_probe_slots_.empty() && scene_pass_aux_ == 0u) {
        pending_depth_clear_ = 0u;
        scene_.depth_dirty |= scene_.samples > 1u;
        clear_depth_in_pass(pending_depth_values_.reversed_depth,
                            gpu::rect(0, 0, static_cast<std::int32_t>(scene_.color->width),
                                      static_cast<std::int32_t>(scene_.color->height)));
        return;
    }
    const bool open = scene_pass_open_;
    const unsigned aux = scene_pass_aux_;
    end_scene_pass("settle_depth_clear");
    if (open) {
        begin_scene_pass_with(aux);
        return;
    }
    const SceneClearValues values = pending_depth_values_;
    const unsigned mode = std::exchange(pending_depth_clear_, 0u);
    begin_scene_pass_with(0u, &values, false, "settle_depth_clear", mode);
    end_scene_pass("settle_depth_clear");
}

FrameMap RendererMtl::output_frame_for_window() const {
    const auto aspect = galaxy::experimental_effective_host_aspect();
    const galaxy::ContentViewport content = galaxy::fit_content_viewport(
        static_cast<int>(host_window::backbuffer_width), static_cast<int>(host_window::backbuffer_height),
        aspect ? aspect->content_aspect : galaxy::kDesignContentAspect);
    if (content.width <= 0 || content.height <= 0) {
        return FrameMap{};
    }
    return FrameMap(static_cast<unsigned>(content.width), static_cast<unsigned>(content.height));
}

// The scene target and the output scene target trade places.
void RendererMtl::swap_scene_sets() {
    end_scene_pass();
    std::swap(scene_, alt_scene_);
    std::swap(scene_map_, alt_scene_map_);
    output_scene_active_ = !output_scene_active_;
    reset_draw_bindings();
}

// The scene target and the target passes' set trade places (begin_target_pass).
void RendererMtl::swap_target_set() {
    end_scene_pass();
    std::swap(scene_, target_scene_);
    std::swap(scene_map_, target_scene_map_);
    target_pass_active_ = !target_pass_active_;
    reset_draw_bindings();
}

// ---- target passes and pictures ----

bool RendererMtl::begin_target_pass(std::uint64_t key, std::uint32_t width, std::uint32_t height) {
    if (target_pass_active_ || width == 0u || height == 0u) {
        return false;
    }
    TargetSlot& slot = target_slots_[key];
    const FrameMap map(width, height);
    const bool kept = slot.set.color && slot.map == map;
    if (!kept) {
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

void RendererMtl::end_target_pass(std::uint8_t channels, TextureStore& textures) {
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
    // Keys unused for kTargetIdleFrames frames give back their render resources (their pictures stay).
    sweep_target_slots();
    bind_scene();
}

void RendererMtl::retire_target_slot(TargetSlot& slot) {
    destroy_scene_set(slot.set);
}

void RendererMtl::sweep_target_slots() {
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

bool RendererMtl::target_picture(std::uint64_t key, TextureHandle& out) const {
    if (pictures_.empty()) {
        return false;
    }
    const auto it = pictures_.find(key);
    if (it == pictures_.end()) {
        return false;
    }
    out = TextureHandle{};
    out.resource = gpu::raw(it->second.viewed);
    out.srv_index = it->second.view;
    out.width = static_cast<std::uint32_t>(it->second.viewed->width);
    out.height = static_cast<std::uint32_t>(it->second.viewed->height);
    out.mip_levels = 1u;
    return true;
}

void RendererMtl::drop_picture(Picture& picture, TextureStore& textures) {
    textures.retire_view(picture.view, std::move(picture.viewed));
    if (picture.sampled != nullptr) {
        retire_object(std::exchange(picture.sampled, nullptr));
    }
    picture.view = ~0u;
}

bool RendererMtl::publish_picture(std::uint64_t key, std::uint32_t width, std::uint32_t height, gpu::TextureRef texture,
                                  std::uint8_t channels, TextureStore& textures) {
    Picture& picture = pictures_[key];
    if (picture.view != ~0u && picture.viewed == texture && picture.channels == channels && picture.width == width &&
        picture.height == height) {
        return false;
    }
    if (picture.view != ~0u) {
        drop_picture(picture, textures);
    }
    picture.view = textures.allocate_view();
    picture.viewed = std::move(texture);
    picture.width = width;
    picture.height = height;
    picture.channels = channels;
    const std::array<std::uint8_t, 4> swizzle = target_channel_swizzle(channels);
    if (swizzle != std::array<std::uint8_t, 4>{0, 1, 2, 3}) {
        static constexpr MTLTextureSwizzle kChannel[5] = {MTLTextureSwizzleRed, MTLTextureSwizzleGreen, MTLTextureSwizzleBlue,
                                                          MTLTextureSwizzleAlpha, MTLTextureSwizzleOne};
        id<MTLTexture> const whole = texture_of(picture.viewed);
        id<MTLTexture> const view = [whole newTextureViewWithPixelFormat:whole.pixelFormat
                                                             textureType:MTLTextureType2D
                                                                  levels:NSMakeRange(0, 1)
                                                                  slices:NSMakeRange(0, 1)
                                                                 swizzle:MTLTextureSwizzleChannelsMake(
                                                                             kChannel[swizzle[0]], kChannel[swizzle[1]],
                                                                             kChannel[swizzle[2]], kChannel[swizzle[3]])];
        if (view == nil) {
            throw std::runtime_error("a picture's channel view could not be made");
        }
        picture.sampled = own(view);
    }
    textures.backend().write_view(picture.view, picture.sampled != nullptr ? picture.sampled : picture.viewed->view());
    return true;
}

bool RendererMtl::publish_frame_pass(const FramePassRequest& params, gpu::TextureRef texture, TextureStore& textures) {
    const FrameExtent size = frame_pass_extent(scene_map_);
    return publish_picture(params.key, size.width, size.height, std::move(texture), 0u, textures);
}

void RendererMtl::free_target(std::uint32_t id, TextureStore& textures) {
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
    // A target pass's set for this texture: ids are not used again, so no pass draws into it after this.
    for (auto it = target_slots_.begin(); it != target_slots_.end();) {
        if (static_cast<std::uint32_t>(it->first) == id && !(target_pass_active_ && it->first == target_key_)) {
            retire_target_slot(it->second);
            it = target_slots_.erase(it);
        } else {
            ++it;
        }
    }
}

// A texture the GPU may still read: released once the submissions that can name it completed.
void RendererMtl::retire(gpu::TextureRef texture) {
    if (!texture) {
        return;
    }
    frames_[frame_slot_].retired.push_back(std::move(texture));
}

void RendererMtl::retire_object(mtl::Object object) {
    if (object != nullptr) {
        frames_[frame_slot_].retired_objects.push_back(object);
    }
}

// ---- copies of the scene target ----

// The scene target's colour drawn into `dest` through the conversion shader: `copy_format` the channels the picture
// keeps, the source rectangle from (src_x, src_y) in steps of (step_x, step_y) frame pixels a picture pixel.
void RendererMtl::record_conversion_blit(mtl::Texture& dest, unsigned dest_w, unsigned dest_h, std::uint8_t copy_format,
                                         float src_x, float src_y, float step_x, float step_y) {
    end_scene_pass();
    // A display copy one scene target pixel to one picture pixel without gamma: its filter alone.
    const bool display_filter = copy_format == kCopyLayoutDisplay && step_x == 1.0f && step_y == 1.0f;
    UtilityKey key;
    key.fragment = display_filter ? mtl::UtilityShader::DisplayFilterPs : mtl::UtilityShader::SceneConversionPs;
    key.colors[0] = dest.format;
    key.color_count = 1u;
    key.write_mask = static_cast<std::uint8_t>(kWriteAll);
    const mtl::Object pipeline = utility_pipeline(key);
    const SceneConversionShaderConstants constants = make_scene_conversion_constants(copy_format, src_x, src_y, step_x, step_y);
    const mtl::Object pass = begin_color_pass(dest.texture, false, "scene copy");
    utility_draw(pass, pipeline, DepthUse::None, dest_w, dest_h,
                 gpu::rect(0, 0, static_cast<std::int32_t>(dest_w), static_cast<std::int32_t>(dest_h)), &constants,
                 sizeof(constants), scene_.color->texture);
    end_own_pass(pass);
    reset_draw_bindings();
    bind_scene();
}

bool RendererMtl::can_take_scene() const noexcept {
    return !frame_generation_running() && scene_.color && !output_scene_active_ && !target_pass_active_ &&
           scene_.samples <= 1u && !scene_.wide;
}

bool RendererMtl::take_scene(std::uint64_t key, gpu::TextureRef fresh, TextureStore& textures, gpu::TextureRef& released) {
    end_scene_pass();
    if (const auto it = pictures_.find(key); it != pictures_.end()) {
        released = it->second.viewed;
    }
    gpu::TextureRef picture = swap_scene_color(std::move(fresh));
    const FrameExtent size = frame_pass_extent(scene_map_);
    return publish_picture(key, size.width, size.height, std::move(picture), 0u, textures);
}

void RendererMtl::open_frame_for_pass(std::uint64_t color_key, std::uint8_t channels, std::uint64_t depth_key,
                                      TextureStore& textures) {
    end_scene_pass();
    const FrameExtent size = frame_pass_extent(scene_map_);
    if (color_key != 0u) {
        (void)publish_picture(color_key, size.width, size.height, scene_.color, channels, textures);
    }
    if (depth_key != 0u) {
        settle_depth_clear();
        resolve_scene_depth();
        (void)publish_picture(depth_key, size.width, size.height, scene_.depth, 0u, textures);
    }
    frame_pass_color_key_ = color_key;
    frame_pass_depth_key_ = depth_key;
}

void RendererMtl::close_frame_for_pass(TextureStore& textures) {
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
gpu::TextureRef RendererMtl::swap_scene_color(gpu::TextureRef fresh) {
    const GpuPassScope scope{*this, GpuPass::DisplayCopy};
    end_scene_pass();
    const mtl::Texture& picture = *scene_.color;
    if (fresh && (fresh->width != picture.width || fresh->height != picture.height || fresh->format != picture.format ||
                  (fresh->use & picture.use) != picture.use || fresh->samples != 1u)) {
        retire(std::move(fresh));
    }
    if (!fresh) {
        fresh = mtl::Context::get().create_texture(picture.width, picture.height, 1u, picture.format, picture.use,
                                                   mtl::Storage::Private, "scene target color");
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

gpu::TextureRef RendererMtl::take_frame_picture(gpu::TextureRef fresh) {
    if (scene_.wide) {
        end_wide_scene();
    }
    end_scene_pass();
    return swap_scene_color(std::move(fresh));
}

bool RendererMtl::draw_frame_pass(const FramePassRequest& params, TextureStore& textures) {
    const GpuPassScope scope{*this, GpuPass::FramePass};
    ++gpu_window_.copies;
    const FrameExtent extent = frame_pass_extent(scene_map_);
    const unsigned w = extent.width;
    const unsigned h = extent.height;
    if (w == 0u || h == 0u) {
        return false;
    }
    // Keyed by the texture and its channels.
    const std::uint64_t dest_key = static_cast<std::uint64_t>(params.key) | (static_cast<std::uint64_t>(params.channels) << 48u);
    FramePassTarget& dest = frame_pass_targets_[dest_key];
    // As RendererVk::draw_frame_pass: every channel of a single-sample scene target of the capture's size and format is
    // taken with the scene target's texture, and the scene goes on in the capture's last texture with the frame drawn back
    // into it as the pass opens (the bloom composite's kind 0). On Apple's tiled GPUs the frame is stored once and read
    // once, where the copy stored it, read it, wrote the capture and loaded the scene target again.
    if (params.channels == 0u && can_take_scene() && scene_pass_aux_ == 0u && scene_.color->width == w &&
        scene_.color->height == h && scene_.color->format == draw_layout_.scene_color) {
        end_scene_pass();
        if (bloom_.prepare_draw_back(draw_layout_.scene_color, scene_.samples)) {
            // The last capture's texture, or one made for it (swap_scene_color: one of another size or use is retired).
            gpu::TextureRef picture = swap_scene_color(std::move(dest.texture));
            dest.texture = picture;
            dest.width = w;
            dest.height = h;
            // The colour opens unloaded: the draw writes every pixel. The depth loads.
            begin_scene_pass_with(0u, nullptr, true);
            bloom_.draw_back(pass_, draw_layout_.scene_color, scene_.samples, *picture, w, h);
            reset_draw_bindings();
            bind_scene();
            return publish_frame_pass(params, std::move(picture), textures);
        }
    }
    if (!dest.texture || dest.width != w || dest.height != h) {
        if (dest.texture) {
            retire(std::move(dest.texture));
        }
        dest.texture = mtl::Context::get().create_texture(w, h, 1u, draw_layout_.scene_color, mtl::kUseTarget | mtl::kUseSample,
                                                          mtl::Storage::Private, "frame pass");
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
    record_conversion_blit(*dest.texture, w, h, params.channels, sampling.src_x, sampling.src_y, sampling.step_x,
                           sampling.step_y);
    return publish_frame_pass(params, dest.texture, textures);
}

void RendererMtl::set_draw_aux(unsigned aux) {
    // Aux 3 needs the layer's window open (the walker asks for it only then); otherwise it is a reactive draw. Aux 4
    // draws in the motion pass.
    const unsigned wanted = effective_draw_aux(aux);
    if (scene_pass_open_ && scene_pass_aux_ == wanted) {
        return;
    }
    begin_scene_pass_with(wanted, nullptr);
}

}  // namespace galaxy::render
