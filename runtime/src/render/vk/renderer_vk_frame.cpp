// SPDX-License-Identifier: GPL-3.0-only
// The Vulkan renderer's scene target (renderer_vk.h): its targets, draws, clears, copies, screen probes and test captures.

#include "galaxy/render/rt_caps.h"
#include "galaxy/render/program_table.h"
#include "renderer_vk_internal.h"
#include "galaxy/render/vk/renderer_vk.h"

#include "render/core/frame_core.h"
#include <nebula_frame_list.h>

#include "galaxy/content_viewport.h"
#include "galaxy/experimental_ultrawide_aspect.h"
#include "galaxy/render/host_window.h"
#include "galaxy/render/render_config.h"
#include "galaxy/render/vk/streamline_vk.h"
#if defined(_WIN32)
#include "galaxy/render/streamline_bridge.h"
#endif
#include "galaxy/platform_time.h"
#include "galaxy/render/texture_store.h"

#include <span>
#include <tuple>
#include <vk_mem_alloc.h>

#include <algorithm>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <span>
#include <stdexcept>
#include <thread>

namespace galaxy::render {

namespace {

using namespace vk_detail;

// A test capture's pixels as a binary PPM, written off the render thread.
void write_ppm_async(std::string path, std::vector<std::uint8_t> rgb, unsigned width, unsigned height) {
    std::thread([path = std::move(path), rgb = std::move(rgb), width, height] {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << "P6\n" << width << ' ' << height << "\n255\n";
        out.write(reinterpret_cast<const char*>(rgb.data()), static_cast<std::streamsize>(rgb.size()));
    }).detach();
}

// A conversion input (utility_set_layout_, its sampler the linear clamp): `view`, shader-readable.
void write_conversion_input(VkDescriptorSet set, VkImageView view) {
    const VkDescriptorImageInfo image{VK_NULL_HANDLE, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = set;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    write.pImageInfo = &image;
    vkUpdateDescriptorSets(vk::Context::get().device(), 1, &write, 0, nullptr);
}

}  // namespace

// A scene target set at `scale`: targets in their attachment layouts, the framebuffer, and the conversion input (allocated
// once per set; rewriting it needs a GPU that no longer reads the old views, which callers ensure).
bool RendererVk::create_scene_set(SceneSet& set, const FrameMap& map, unsigned samples, unsigned set_width,
                                unsigned set_height, bool target) {
    vk::Context& context = vk::Context::get();
    const VkDevice device = context.device();
    const std::uint32_t width = static_cast<std::uint32_t>(std::lround(static_cast<double>(set_width) * map.sx));
    const std::uint32_t height = static_cast<std::uint32_t>(std::lround(static_cast<double>(set_height) * map.sy));
    gpu::TextureRef color = context.create_texture(
        width, height, 1u, target ? vk::kTargetColorFormat : vk::kSceneColorFormat,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | (scene_set_storage_ ? VK_IMAGE_USAGE_STORAGE_BIT : 0u),
        true, "scene target color");
    gpu::TextureRef depth = context.create_texture(
        width, height, 1u, depth_format_,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        true, "scene target depth");
    if (!color || !depth) {
        std::cerr << "[Renderer] scene target " << width << 'x' << height << " allocation failed\n";
        return false;
    }
    gpu::TextureRef ms_color;
    gpu::TextureRef ms_depth;
    if (samples > 1u) {
        const auto count = static_cast<VkSampleCountFlagBits>(samples);
        ms_color = context.create_texture(width, height, 1u, vk::kSceneColorFormat,
                                          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, true, "scene target color MSAA", count);
        ms_depth = context.create_texture(width, height, 1u, depth_format_,
                                          VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                          true, "scene target depth MSAA", count);
        if (!ms_color || !ms_depth) {
            std::cerr << "[Renderer] scene target MSAA " << samples << "x allocation failed; single-sample\n";
            ms_color.reset();
            ms_depth.reset();
            samples = 1u;
        }
    }
    const auto make_framebuffer = [&](VkRenderPass pass, std::span<const VkImageView> views) {
        VkFramebufferCreateInfo info{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        info.renderPass = pass;
        info.attachmentCount = static_cast<std::uint32_t>(views.size());
        info.pAttachments = views.data();
        info.width = width;
        info.height = height;
        info.layers = 1;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        vk::check(vkCreateFramebuffer(device, &info, nullptr, &framebuffer), "scene target framebuffer");
        return framebuffer;
    };
    const VkImageView single[2] = {color->target_view, depth->target_view};
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    VkFramebuffer resolve_framebuffer = VK_NULL_HANDLE;
    if (samples > 1u) {
        const VkImageView multi[3] = {ms_color->target_view, ms_depth->target_view, color->target_view};
        framebuffer = make_framebuffer(draw_layout_.pass_for(static_cast<unsigned>(std::countr_zero(samples)), false),
                                       multi);
        resolve_framebuffer = make_framebuffer(draw_layout_.scene_pass, single);
    } else {
        framebuffer = make_framebuffer(target ? draw_layout_.target_pass : draw_layout_.scene_pass, single);
    }
    // The new targets enter their attachment layouts now, so every later submission finds them so.
    run_one_time([&](VkCommandBuffer commands) {
        for (const vk::Texture* target : {color.get(), ms_color.get()}) {
            if (target == nullptr) continue;
            image_barrier(commands, target->image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
                          VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                          VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
        }
        for (const vk::Texture* target : {depth.get(), ms_depth.get()}) {
            if (target == nullptr) continue;
            image_barrier(commands, target->image, depth_aspects(depth_format_), VK_IMAGE_LAYOUT_UNDEFINED,
                          VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
                          VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
                          VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
        }
    });
    set.color = std::move(color);
    set.depth = std::move(depth);
    set.framebuffer = framebuffer;
    set.resolve_framebuffer = resolve_framebuffer;
    set.ms_color = std::move(ms_color);
    set.ms_depth = std::move(ms_depth);
    set.samples = samples;
    set.depth_dirty = false;
    set.turned = false;
    set.depth_serial = ++scene_depth_serial_;
    if (set.color_source == VK_NULL_HANDLE) {
        set.color_source = allocate_persistent_set(utility_set_layout_);
    }
    if (samples > 1u) {
        if (set.ms_depth_source == VK_NULL_HANDLE) {
            set.ms_depth_source = allocate_persistent_set(utility_set_layout_);
        }
        const VkDescriptorImageInfo ms_info{VK_NULL_HANDLE, set.ms_depth->view, kDepthSampleLayout};
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = set.ms_depth_source;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        write.pImageInfo = &ms_info;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    }
    write_conversion_input(set.color_source, set.color->view);
    return true;
}

// Releases a set's targets and framebuffer once the GPU is past the commands recorded so far (its conversion inputs
// stay allocated for the next create_scene_set).
void RendererVk::destroy_scene_set(SceneSet& set) {
    const VkDevice device = vk::Context::get().device();
    // The wide set borrows the base set's depth-resolve framebuffer.
    if (set.wide) set.resolve_framebuffer = VK_NULL_HANDLE;
    for (VkFramebuffer* framebuffer : {&set.framebuffer, &set.resolve_framebuffer}) {
        if (*framebuffer != VK_NULL_HANDLE) {
            const VkFramebuffer retired = *framebuffer;
            vk::Context::get().defer([device, retired] { vkDestroyFramebuffer(device, retired, nullptr); });
            *framebuffer = VK_NULL_HANDLE;
        }
    }
    retire(std::move(set.color));
    retire(std::move(set.depth));
    retire(std::move(set.ms_color));
    retire(std::move(set.ms_depth));
    set.samples = 1u;
}

bool RendererVk::create_scene_targets(const FrameMap& map, unsigned samples) {
    SceneSet made;
    made.color_source = scene_.color_source;
    made.ms_depth_source = scene_.ms_depth_source;
    if (!create_scene_set(made, map, samples)) {
        return false;
    }
    if (scene_.framebuffer != VK_NULL_HANDLE) {
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

unsigned RendererVk::wanted_msaa_samples() const {
    // A temporal upscaler antialiases the scene itself.
    if (temporal_mode()) {
        return 1u;
    }
    return std::min(std::bit_floor(std::clamp(get_render_config().msaa_samples, 1u, 8u)), msaa_max_samples_);
}

void RendererVk::resolve_scene_depth(VkCommandBuffer commands) {
    SceneSet& set = scene_;
    if (set.samples <= 1u || !set.depth_dirty || depth_resolve_pipeline_ == VK_NULL_HANDLE) {
        return;
    }
    const GpuPassScope pass{*this, GpuPass::Resolve};
    image_barrier(commands, set.ms_depth->image, depth_aspects(depth_format_),
                  VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, kDepthSampleLayout,
                  VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                  VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    const std::uint32_t width = scene_map_.width;
    const std::uint32_t height = scene_map_.height;
    VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    begin.renderPass = draw_layout_.scene_pass;
    begin.framebuffer = set.wide ? set.resolve_framebuffer : scene_framebuffer(true);
    begin.renderArea = {{0, 0}, {width, height}};
    vkCmdBeginRenderPass(commands, &begin, VK_SUBPASS_CONTENTS_INLINE);
    const VkViewport viewport{0.0f, static_cast<float>(height), static_cast<float>(width),
                              -static_cast<float>(height), 0.0f, 1.0f};
    const VkRect2D scissor{{0, 0}, {width, height}};
    vkCmdSetViewport(commands, 0, 1, &viewport);
    vkCmdSetScissor(commands, 0, 1, &scissor);
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, depth_resolve_pipeline_);
    vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, utility_layout_, 0, 1, &set.ms_depth_source,
                            0, nullptr);
    vkCmdDraw(commands, 3, 1, 0, 0);
    vkCmdEndRenderPass(commands);
    image_barrier(commands, set.ms_depth->image, depth_aspects(depth_format_), kDepthSampleLayout,
                  VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_NONE,
                  VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
                  VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
    if (commands == commands_) {
        // Another command buffer resolves what the frame submitted before it; the frame's own draws since stay due.
        set.depth_dirty = false;
        reset_draw_bindings();
    }
}

void RendererVk::run_one_time(const std::function<void(VkCommandBuffer)>& record) {
    vk::Context& context = vk::Context::get();
    const VkDevice device = context.device();
    VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    alloc.commandPool = utility_pool_;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 1;
    VkCommandBuffer commands = VK_NULL_HANDLE;
    vk::check(vkAllocateCommandBuffers(device, &alloc, &commands), "vkAllocateCommandBuffers");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vk::check(vkBeginCommandBuffer(commands, &begin), "vkBeginCommandBuffer");
    record(commands);
    vk::check(vkEndCommandBuffer(commands), "vkEndCommandBuffer");
    const bool done = context.wait(context.submit(commands), 10'000'000'000ull);
    vkFreeCommandBuffers(device, utility_pool_, 1, &commands);
    if (!done) {
        throw std::runtime_error("one-time GPU work timed out or the device was lost");
    }
}

bool RendererVk::video_memory(std::uint64_t& usage, std::uint64_t& budget) const noexcept {
    return vk::Context::get().device_memory_budget(usage, budget);
}

// `[vram] breakdown`: every 600 frames with --diagnostics memory (RenderCore's table log). The allocator's blocks and used bytes are the device-local
// heap's (images and buffers together); the images listed are each texture's allocation size. Textures of the store, the post
// chain's, ambient occlusion's and bloom's targets and the rings are in `used`, not in `accounted`, so the rest is `unlisted`.
// Upload rings in video memory (a CPU-mapped one) are counted in `rings local`.
void RendererVk::log_video_memory(const VideoMemoryOutside& outside) {
    const auto mb = [](std::uint64_t bytes) { return static_cast<double>(bytes) / (1024.0 * 1024.0); };
    std::uint64_t usage = 0u;
    std::uint64_t budget = 0u;
    (void)video_memory(usage, budget);
    vk::Context::HeapStats local;
    vk::Context::HeapStats other;
    (void)vk::Context::get().heap_stats(local, other);

    const auto set_bytes = [](const SceneSet& set) {
        return allocation_bytes(set.color.get()) + allocation_bytes(set.depth.get()) + allocation_bytes(set.ms_color.get()) +
               allocation_bytes(set.ms_depth.get());
    };
    // The scene targets: the live set, the wide, output-resolution and open target pass's, and the motion, reactive and
    // effects-layer targets, the frame-generation and HDR captures.
    std::uint64_t scene = set_bytes(scene_) + set_bytes(wide_scene_) + set_bytes(alt_scene_) + set_bytes(target_scene_);
    for (const gpu::TextureRef* texture : {&motion_.motion, &motion_.reactive, &motion_.layer_color, &motion_.layer_trans,
                                           &fg_hudless_scene_color_, &fg_hudless_display_, &fg_hudless_target_, &hdr_capture_,
                                           &oriented_}) {
        scene += allocation_bytes(texture->get());
    }
    std::uint64_t slots = 0u;
    for (const auto& entry : target_slots_) {
        slots += set_bytes(entry.second.set);
    }
    std::uint64_t frame_pass = 0u;
    for (const auto& entry : frame_pass_targets_) {
        frame_pass += allocation_bytes(entry.second.texture.get());
    }
    // Images waiting out their frames, and buffers (retired rings and chunks) listed apart.
    std::uint64_t retired_textures = 0u;
    std::uint64_t retired_buffers = 0u;
    for (const Frame& frame : frames_) {
        for (const gpu::TextureRef& texture : frame.retired) {
            retired_textures += allocation_bytes(texture.get());
        }
        for (const gpu::BufferRef& buffer : frame.retired_buffers) {
            retired_buffers += buffer ? buffer->size : 0u;
        }
    }
    std::uint64_t ring_local = 0u;
    std::uint64_t ring_host = 0u;
    std::uint64_t chunk_local = 0u;
    std::uint64_t chunk_host = 0u;
    for (const UploadRing* ring : {&vertex_ring_, &index_ring_, &constant_ring_, &matrix_ring_}) {
        (ring->local() ? ring_local : ring_host) += ring->capacity();
        (ring->local() ? chunk_local : chunk_host) += ring->live_chunk_bytes();
    }
    const std::uint64_t accounted =
        scene + slots + frame_pass + outside.picture_bytes + retired_textures + ring_local + chunk_local + outside.mesh_local_bytes;
    std::fprintf(stderr,
                 "[vram] breakdown os-usage=%.0f budget=%.0f MB | allocator blocks=%u %.0f used=%.1f allocations=%u | "
                 "scene-targets=%.1f target-slots=%zu %.1f frame-pass=%zu %.1f pictures=%u %.1f retired=%.1f | "
                 "rings local=%.0f chunks=%.0f | accounted=%.0f unlisted=%.0f | other-heaps blocks=%u %.0f used=%.0f | "
                 "host: staging=%.0f mesh-pages=%u (empty %u) %.0f (%.0f in video memory) live %.1f slots=%u %.1f rings=%.0f chunks=%.0f "
                 "retired-buffers=%.1f | pipelines=%llu\n",
                 mb(usage), mb(budget), local.blocks, mb(local.block_bytes), mb(local.allocation_bytes), local.allocations,
                 mb(scene), target_slots_.size(), mb(slots), frame_pass_targets_.size(), mb(frame_pass), outside.picture_count,
                 mb(outside.picture_bytes), mb(retired_textures), mb(ring_local), mb(chunk_local), mb(accounted),
                 local.allocation_bytes > accounted ? mb(local.allocation_bytes - accounted) : 0.0, other.blocks,
                 mb(other.block_bytes), mb(other.allocation_bytes), mb(outside.staging_bytes), outside.mesh_pages,
                 outside.mesh_empty_pages, mb(outside.mesh_page_bytes), mb(outside.mesh_local_bytes), mb(outside.mesh_live_bytes),
                 outside.slots,
                 mb(outside.slot_bytes), mb(ring_host), mb(chunk_host), mb(retired_buffers),
                 static_cast<unsigned long long>(outside.pipelines));
}

// The scale-dependent memory a new internal resolution adds must fit 90% of the device's budget (RendererD3D12's
// rule): scene target colour (4 bytes per pixel) and depth (4 for D24S8; D32F with stencil counted at 8, as drivers pad
// it) and, at most, the display picture pair plus one full-picture copy (12 bytes per pixel of the picture).
bool RendererVk::scene_fits_memory(const FrameMap& map) const {
    std::uint64_t usage = 0u;
    std::uint64_t budget = 0u;
    if (!vk::Context::get().device_memory_budget(usage, budget)) {
        return true;
    }
    const double depth_bytes = depth_format_ == VK_FORMAT_D24_UNORM_S8_UINT ? 4.0 : 8.0;
    const auto bytes = [depth_bytes](const FrameMap& m) { return (4.0 + depth_bytes + 12.0) * m.width * m.height; };
    const double needed = bytes(map) - bytes(scene_map_);
    return static_cast<double>(usage) + needed <= 0.9 * static_cast<double>(budget);
}

// A changed internal resolution, between frames: the GPU finishes with the old targets and every scaled copy.
void RendererVk::apply_window_size() {
    if (scene_.wide) {
        return;
    }
    // An edge drag resizes every frame: settle first, then re-create once.
    if (host_window::move_size_active.load(std::memory_order_acquire)) {
        return;
    }
    // The picture's exact size on the screen, square pixels.
    const auto aspect = galaxy::experimental_effective_host_aspect();
    const galaxy::ContentViewport content = galaxy::fit_content_viewport(
        static_cast<int>(host_window::backbuffer_width), static_cast<int>(host_window::backbuffer_height),
        aspect ? aspect->content_aspect : galaxy::kDesignContentAspect);
    if (content.width <= 0 || content.height <= 0) {
        return;
    }
    FrameMap wanted(static_cast<unsigned>(content.width), static_cast<unsigned>(content.height));
    upscale_output_ = wanted;
    const auto config = get_render_config();
    const auto [render_width, render_height] = internal_extent(wanted.width, wanted.height, render_internal_height(config),
        vk::Context::get().properties().limits.maxImageDimension2D,
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
        destroy_retired_pools(frame);
    }
    const FrameMap previous = scene_map_;
    if (!create_scene_targets(wanted, samples)) {
        return;
    }
    reset_draw_bindings();
    std::cout << "[Renderer] internal resolution " << previous.width << "x" << previous.height << " -> " << wanted.width
              << "x" << wanted.height << '\n';
}

void RendererVk::begin_scene_pass(const SceneClearValues* clear, const char* caller) {
    begin_scene_pass_with(0u, clear, false, caller);
}

void RendererVk::begin_scene_pass_with(unsigned aux, const SceneClearValues* clear, bool discard_color, const char* caller,
                                       unsigned clear_depth, bool secondary) {
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
            // No variant of this pass clears only the depth: one that does opens and ends first.
            const SceneClearValues values = pending_depth_values_;
            const unsigned mode = std::exchange(pending_depth_clear_, 0u);
            begin_scene_pass_with(0u, &values, false, "settle_depth_clear", mode);
            end_scene_pass("settle_depth_clear");
        }
    }
    const VkCommandBuffer commands = commands_;
    VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    begin.renderPass = aux == kAuxPrepass ? VK_NULL_HANDLE
                       : aux != 0u        ? draw_layout_.aux_pass_for(scene_variant(), aux, clear != nullptr)
                                          : draw_layout_.pass_for(scene_variant(), clear != nullptr);
    // The scene target's own variants (a texture target's colour has another format).
    if (discard_color && aux == 0u && !target_pass_active_) {
        begin.renderPass = draw_layout_.discard_color_passes[scene_.wide ? 1u : 0u][scene_samples_log2()];
    }
    if (clear_depth != 0u && clear != nullptr && aux == 0u && !target_pass_active_) {
        begin.renderPass = draw_layout_.depth_clear_passes[clear_depth - 1u][scene_.wide ? 1u : 0u][scene_samples_log2()];
    }
    begin.framebuffer = aux == kAuxPrepass ? VK_NULL_HANDLE : aux != 0u ? motion_framebuffer(aux) : scene_framebuffer();
    const bool items = aux == kAuxPrepass;
    if (items) {
        // The tracer's prepass: the item target with the scene's depth; its first pass clears the item target.
        begin.renderPass = item_clear_pending_ ? draw_layout_.item_pass_clear : draw_layout_.item_pass;
        begin.framebuffer = item_framebuffer();
    }
    scene_pass_aux_ = aux;
    // Loading a pass, tone mapping and AO compositing do not modify depth.
    // Native depth-writing draws and explicit clears invalidate the resolved image.
    scene_.depth_dirty |= clear != nullptr && scene_.samples > 1u;
    // The set's own size: the scene target, or a target pass's smaller target (E-07).
    begin.renderArea = {{0, 0}, {scene_.color->width, scene_.color->height}};
    VkClearValue values[3]{};  // an aux target clears to zero
    if (clear != nullptr) {
        std::memcpy(values[0].color.float32, clear->color.data(), sizeof(float) * 4u);
        values[1].depthStencil = {clear->reversed_depth, 0u};  // reversed-Z, as clear_scene's
        begin.clearValueCount = aux != 0u ? 3u : 2u;
        begin.pClearValues = values;
    }
    if (items) {
        values[0] = VkClearValue{};  // no item
        begin.clearValueCount = item_clear_pending_ ? 1u : 0u;
        begin.pClearValues = item_clear_pending_ ? values : nullptr;
        item_clear_pending_ = false;
    }
    gpu_pass(current_pass_);
    vkCmdBeginRenderPass(commands, &begin,
                         secondary ? VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS : VK_SUBPASS_CONTENTS_INLINE);
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
    encoder_.forget_rects();
    if (secondary) {
        return;  // only secondary command buffers run in this pass: each sets its own dynamic state
    }
    // Dynamic state and bindings outlive the pass, but the renderer's own passes in between changed them.
    if (bound_.viewport_valid) {
        vkCmdSetViewport(commands, 0, 1, &bound_.viewport);
    }
    if (bound_.scissor_valid) {
        vkCmdSetScissor(commands, 0, 1, &bound_.scissor);
    }
}

void RendererVk::end_scene_pass(const char* caller) {
    if (!scene_pass_open_) {
        return;
    }
    vkCmdEndRenderPass(commands_);
    scene_pass_open_ = false;
    scene_pass_closed_by_ = caller;
    if (!held_probe_slots_.empty()) {
        read_held_probes();
    }
    gpu_pass(current_pass_);
}

void RendererVk::reset_draw_bindings() noexcept {
    bound_ = Bound{};
    encoder_.reset();
}

void RendererVk::bind_scene() {
    const float x = 0.0f, y = 0.0f;
    const float w = static_cast<float>(scene_.color->width);
    const float h = static_cast<float>(scene_.color->height);
    bound_.viewport = VkViewport{x, y + h, w, -h, 0.0f, 1.0f};
    bound_.viewport_valid = true;
    bound_.scissor = VkRect2D{{0, 0}, {scene_.color->width, scene_.color->height}};
    bound_.scissor_valid = true;
    if (scene_pass_open_) {
        const VkCommandBuffer commands = commands_;
        vkCmdSetViewport(commands, 0, 1, &bound_.viewport);
        vkCmdSetScissor(commands, 0, 1, &bound_.scissor);
    }
}

void RendererVk::clear_scene(std::uint32_t clear_rgba,
                           bool color_enable, bool alpha_enable, bool depth_enable, unsigned rect_x, unsigned rect_y,
                           unsigned rect_w, unsigned rect_h, std::uint8_t pixel_format) {
    const std::int32_t scene_w = static_cast<std::int32_t>(scene_.color->width);
    const std::int32_t scene_h = static_cast<std::int32_t>(scene_.color->height);
    VkRect2D rect{{0, 0}, {static_cast<std::uint32_t>(scene_w), static_cast<std::uint32_t>(scene_h)}};
    if (rect_w != 0u && rect_h != 0u) {
        const ScaledSceneRect scaled = rect_pixels(
            static_cast<std::int32_t>(rect_x), static_cast<std::int32_t>(rect_y),
            static_cast<std::int32_t>(rect_x + rect_w), static_cast<std::int32_t>(rect_y + rect_h), scene_map_);
        if (scaled.right <= scaled.left || scaled.bottom <= scaled.top) {
            return;
        }
        rect = VkRect2D{{scaled.left, scaled.top},
                        {static_cast<std::uint32_t>(scaled.right - scaled.left),
                         static_cast<std::uint32_t>(scaled.bottom - scaled.top)}};
    }
    if (depth_enable && depth_clear_pending()) {
        const bool whole_depth = rect.offset.x == 0 && rect.offset.y == 0 &&
                                 rect.extent.width == static_cast<std::uint32_t>(scene_w) &&
                                 rect.extent.height == static_cast<std::uint32_t>(scene_h);
        if (whole_depth) {
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
    const bool whole = rect.offset.x == 0 && rect.offset.y == 0 && rect.extent.width == static_cast<std::uint32_t>(scene_w) &&
                       rect.extent.height == static_cast<std::uint32_t>(scene_h);
    const bool every_channel = color_enable && (alpha_enable || !has_alpha);
    if (!scene_pass_open_ && every_channel && depth_enable && whole) {
        begin_scene_pass(&clear);
        return;
    }
    // The whole colour without depth while the pass is closed (the bloom's fresh target over the scene's depth): the pass
    // opens without reading the colour the clear replaces, which a tiled GPU would load from memory.
    // The opening's name in the [vk-gpu] report says what the clear was.
    const char* kind = !whole ? "clear_scene(part)"
                       : every_channel ? (depth_enable ? "clear_scene(colour+depth)" : "clear_scene(colour)")
                       : color_enable || alpha_enable ? (depth_enable ? "clear_scene(channels+depth)" : "clear_scene(channels)")
                       : "clear_scene(depth)";
    const VkPipeline depth_clear = depth_enable ? scene_utility_pipeline(SceneUtility::ClearDepth) : VK_NULL_HANDLE;
    // The whole depth while the pass is closed: the pass opens with the depth cleared instead of loading what the clear
    // replaces, and the stencil as the clear below leaves it (the draw keeps it, vkCmdClearAttachments clears it).
    const bool open_cleared_depth = !scene_pass_open_ && whole && depth_enable && !target_pass_active_;
    begin_scene_pass_with(0u, open_cleared_depth ? &clear : nullptr, !scene_pass_open_ && every_channel && whole, kind,
                          open_cleared_depth ? (depth_clear != VK_NULL_HANDLE ? 1u : 2u) : 0u);
    const VkCommandBuffer commands = commands_;
    const VkClearRect clear_rect{rect, 0, 1};
    if (color_enable || alpha_enable) {
        if (!has_alpha) {
            alpha_enable = true;
        }
        const VkPipeline rgba = scene_utility_pipeline(SceneUtility::ClearRgba);
        if (color_enable && alpha_enable && rgba != VK_NULL_HANDLE) {
            // Drawn: vkCmdClearAttachments races with draws on this driver (DeviceQuirks::clear_with_draw).
            const VkViewport viewport{0.0f, static_cast<float>(scene_h), static_cast<float>(scene_w),
                                      -static_cast<float>(scene_h), 0.0f, 1.0f};
            vkCmdSetViewport(commands, 0, 1, &viewport);
            vkCmdSetScissor(commands, 0, 1, &rect);
            vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, rgba);
            vkCmdPushConstants(commands, utility_layout_, kPushStages, 0, sizeof(float) * 4u, clear.color.data());
            vkCmdDraw(commands, 3, 1, 0, 0);
            reset_draw_bindings();
            bind_scene();
        } else if (color_enable && alpha_enable) {
            VkClearAttachment attachment{};
            attachment.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            attachment.colorAttachment = 0;
            std::memcpy(attachment.clearValue.color.float32, clear.color.data(), sizeof(float) * 4u);
            vkCmdClearAttachments(commands, 1, &attachment, 1, &clear_rect);
        } else {
            // One channel group only: a full-target quad with that write mask, clipped to the rectangle.
            const VkViewport viewport{0.0f, static_cast<float>(scene_h), static_cast<float>(scene_w),
                                      -static_cast<float>(scene_h), 0.0f, 1.0f};
            vkCmdSetViewport(commands, 0, 1, &viewport);
            vkCmdSetScissor(commands, 0, 1, &rect);
            vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              scene_utility_pipeline(color_enable ? SceneUtility::ClearRgb : SceneUtility::ClearAlpha));
            vkCmdPushConstants(commands, utility_layout_, kPushStages, 0, sizeof(float) * 4u, clear.color.data());
            vkCmdDraw(commands, 3, 1, 0, 0);
            reset_draw_bindings();
            bind_scene();
        }
    }
    if (depth_enable && !open_cleared_depth) {
        clear_depth_in_pass(clear.reversed_depth, rect);
    }
}

VkDescriptorSet RendererVk::allocate_frame_set(VkDescriptorSetLayout layout) {
    Frame& frame = *frame_;
    const VkDevice device = vk::Context::get().device();
    VkDescriptorSetAllocateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    info.descriptorPool = frame.overflow_descriptors.empty() ? frame.descriptors : frame.overflow_descriptors.back();
    info.descriptorSetCount = 1;
    info.pSetLayouts = &layout;
    VkDescriptorSet set = VK_NULL_HANDLE;
    VkResult result = vkAllocateDescriptorSets(device, &info, &set);
    if (result == VK_ERROR_OUT_OF_POOL_MEMORY || result == VK_ERROR_FRAGMENTED_POOL) {
        // A frame with more distinct tables than the pool holds: another pool, freed with the slot. The first one of a frame
        // has the size that satisfied the last such frame, each further one in the same frame twice the last, so a frame that
        // needs it every time makes the same pool again instead of a larger one each frame.
        const bool first_of_frame = frame.overflow_descriptors.empty();
        frame.overflow_sets = frame.overflow_sets == 0u ? vk_detail::kFrameSets
                              : first_of_frame           ? frame.overflow_sets
                              : frame.overflow_sets <= vk_detail::kMaxOverflowSets / 2u ? frame.overflow_sets * 2u
                                                                                        : vk_detail::kMaxOverflowSets;
        const VkDescriptorPool pool = vk_detail::create_pool(device, frame.overflow_sets, vk_detail::frame_pool(!bindless_, frame.overflow_sets));
        frame.overflow_descriptors.push_back(pool);
        info.descriptorPool = pool;
        result = vkAllocateDescriptorSets(device, &info, &set);
    }
    vk::check(result, "vkAllocateDescriptorSets");
    return set;
}

FrameMap RendererVk::output_frame_for_window() const {
    const auto aspect = galaxy::experimental_effective_host_aspect();
    const galaxy::ContentViewport content = galaxy::fit_content_viewport(
        static_cast<int>(host_window::backbuffer_width), static_cast<int>(host_window::backbuffer_height),
        aspect ? aspect->content_aspect : galaxy::kDesignContentAspect);
    if (content.width <= 0 || content.height <= 0) {
        return FrameMap{};
    }
    return FrameMap(static_cast<unsigned>(content.width), static_cast<unsigned>(content.height));
}

// The scene target and the output scene target trade places: targets, scale and their copy sources.
void RendererVk::swap_scene_sets() {
    end_scene_pass();
    std::swap(scene_, alt_scene_);
    std::swap(scene_map_, alt_scene_map_);
    output_scene_active_ = !output_scene_active_;
    reset_draw_bindings();
}

// The scene target and the target passes' set trade places (begin_target_pass): targets and scale, the output scene target's state kept.
void RendererVk::swap_target_set() {
    end_scene_pass();
    std::swap(scene_, target_scene_);
    std::swap(scene_map_, target_scene_map_);
    target_pass_active_ = !target_pass_active_;
    reset_draw_bindings();
}

bool RendererVk::begin_target_pass(std::uint64_t key, std::uint32_t width, std::uint32_t height) {
    if (target_pass_active_ || width == 0u || height == 0u) {
        return false;
    }
    TargetSlot& slot = target_slots_[key];
    if (slot.set.color_source == VK_NULL_HANDLE) {
        // Colour inputs of dropped slots come back once the GPU is past the commands that read them.
        const std::uint64_t completed = completed_fence_value();
        std::erase_if(retired_target_sources_, [&](const RetiredTable& retired) {
            if (retired.fence > completed) {
                return false;
            }
            target_free_sources_.push_back(retired.set);
            return true;
        });
        if (!target_free_sources_.empty()) {
            slot.set.color_source = target_free_sources_.back();
            target_free_sources_.pop_back();
        }
    }
    const FrameMap map(width, height);
    const bool kept = slot.set.color && slot.map == map;
    if (!kept) {
        if (slot.set.color || slot.set.depth) {
            destroy_scene_set(slot.set);
        }
        // A new set enters its attachment layouts (create_scene_set); the pass's first clear (RenderCore) fills it.
        if (!create_scene_set(slot.set, map, 1u, kFrameUnits, kFrameUnits, true)) {
            if (slot.set.color_source != VK_NULL_HANDLE) {
                target_free_sources_.push_back(slot.set.color_source);
            }
            target_slots_.erase(key);
            return false;
        }
        slot.set.color->layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        slot.map = map;
        slot.width = width;
        slot.height = height;
    }
    slot.used = frame_index_;
    target_key_ = key;
    std::swap(target_scene_, slot.set);
    target_scene_map_ = slot.map;
    swap_target_set();
    if (kept) {
        // Sampled since its last pass: drawn into again, after those reads.
        image_barrier(commands_, scene_.color->image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_NONE,
                      VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                      VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
        scene_.color->layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }
    bind_scene();
    return true;
}

void RendererVk::end_target_pass(std::uint8_t channels, TextureStore& textures) {
    if (!target_pass_active_) {
        return;
    }
    swap_target_set();
    if (frame_pass_color_key_ != 0u || frame_pass_depth_key_ != 0u) {
        close_frame_for_pass(textures);
    }
    // The picture is sampled until the key's next pass.
    vk::Texture& picture = *target_scene_.color;
    image_barrier(commands_, picture.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                  VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    picture.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    TargetSlot& slot = target_slots_[target_key_];
    std::swap(slot.set, target_scene_);
    slot.map = target_scene_map_;
    target_scene_map_ = FrameMap{};
    (void)publish_picture(target_key_, slot.width, slot.height, slot.set.color, channels, textures);
    // Keys unused for kTargetIdleFrames frames give back their render resources (their pictures stay).
    sweep_target_slots();
    bind_scene();
}

// A slot's targets go to the open frame's retired list; its colour input is reused once the commands recorded so far have run.
void RendererVk::retire_target_slot(TargetSlot& slot) {
    const VkDescriptorSet source = slot.set.color_source;
    destroy_scene_set(slot.set);
    if (source != VK_NULL_HANDLE) {
        retired_target_sources_.push_back({source, last_submitted_ + 1u});
    }
    slot.set.color_source = VK_NULL_HANDLE;
}

void RendererVk::sweep_target_slots() {
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

bool RendererVk::target_picture(std::uint64_t key, TextureHandle& out) const {
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

bool RendererVk::publish_picture(std::uint64_t key, std::uint32_t width, std::uint32_t height, gpu::TextureRef texture,
                                 std::uint8_t channels, TextureStore& textures) {
    const VkDevice device = vk::Context::get().device();
    Picture& picture = pictures_[key];
    if (picture.view != ~0u && picture.viewed == texture && picture.channels == channels && picture.width == width &&
        picture.height == height) {
        return false;
    }
    if (picture.view != ~0u) {
        textures.retire_view(picture.view, std::move(picture.viewed));
    }
    picture.view = textures.allocate_view();
    picture.viewed = std::move(texture);
    picture.width = width;
    picture.height = height;
    picture.channels = channels;
    const std::array<std::uint8_t, 4> swizzle = target_channel_swizzle(channels);
    if (picture.sampled != VK_NULL_HANDLE) {
        const VkImageView old = picture.sampled;
        vk::Context::get().defer([device, old] { vkDestroyImageView(device, old, nullptr); });
        picture.sampled = VK_NULL_HANDLE;
    }
    if (swizzle != std::array<std::uint8_t, 4>{0, 1, 2, 3}) {
        static constexpr VkComponentSwizzle kChannel[5] = {VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G,
                                                         VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A,
                                                         VK_COMPONENT_SWIZZLE_ONE};
        VkImageViewCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        info.image = picture.viewed->image;
        info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        info.format = picture.viewed->format;
        info.components = {kChannel[swizzle[0]], kChannel[swizzle[1]], kChannel[swizzle[2]], kChannel[swizzle[3]]};
        info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u};
        vk::check(vkCreateImageView(device, &info, nullptr, &picture.sampled), "vkCreateImageView (picture channels)");
    }
    textures.backend().write_view(picture.view, picture.sampled != VK_NULL_HANDLE ? picture.sampled
                                                                                 : picture.viewed->view);
    return true;
}

bool RendererVk::publish_frame_pass(const FramePassRequest& params, gpu::TextureRef texture, TextureStore& textures) {
    const FrameExtent size = frame_pass_extent(scene_map_);
    return publish_picture(params.key, size.width, size.height, std::move(texture), 0u, textures);
}

bool RendererVk::temporal_mode() const {
    const unsigned upscaler = get_render_config().upscaler;
#if defined(_WIN32)
    if (upscaler == 3u && streamline::dlss_supported()) {
        return true;
    }
#endif
    return (upscaler == 2u && fsr3_available() && vk::Context::get().temporal_upscaling()) ||
           (upscaler == 4u && vk::Context::get().xess_upscaling()) || upscaler == 5u;
}

bool RendererVk::motion_pass_only() const {
    return get_render_config().upscaler == 5u;
}

void RendererVk::prepare_temporal_upscaler() {
    const FrameMap render_map = output_scene_active_ ? alt_scene_map_ : scene_map_;
    const FrameMap output_map = upscale_output_;
    if (!temporal_mode() || output_map.empty() || render_map.width > output_map.width) {
        if (temporal_) {
            wait_for_gpu_idle();
            temporal_.reset();
        }
        temporal_pending_ = {};
        temporal_render_w_ = temporal_render_h_ = 0u;
        temporal_output_w_ = temporal_output_h_ = 0u;
        temporal_history_valid_ = false;
        return;
    }
    const unsigned rw = render_map.width;
    const unsigned rh = render_map.height;
    const unsigned ow = output_map.width;
    const unsigned oh = output_map.height;
    const RenderConfig cfg = get_render_config();
    const unsigned kind = cfg.upscaler;
    const unsigned quality = std::min(cfg.temporal_quality, 5u);
#if defined(_WIN32)
    // DLSS as Ray Reconstruction while the tracer runs, where it is installed and supported (FB, 22.20).
    const bool rr = kind == 3u && render_tracing() && streamline::dlss_rr_supported();
#else
    const bool rr = false;
#endif
    // A failed creation is not retried until its sizes or mode change.
    if (rw == temporal_render_w_ && rh == temporal_render_h_ && ow == temporal_output_w_ && oh == temporal_output_h_ &&
        kind == temporal_kind_ && (kind < 3u || (quality == temporal_quality_ && rr == temporal_rr_))) {
        if (temporal_pending_.valid() &&
            temporal_pending_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            temporal_ = temporal_pending_.get();
            temporal_history_valid_ = false;
            if (temporal_) {
                std::cout << "[Renderer] " << temporal_->name() << " " << rw << "x" << rh << " -> " << ow << "x" << oh << "\n";
            }
        }
        return;
    }
    if (temporal_) {
        wait_for_gpu_idle();
        temporal_.reset();
    }
    // A creation still running for older sizes is waited for and dropped.
    temporal_pending_ = {};
    temporal_history_valid_ = false;
    temporal_render_w_ = rw;
    temporal_render_h_ = rh;
    temporal_output_w_ = ow;
    temporal_output_h_ = oh;
    temporal_kind_ = kind;
    temporal_quality_ = quality;
    temporal_rr_ = rr;
#if defined(_WIN32)
    if (kind == 3u) {
        temporal_ = vk::streamline_vk::create_dlss_upscaler(rw, rh, ow, oh, quality, rr);
        if (temporal_) {
            std::cout << "[Renderer] " << temporal_->name() << " " << rw << "x" << rh << " -> " << ow << "x" << oh << "\n";
        }
        return;
    }
#endif
    if (kind == 5u) {
        // Two pipelines and three images: made here, as the frame begins.
        temporal_ = vk::create_sgsr2_upscaler(rw, rh, ow, oh, vk::kSceneColorFormat);
        if (temporal_) {
            std::cout << "[Renderer] " << temporal_->name() << " " << rw << "x" << rh << " -> " << ow << "x" << oh << "\n";
        }
        return;
    }
    const vk::Context& context = vk::Context::get();
    // Until it is ready the scene renders at the render scale without the upscale.
    temporal_pending_ = std::async(std::launch::async, [instance = context.instance(), device = context.device(),
                                                        physical = context.physical_device(), rw, rh, ow, oh, kind, quality] {
        return kind == 4u ? vk::create_xess_upscaler(instance, physical, device, ow, oh, quality)
                          : vk::create_fsr3_upscaler(device, physical, rw, rh, ow, oh);
    });
}

bool RendererVk::scene_temporal_begin(float& jitter_x, float& jitter_y) {
    jitter_x = 0.0f;
    jitter_y = 0.0f;
    scene_temporal_ = false;
    scene_motion_ = false;
    const unsigned rw = scene_map_.width;
    const unsigned ow = temporal_output_w_;
    // The context is replaced only by begin_frame (prepare_temporal_upscaler).
    if (!temporal_ || !temporal_mode() || output_scene_active_ || target_pass_active_ || rw != temporal_render_w_ ||
        scene_map_.height != temporal_render_h_) {
        temporal_history_valid_ = false;
        return false;
    }
    const unsigned phases = std::max(temporal_->jitter_phase_count(rw, ow), 1u);
    temporal_->jitter_offset(temporal_frame_ % phases, phases, jitter_x, jitter_y);
    scene_jitter_index_ = static_cast<unsigned>(temporal_frame_ % phases);
    scene_jitter_phases_ = phases;
    ++temporal_frame_;
    scene_jitter_[0] = jitter_x;
    scene_jitter_[1] = jitter_y;
    scene_temporal_ = true;
    scene_motion_ = true;
    return true;
}

void RendererVk::destroy_motion_framebuffers() {
    const VkDevice device = vk::Context::get().device();
    for (VkFramebuffer& framebuffer : motion_.framebuffers) {
        if (framebuffer != VK_NULL_HANDLE) {
            const VkFramebuffer retired = framebuffer;
            vk::Context::get().defer([device, retired] { vkDestroyFramebuffer(device, retired, nullptr); });
            framebuffer = VK_NULL_HANDLE;
        }
    }
    motion_.framebuffer_color = VK_NULL_HANDLE;
    motion_.framebuffer_depth = VK_NULL_HANDLE;
}

void RendererVk::trim_idle_effect_targets() {
    constexpr std::uint64_t kIdleFrames = 600u;
    if (frame_generation_running()) {
        fg_hudless_used_frame_ = frame_index_;
    }
    if (frame_index_ > fg_hudless_used_frame_ + kIdleFrames) {
        retire(std::move(fg_hudless_scene_color_));
        retire(std::move(fg_hudless_display_));
        retire(std::move(fg_hudless_target_));
        fg_hudless_scene_ = fg_hudless_taken_ = fg_hudless_ready_ = false;
    }
    if (hdr_capture_ && frame_index_ > hdr_capture_used_frame_ + kIdleFrames) {
        retire(std::move(hdr_capture_));
    }
    if (motion_.motion && !layer_window_ && frame_index_ > motion_used_frame_ + kIdleFrames) {
        destroy_motion_framebuffers();
        retire(std::move(motion_.motion));
        retire(std::move(motion_.reactive));
        retire(std::move(motion_.layer_color));
        retire(std::move(motion_.layer_trans));
        reactive_scene_ = false;
        motion_.map = FrameMap{};
    }
    // The output set and the wide set are the live scene target while a swap holds them; between scenes they are idle
    // sets of their own.
    if (alt_scene_.color && !output_scene_active_ && !scene_.wide && frame_index_ > output_scene_used_frame_ + kIdleFrames) {
        destroy_scene_set(alt_scene_);
        alt_scene_map_ = FrameMap{};
    }
    if (wide_scene_.color && !scene_.wide && !output_scene_active_ && !target_pass_active_ &&
        frame_index_ > wide_scene_used_frame_ + kIdleFrames) {
        destroy_scene_set(wide_scene_);
    }
}

bool RendererVk::ensure_motion_targets() {
    motion_used_frame_ = frame_index_;
    if (motion_.motion && motion_.map == scene_map_) {
        return true;
    }
    destroy_motion_framebuffers();
    retire(std::move(motion_.motion));
    retire(std::move(motion_.reactive));
    retire(std::move(motion_.layer_color));
    retire(std::move(motion_.layer_trans));
    layer_window_ = false;
    reactive_scene_ = false;
    motion_.map = FrameMap{};
    vk::Context& context = vk::Context::get();
    const std::uint32_t width = scene_.color->width;
    const std::uint32_t height = scene_.color->height;
    const VkImageUsageFlags usage =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    gpu::TextureRef motion = context.create_texture(width, height, 1u, VK_FORMAT_R16G16_SFLOAT, usage, true, "motion vectors");
    // The tracer writes into the reactive mask too (plan 16.4) where the device traces and R8 images store.
    VkImageUsageFlags reactive_usage = usage;
    reactive_storable_ = false;
    if (rt_capable(rt_caps())) {
        VkFormatProperties formats{};
        vkGetPhysicalDeviceFormatProperties(context.physical_device(), VK_FORMAT_R8_UNORM, &formats);
        reactive_storable_ = (formats.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) != 0u;
        reactive_usage |= reactive_storable_ ? VK_IMAGE_USAGE_STORAGE_BIT : 0u;
    }
    gpu::TextureRef reactive = context.create_texture(width, height, 1u, VK_FORMAT_R8_UNORM, reactive_usage, true, "reactive mask");
    if (!motion || !reactive) {
        std::cerr << "[Renderer] motion targets " << width << 'x' << height << " allocation failed\n";
        return false;
    }
    // The effects layer's two targets, at the scene target's size and, as the Direct3D 12 renderer's, in its own encoding;
    // without them the scene runs as before (begin_layer_window does nothing).
    gpu::TextureRef layer_color, layer_trans;
    if (layer_targets_ok_) {
        layer_color = context.create_texture(width, height, 1u, vk::kLayerFormat, usage, true, "effects layer colour");
        layer_trans = context.create_texture(width, height, 1u, vk::kLayerFormat, usage, true, "effects layer transmittance");
        if (!layer_color || !layer_trans) {
            std::cerr << "[Renderer] effects layer targets " << width << 'x' << height << " allocation failed\n";
            layer_color.reset();
            layer_trans.reset();
        }
    }
    run_one_time([&](VkCommandBuffer commands) {
        for (const vk::Texture* target : {motion.get(), reactive.get()}) {
            image_barrier(commands, target->image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
                          VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                          VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
        }
        // The layer targets start as an empty layer (nothing added, everything let through), as clear_layer_targets leaves them,
        // so a read before the first layer window sees that and not undefined contents.
        const VkClearColorValue nothing{};
        VkClearColorValue through{};
        for (float& channel : through.float32) {
            channel = 1.0f;
        }
        const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u};
        const vk::Texture* const layers[2] = {layer_color.get(), layer_trans.get()};
        for (unsigned index = 0u; index < 2u; ++index) {
            if (layers[index] == nullptr) {
                continue;
            }
            image_barrier(commands, layers[index]->image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
                          VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
            vkCmdClearColorImage(commands, layers[index]->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, index == 0u ? &nothing : &through,
                                 1u, &range);
            image_barrier(commands, layers[index]->image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                          VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                          VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
        }
    });
    motion_.motion = std::move(motion);
    motion_.reactive = std::move(reactive);
    motion_.layer_color = std::move(layer_color);
    motion_.layer_trans = std::move(layer_trans);
    motion_.map = scene_map_;
    return true;
}

VkFramebuffer RendererVk::motion_framebuffer(unsigned aux) {
    const VkImageView color = scene_.color->target_view;
    const VkImageView depth = scene_.depth->target_view;
    if (motion_.framebuffer_color != color || motion_.framebuffer_depth != depth) {
        destroy_motion_framebuffers();
        motion_.framebuffer_color = color;
        motion_.framebuffer_depth = depth;
    }
    VkFramebuffer& framebuffer = motion_.framebuffers[aux - 1u];
    if (framebuffer == VK_NULL_HANDLE) {
        // Aux 3: the reactive mask, then the effects layer's colour and transmittance (attachments 2, 3 and 4).
        const VkImageView views[5] = {color, depth, aux == 1u ? motion_.motion->target_view : motion_.reactive->target_view,
                                      aux == 3u ? motion_.layer_color->target_view : VK_NULL_HANDLE,
                                      aux == 3u ? motion_.layer_trans->target_view : VK_NULL_HANDLE};
        VkFramebufferCreateInfo info{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        info.renderPass = draw_layout_.aux_pass_for(scene_variant(), aux);
        info.attachmentCount = aux == 3u ? 5u : 3u;
        info.pAttachments = views;
        info.width = scene_.color->width;
        info.height = scene_.color->height;
        info.layers = 1;
        vk::check(vkCreateFramebuffer(vk::Context::get().device(), &info, nullptr, &framebuffer), "temporal scene framebuffer");
    }
    return framebuffer;
}

bool RendererVk::begin_motion_scene() {
    const bool cleared = std::exchange(motion_cleared_, false);
    reactive_scene_ = false;
    layer_window_ = false;
    if (!scene_motion_ || !ensure_motion_targets()) {
        return false;
    }
    if (!cleared) {
        clear_motion_targets();
    }
    reactive_scene_ = !motion_pass_only();
    return true;
}

// The layer opens when a scene writes the reactive mask and both targets exist: it is cleared (nothing added, everything
// let through) and the scene's blended draws that never read the scene write it too until the next scene.
void RendererVk::begin_layer_window() {
    if (!reactive_scene_ || layer_window_ || !motion_.layer_color || !motion_.layer_trans ||
        draw_layout_.aux_pass_for(scene_variant(), 3u) == VK_NULL_HANDLE) {
        return;
    }
    clear_layer_targets();
    layer_window_ = true;
}

void RendererVk::clear_layer_targets() {
    end_scene_pass("clear_layer_targets");
    const vk::Texture* targets[2] = {motion_.layer_color.get(), motion_.layer_trans.get()};
    vk::BarriersVk uses(commands_);
    for (const vk::Texture* target : targets) {
        uses.image(target->image, VK_IMAGE_ASPECT_COLOR_BIT, gpu::Use::ColorWrite, gpu::Use::CopyDest);
    }
    uses.issue();
    const VkClearColorValue nothing{};
    VkClearColorValue through{};
    for (float& channel : through.float32) {
        channel = 1.0f;
    }
    const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u};
    vkCmdClearColorImage(commands_, targets[0]->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &nothing, 1u, &range);
    vkCmdClearColorImage(commands_, targets[1]->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &through, 1u, &range);
    uses.undo();
}

// In the open scene pass (aux 0): the depth clear's draw keeps the stencil; without it, vkCmdClearAttachments clears both.
void RendererVk::clear_depth_in_pass(float reversed_depth, const VkRect2D& rect) {
    const VkCommandBuffer commands = commands_;
    const VkPipeline depth_clear = scene_utility_pipeline(SceneUtility::ClearDepth);
    if (depth_clear != VK_NULL_HANDLE) {
        const float scene_w = static_cast<float>(scene_.color->width);
        const float scene_h = static_cast<float>(scene_.color->height);
        const VkViewport viewport{0.0f, scene_h, scene_w, -scene_h, 0.0f, 1.0f};
        const float value[4] = {reversed_depth, 0.0f, 0.0f, 0.0f};
        vkCmdSetViewport(commands, 0, 1, &viewport);
        vkCmdSetScissor(commands, 0, 1, &rect);
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, depth_clear);
        vkCmdPushConstants(commands, utility_layout_, kPushStages, 0, sizeof(value), value);
        vkCmdDraw(commands, 3, 1, 0, 0);
        reset_draw_bindings();
        bind_scene();
    } else {
        // Reversed Z: the far plane is 0.
        VkClearAttachment attachment{};
        attachment.aspectMask = depth_aspects(depth_format_);
        attachment.clearValue.depthStencil = {reversed_depth, 0u};
        const VkClearRect clear_rect{rect, 0, 1};
        vkCmdClearAttachments(commands, 1, &attachment, 1, &clear_rect);
    }
}


bool RendererVk::defer_scene_depth_clear(std::uint32_t clear_rgba) {
    if (!scene_pass_open_ || held_probe_slots_.empty() || target_pass_active_) {
        return false;
    }
    pending_depth_ = scene_depth_attachment();
    pending_depth_values_ = make_scene_clear_values(clear_rgba, 0u);
    pending_depth_clear_ = scene_utility_pipeline(SceneUtility::ClearDepth) != VK_NULL_HANDLE ? 1u : 2u;
    return true;
}

// The waiting depth clear now: in the open pass once no held probe needs the depth it replaces; otherwise the pass ends
// (the held probes read) and the next opening clears it, at once when no pass was open.
void RendererVk::settle_depth_clear() {
    if (!depth_clear_pending()) {
        return;
    }
    if (scene_pass_open_ && held_probe_slots_.empty() && scene_pass_aux_ == 0u) {
        pending_depth_clear_ = 0u;
        scene_.depth_dirty |= scene_.samples > 1u;
        clear_depth_in_pass(pending_depth_values_.reversed_depth,
                            VkRect2D{{0, 0}, {scene_.color->width, scene_.color->height}});
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

bool RendererVk::begin_cleared_motion_scene(std::uint32_t clear_rgba) {
    // scene_temporal_begin's conditions, which the scene's begin tests next.
    if (scene_pass_open_ || target_pass_active_ || output_scene_active_ || !temporal_ || !temporal_mode() ||
        scene_map_.width != temporal_render_w_ || scene_map_.height != temporal_render_h_ || scene_.samples != 1u ||
        !ensure_motion_targets()) {
        return false;
    }
    if (!motion_pass_only()) {
        clear_motion_targets(false);  // the reactive mask, while no pass is open
    }
    const SceneClearValues clear = make_scene_clear_values(clear_rgba, 0u);
    begin_scene_pass_with(1u, &clear, false, "begin_cleared_motion_scene");
    motion_cleared_ = true;
    return true;
}

void RendererVk::clear_motion_targets(bool motion) {
    end_scene_pass();
    const VkClearColorValue zero{};
    const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u};
    // Nothing reads the reactive mask when the scene is one pass (motion_pass_only), and nothing writes it.
    const bool reactive = !motion_pass_only();
    for (const vk::Texture* target : {motion_.motion.get(), motion_.reactive.get()}) {
        if ((target == motion_.reactive.get() && !reactive) || (target == motion_.motion.get() && !motion)) {
            continue;
        }
        image_barrier(commands_, target->image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                      VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_CLEAR_BIT,
                      VK_ACCESS_2_TRANSFER_WRITE_BIT);
        vkCmdClearColorImage(commands_, target->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1u, &range);
        image_barrier(commands_, target->image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                      VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                      VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    }
}

bool RendererVk::ensure_output_set(const FrameMap& output, bool storage) {
    output_scene_used_frame_ = frame_index_;
    if (alt_scene_.color && alt_scene_map_ == output && (alt_scene_storage_ || !storage)) {
        return true;
    }
    if (alt_scene_.color) {
        destroy_scene_set(alt_scene_);
    }
    scene_set_storage_ = storage;
    const bool made = create_scene_set(alt_scene_, output);
    scene_set_storage_ = false;
    if (!made) {
        alt_scene_map_ = FrameMap{};
        return false;
    }
    alt_scene_map_ = output;
    alt_scene_storage_ = storage;
    return true;
}

vk::TemporalUpscaleInputs RendererVk::temporal_inputs(const vk::Texture& color, const vk::Texture& depth, const vk::Texture& motion,
                                                       const vk::Texture& reactive, const vk::Texture& output) const {
    const auto image = [](const vk::Texture& texture, VkFormat format, VkImageUsageFlags usage) {
        return vk::TemporalImage{texture.image, texture.view, VK_IMAGE_ASPECT_COLOR_BIT, format, texture.width, texture.height, usage};
    };
    constexpr VkImageUsageFlags kColor = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    vk::TemporalUpscaleInputs in{};
    in.color = image(color, color.format, kColor);
    in.depth = image(depth, depth_format_, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    in.depth.barrier_aspects = depth_aspects(depth_format_);
    in.motion = image(motion, VK_FORMAT_R16G16_SFLOAT, kColor | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    in.reactive = image(reactive, VK_FORMAT_R8_UNORM, kColor);
    in.output = image(output, output.format, kColor | VK_IMAGE_USAGE_STORAGE_BIT);
    in.output.view = output.target_view;
    in.camera_near = scene_lens_[0];
    in.camera_far = scene_lens_[1];
    in.fov_y = scene_lens_[2];
    in.view = scene_view_;
    in.view_known = scene_view_known_;
    return in;
}


bool RendererVk::record_temporal_upscale(const FrameMap& output) {
    end_scene_pass();
    settle_depth_clear();
    const GpuPassScope pass{*this, GpuPass::Upscale};
    if (!temporal_ || !motion_.motion || !scene_.color || !alt_scene_.color || scene_map_.width != temporal_render_w_ ||
        scene_map_.height != temporal_render_h_ || output.width != temporal_output_w_ || output.height != temporal_output_h_ ||
        scene_.wide) {
        temporal_history_valid_ = false;
        return false;
    }
    vk::TemporalUpscaleInputs in = temporal_inputs(*scene_.color, *scene_.depth, *motion_.motion, *motion_.reactive, *alt_scene_.color);
    in.commands = commands_;
    in.render_width = scene_map_.width;
    in.render_height = scene_map_.height;
    in.output_width = output.width;
    in.output_height = output.height;
    in.jitter_x = scene_jitter_[0];
    in.jitter_y = scene_jitter_[1];
    // The time since the last upscale, as measured.
    const std::int64_t now_us = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    in.frame_ms = temporal_last_upscale_us_ != 0 ? static_cast<float>(now_us - temporal_last_upscale_us_) * 0.001f : 0.0f;
    temporal_last_upscale_us_ = now_us;
    in.reset = !temporal_history_valid_;
    const bool recorded = temporal_->evaluate(in);
    if (recorded && swapchain_fsr_) {
        const unsigned width = swapchain_.image.width;
        const unsigned height = swapchain_.image.height;
        bool ready = fsr_generation_.matches(in.render_width, in.render_height, width, height);
        if (!ready) {
            vkDeviceWaitIdle(vk::Context::get().device());
            fsr_generation_.destroy(swapchain_.handle);
            ready = fsr_generation_.create(in.render_width, in.render_height, width, height, swapchain_.format);
        }
        if (ready) {
            (void)fsr_generation_.prepare(commands_, in);
        }
    } else if (recorded && frame_generation_running()) {
        // Back in their attachment layouts (temporal_barriers).
        vk::streamline_vk::tag_generation_inputs(commands_, in.depth, in.motion, in.render_width, in.render_height,
                                                 VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    }
    // The upscaler binds its own pipelines and descriptor sets.
    reset_draw_bindings();
    temporal_history_valid_ = recorded;
    return recorded;
}

// DLSS Frame Generation's HUD-less picture: the output scene right after the upscale, before the 2D layer draws into it.
void RendererVk::take_fg_hudless_scene() {
    fg_hudless_scene_ = false;
    if (!frame_generation_running() || !scene_.color) {
        return;
    }
    vk::Context& context = vk::Context::get();
    const vk::Texture& color = *scene_.color;
    if (!fg_hudless_scene_color_ || fg_hudless_scene_color_->width != color.width || fg_hudless_scene_color_->height != color.height) {
        vkDeviceWaitIdle(context.device());
        fg_hudless_scene_color_ = context.create_texture(color.width, color.height, 1u, color.format,
                                                         VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, true,
                                                         "frame generation scene");
        if (!fg_hudless_scene_color_) {
            return;
        }
    }
    const VkCommandBuffer commands = commands_;
    // The scene colour to a copy source and the (idle) copy target to a destination, in one call.
    vk::BarriersVk before(commands);
    before.image(color.image, VK_IMAGE_ASPECT_COLOR_BIT, gpu::Use::ColorWrite, gpu::Use::CopySource);
    before.image_scoped(fg_hudless_scene_color_->image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
                        VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    before.issue();
    VkImageCopy region{};
    region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u};
    region.dstSubresource = region.srcSubresource;
    region.extent = {color.width, color.height, 1u};
    vkCmdCopyImage(commands, color.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, fg_hudless_scene_color_->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   1u, &region);
    vk::BarriersVk after(commands);
    after.image(color.image, VK_IMAGE_ASPECT_COLOR_BIT, gpu::Use::CopySource, gpu::Use::ColorWrite);
    after.image(fg_hudless_scene_color_->image, VK_IMAGE_ASPECT_COLOR_BIT, gpu::Use::CopyDest, gpu::Use::PixelRead);
    after.issue();
    fg_hudless_scene_color_->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    fg_hudless_scene_ = true;
}

// The display copy of the taken scene: the same copy into the HUD-less display picture.
void RendererVk::copy_fg_hudless_display(const DisplayTexture& dest) {
    const FramePassGeometry sampling = frame_pass_geometry(dest.width, dest.height, scene_map_);
    const bool taken = fg_hudless_scene_;
    fg_hudless_scene_ = false;
    if (!taken || !dest.texture || !fg_hudless_scene_color_) {
        return;
    }
    vk::Context& context = vk::Context::get();
    if (!fg_hudless_display_ || fg_hudless_display_->width != dest.width || fg_hudless_display_->height != dest.height) {
        vkDeviceWaitIdle(context.device());
        fg_hudless_display_ = context.create_texture(dest.width, dest.height, 1u, vk::kSceneColorFormat,
                                                     VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, true,
                                                     "frame generation display picture");
        if (!fg_hudless_display_) {
            return;
        }
    }
    const VkCommandBuffer commands = commands_;
    end_scene_pass();
    VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    begin.renderPass = color_pass_;
    begin.framebuffer = framebuffer_for(*fg_hudless_display_);
    begin.renderArea = {{0, 0}, {dest.width, dest.height}};
    vkCmdBeginRenderPass(commands, &begin, VK_SUBPASS_CONTENTS_INLINE);
    const VkViewport viewport{0.0f, static_cast<float>(dest.height), static_cast<float>(dest.width), -static_cast<float>(dest.height), 0.0f, 1.0f};
    const VkRect2D scissor{{0, 0}, {dest.width, dest.height}};
    vkCmdSetViewport(commands, 0, 1, &viewport);
    vkCmdSetScissor(commands, 0, 1, &scissor);
    const bool display_filter = sampling.step_x == 1.0f && sampling.step_y == 1.0f && display_filter_pipeline_ != VK_NULL_HANDLE;
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, display_filter ? display_filter_pipeline_ : conversion_pipeline_);
    const VkDescriptorSet source = allocate_frame_set(utility_set_layout_);
    write_conversion_input(source, fg_hudless_scene_color_->view);
    vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, utility_layout_, 0, 1, &source, 0, nullptr);
    const SceneConversionShaderConstants constants =
        make_scene_conversion_constants(kCopyLayoutDisplay, sampling.src_x, sampling.src_y, sampling.step_x, sampling.step_y);
    vkCmdPushConstants(commands, utility_layout_, kPushStages, 0, sizeof(constants), &constants);
    vkCmdDraw(commands, 3, 1, 0, 0);
    vkCmdEndRenderPass(commands);
    fg_hudless_display_->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    reset_draw_bindings();
    bind_scene();
    fg_hudless_taken_ = true;
    fg_hudless_slot_ = dest.slot;
}

// The HUD-less display picture of the presented copy, drawn as the frame is into a swap-chain-sized target that frame
// generation reads at the present; without one this frame, no HUD-less tag.
void RendererVk::present_fg_hudless(const DisplayTexture& source, const galaxy::ContentViewport& content) {
    vk::Context& context = vk::Context::get();
    const VkCommandBuffer commands = commands_;
    const bool ready = fg_hudless_display_ && fg_hudless_taken_ && fg_hudless_slot_ == source.slot && fg_hudless_display_->width == source.width &&
                       fg_hudless_display_->height == source.height && post_.ready() && !present_black_;
    fg_hudless_taken_ = false;
    if (!ready) {
        vk::streamline_vk::tag_hudless(commands, nullptr);
        return;
    }
    const std::uint32_t width = swapchain_.image.width;
    const std::uint32_t height = swapchain_.image.height;
    constexpr VkImageUsageFlags kUsage =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (!fg_hudless_target_ || fg_hudless_target_->width != width || fg_hudless_target_->height != height) {
        vkDeviceWaitIdle(context.device());
        fg_hudless_target_ = context.create_texture(width, height, 1u, VK_FORMAT_R8G8B8A8_UNORM, kUsage, true, "frame generation HUD-less");
        if (!fg_hudless_target_) {
            vk::streamline_vk::tag_hudless(commands, nullptr);
            return;
        }
    }
    const auto allocate = [this](VkDescriptorSetLayout layout) { return allocate_frame_set(layout); };
    const vk::PostProcessorVk::PresentStep step =
        post_.prepare_present(commands, *fg_hudless_display_, fg_hudless_display_->width, fg_hudless_display_->height,
                              static_cast<std::uint32_t>(content.width), static_cast<std::uint32_t>(content.height), false, allocate);
    if (step.pipeline == VK_NULL_HANDLE) {
        vk::streamline_vk::tag_hudless(commands, nullptr);
        return;
    }
    VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    begin.renderPass = rgba8_pass_;
    begin.framebuffer = framebuffer_for(*fg_hudless_target_);
    begin.renderArea = {{0, 0}, {width, height}};
    vkCmdBeginRenderPass(commands, &begin, VK_SUBPASS_CONTENTS_INLINE);
    VkClearValue clear{};
    clear.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    const VkClearAttachment attachment{VK_IMAGE_ASPECT_COLOR_BIT, 0, clear};
    const VkClearRect rect{{{0, 0}, {width, height}}, 0, 1};
    vkCmdClearAttachments(commands, 1, &attachment, 1, &rect);
    const VkRect2D area{{content.left, content.top}, {static_cast<std::uint32_t>(content.width), static_cast<std::uint32_t>(content.height)}};
    const VkViewport viewport{static_cast<float>(area.offset.x), static_cast<float>(area.offset.y) + static_cast<float>(area.extent.height),
                              static_cast<float>(area.extent.width), -static_cast<float>(area.extent.height), 0.0f, 1.0f};
    vkCmdSetViewport(commands, 0, 1, &viewport);
    vkCmdSetScissor(commands, 0, 1, &area);
    post_.finish_present(commands, step, area, allocate);
    vkCmdEndRenderPass(commands);
    fg_hudless_target_->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    reset_draw_bindings();
    vk::TemporalImage picture{fg_hudless_target_->image, fg_hudless_target_->view, VK_IMAGE_ASPECT_COLOR_BIT, VK_FORMAT_R8G8B8A8_UNORM, width, height, kUsage};
    vk::streamline_vk::tag_hudless(commands, &picture);
    fg_hudless_ready_ = true;
}

bool RendererVk::begin_output_scene(bool resolve_msaa_only) {
    if (resolve_msaa_only) {
        const FrameMap output = output_frame_for_window();
        if (output_scene_active_ || scene_temporal_ || scene_.samples <= 1u || output != scene_map_) {
            return false;
        }
        return enter_output_scene(output);
    }
    const bool temporal = scene_temporal_;
    scene_temporal_ = false;
    if (output_scene_active_) {
        return false;
    }
    if (temporal) {
        const FrameMap upscaled_map(temporal_output_w_, temporal_output_h_);
        if (!upscaled_map.empty() && upscaled_map.width >= scene_map_.width &&
            ensure_output_set(upscaled_map, temporal_kind_ != 5u) && record_temporal_upscale(upscaled_map)) {
            swap_scene_sets();
            take_fg_hudless_scene();
            bind_scene();
            clear_scene(0u, false, false, true);
            return true;
        }
        temporal_history_valid_ = false;
    }
    const FrameMap output = temporal && !upscale_output_.empty() ? upscale_output_ : output_frame_for_window();
    // Nebula's screens with small text are drawn at the window's resolution whatever the HUD's (RendererD3D12::begin_output_scene).
    const bool full = temporal || (get_render_config().hud_output_resolution || render_internal_height(get_render_config()) != 0u) || sharp_2d_requested();
    if (!full || output.empty() || output.width < scene_map_.width ||
        (!temporal && output.width == scene_map_.width)) {
        return false;
    }

    return enter_output_scene(output);
}

// The whole scene scene target resampled into the output scene target with the display copy's identity settings (no filter, gamma 1.0,
// alpha kept), which then takes the scene target's place with its depth cleared to the far plane.
bool RendererVk::enter_output_scene(const FrameMap& output) {
    if (!ensure_output_set(output, false)) {
        return false;
    }
    const unsigned width = output.width;
    const unsigned height = output.height;
    const FramePassGeometry sampling = frame_pass_geometry(width, height, scene_map_);
    record_conversion_blit(*alt_scene_.color, width, height, kCopyLayoutDisplay, sampling.src_x, sampling.src_y, sampling.step_x, sampling.step_y);
    end_scene_pass();
    image_barrier(commands_, alt_scene_.color->image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    alt_scene_.color->layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    swap_scene_sets();
    bind_scene();
    clear_scene(0u, false, false, true);
    return true;
}

void RendererVk::end_output_scene() {
    if (!output_scene_active_) {
        return;
    }

    swap_scene_sets();
    bind_scene();
}

void RendererVk::record_scene_aa() {
    // A temporal upscaler antialiases the jittered scene itself (RendererD3D12::record_scene_aa).
    if (!post_.ready() || scene_temporal_ || !scene_.color || post_.settings().aa == PostProcessSettings::Aa::kOff) {
        return;
    }
    const GpuPassScope pass{*this, GpuPass::SceneAa};

    // The picture the display copy takes, against its size on screen, decides AUTO.
    const auto aspect = galaxy::experimental_effective_host_aspect();
    const galaxy::ContentViewport content = galaxy::fit_content_viewport(
        static_cast<int>(host_window::backbuffer_width), static_cast<int>(host_window::backbuffer_height),
        aspect ? aspect->content_aspect : galaxy::kDesignContentAspect);
    end_scene_pass();
    const vk::Texture& color = *scene_.color;
    const auto allocate = [this](VkDescriptorSetLayout layout) { return allocate_frame_set(layout); };
    const AaRun ran = post_.prepare_scene_aa(
        commands_, color, color.width, color.height, scene_map_.width, scene_map_.height,
        static_cast<unsigned>(std::max(content.width, 1)), static_cast<unsigned>(std::max(content.height, 1)),
        allocate);
    begin_scene_pass();
    post_.finish_scene_aa(commands_, ran, color.width, color.height, allocate,
                          draw_layout_.pass_for(scene_samples_log2(), false), scene_samples_log2());
    reset_draw_bindings();
}

RendererVk::RtCommands RendererVk::rt_commands_begin() {
    end_scene_pass();
    rt_outer_pass_ = current_pass_;
    gpu_pass(GpuPass::RayTracing);
    rt_textures_.set = bindless_images_;
    rt_textures_.layout = bindless_images_ != VK_NULL_HANDLE ? draw_layout_.sets[1] : VK_NULL_HANDLE;
    return {commands_, &rt_textures_};
}

void RendererVk::rt_commands_end() { gpu_pass(rt_outer_pass_); }

bool RendererVk::rt_depth_begin(rt::TextureView& depth, unsigned& width, unsigned& height, rt::ImageFormat& color,
                                rt::TextureView* color_view) {
    if (!scene_.color || !scene_.depth || scene_.samples > 1u || (scene_.color->usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) == 0u ||
        (scene_.color->format != vk::kSceneColorFormat && scene_.color->format != VK_FORMAT_R16G16B16A16_SFLOAT)) {
        return false;
    }
    end_scene_pass();
    settle_depth_clear();
    image_barrier(commands_, scene_.depth->image, depth_aspects(depth_format_), VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                  kDepthSampleLayout, VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                  VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    depth = {scene_.depth->view, static_cast<std::uint32_t>(kDepthSampleLayout)};
    rt_color_read_ = color_view != nullptr && (scene_.color->usage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0u;
    if (color_view != nullptr && !rt_color_read_) {
        rt_depth_end();
        return false;
    }
    if (rt_color_read_) {
        image_barrier(commands_, scene_.color->image, VK_IMAGE_ASPECT_COLOR_BIT, gpu::Use::ColorWrite, gpu::Use::ComputeRead);
        *color_view = {scene_.color->view, static_cast<std::uint32_t>(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)};
    }
    width = scene_.depth->width;
    height = scene_.depth->height;
    color = scene_.color->format == vk::kSceneColorFormat ? rt::ImageFormat::Rgb10a2 : rt::ImageFormat::Rgba16f;
    return true;
}

void RendererVk::rt_depth_end() {
    if (rt_color_read_) {
        image_barrier(commands_, scene_.color->image, VK_IMAGE_ASPECT_COLOR_BIT, gpu::Use::ComputeRead, gpu::Use::ColorWrite);
        rt_color_read_ = false;
    }
    image_barrier(commands_, scene_.depth->image, depth_aspects(depth_format_), kDepthSampleLayout,
                  VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_NONE,
                  VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
                  VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
}

void RendererVk::destroy_item_framebuffer() {
    if (item_framebuffer_ != VK_NULL_HANDLE) {
        const VkDevice device = vk::Context::get().device();
        const VkFramebuffer retired = item_framebuffer_;
        vk::Context::get().defer([device, retired] { vkDestroyFramebuffer(device, retired, nullptr); });
        item_framebuffer_ = VK_NULL_HANDLE;
    }
    item_framebuffer_depth_ = VK_NULL_HANDLE;
}

VkFramebuffer RendererVk::item_framebuffer() {
    const VkImageView depth = scene_.depth->target_view;
    if (item_framebuffer_ != VK_NULL_HANDLE && item_framebuffer_depth_ != depth) {
        destroy_item_framebuffer();
    }
    if (item_framebuffer_ == VK_NULL_HANDLE) {
        const VkImageView views[2] = {item_target_->target_view, depth};
        VkFramebufferCreateInfo info{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        info.renderPass = draw_layout_.item_pass;
        info.attachmentCount = 2u;
        info.pAttachments = views;
        info.width = item_target_->width;
        info.height = item_target_->height;
        info.layers = 1;
        vk::check(vkCreateFramebuffer(vk::Context::get().device(), &info, nullptr, &item_framebuffer_), "ray tracing prepass framebuffer");
        item_framebuffer_depth_ = depth;
    }
    return item_framebuffer_;
}

bool RendererVk::rt_items_begin() {
    if (!scene_.color || !scene_.depth || scene_.samples > 1u || target_pass_active_ || draw_layout_.item_pass == VK_NULL_HANDLE) {
        return false;
    }
    if (!item_target_ || item_target_->width != scene_.depth->width || item_target_->height != scene_.depth->height) {
        end_scene_pass();
        destroy_item_framebuffer();
        retire(std::move(item_target_));
        item_target_ = vk::Context::get().create_texture(scene_.depth->width, scene_.depth->height, 1u, VK_FORMAT_R32G32_UINT,
                                                         VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, true,
                                                         "ray tracing items");
        if (!item_target_) {
            return false;
        }
    }
    item_clear_pending_ = true;
    return true;
}

bool RendererVk::rt_light_begin(rt::TextureView& depth, rt::TextureView& items, unsigned& width, unsigned& height) {
    if (!scene_.depth || !item_target_ || scene_.samples > 1u) {
        return false;
    }
    if (item_clear_pending_) {
        begin_scene_pass_with(kAuxPrepass, nullptr);  // the prepass drew nothing: the item target is cleared all the same
    }
    end_scene_pass();
    settle_depth_clear();
    image_barrier(commands_, scene_.depth->image, depth_aspects(depth_format_), VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                  kDepthSampleLayout, VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                  VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    image_barrier(commands_, item_target_->image, VK_IMAGE_ASPECT_COLOR_BIT, gpu::Use::ColorWrite, gpu::Use::ComputeRead);
    depth = {scene_.depth->view, static_cast<std::uint32_t>(kDepthSampleLayout)};
    items = {item_target_->view, static_cast<std::uint32_t>(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)};
    width = scene_.depth->width;
    height = scene_.depth->height;
    return true;
}

void RendererVk::rt_light_end() {
    image_barrier(commands_, item_target_->image, VK_IMAGE_ASPECT_COLOR_BIT, gpu::Use::ComputeRead, gpu::Use::ColorWrite);
    image_barrier(commands_, scene_.depth->image, depth_aspects(depth_format_), kDepthSampleLayout,
                  VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_NONE,
                  VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
                  VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
}

bool RendererVk::rt_reactive_begin(rt::TextureView& reactive) {
    if (!reactive_scene_ || !motion_.reactive || !reactive_storable_) {
        return false;
    }
    end_scene_pass();
    image_barrier(commands_, motion_.reactive->image, VK_IMAGE_ASPECT_COLOR_BIT, gpu::Use::ColorWrite, gpu::Use::ComputeWrite);
    reactive = {motion_.reactive->view, static_cast<std::uint32_t>(VK_IMAGE_LAYOUT_GENERAL)};
    return true;
}

void RendererVk::rt_reactive_end() {
    image_barrier(commands_, motion_.reactive->image, VK_IMAGE_ASPECT_COLOR_BIT, gpu::Use::ComputeWrite, gpu::Use::ColorWrite);
}

void RendererVk::rt_unpublish_lights(TextureStore& textures) {
    const VkDevice device = vk::Context::get().device();
    for (unsigned which = 0u; which < kRtLightPictures; ++which) {  // light, added, mirror, facing, volume, scatter, transmit, indirect, two guides
        const auto found = pictures_.find(kRtLightPicture + which);
        if (found == pictures_.end()) {
            continue;
        }
        Picture& picture = found->second;
        if (picture.view != ~0u) {
            textures.retire_view(picture.view, std::move(picture.viewed));
        }
        if (picture.sampled != VK_NULL_HANDLE) {
            const VkImageView old = picture.sampled;
            vk::Context::get().defer([device, old] { vkDestroyImageView(device, old, nullptr); });
        }
        pictures_.erase(found);
    }
}

std::uint32_t RendererVk::rt_publish_light(void* image, TextureStore& textures, unsigned which) {
    auto* const texture = static_cast<vk::Texture*>(image);
    if (texture == nullptr) {
        return null_texture_slot_;
    }
    // The tracer owns the image (and frees it frames after its last use): the picture only names it.
    static_cast<void>(publish_picture(kRtLightPicture + which, texture->width, texture->height, gpu::TextureRef(gpu::TextureRef(), texture),
                                      0u, textures));
    return pictures_[kRtLightPicture + which].view;
}

void RendererVk::rt_copy_to_scene(void* image) {
    const auto* const source = static_cast<const vk::Texture*>(image);
    if (source == nullptr || !scene_.color || source->width != scene_.color->width || source->height != scene_.color->height ||
        source->format != scene_.color->format) {
        return;
    }
    end_scene_pass();
    const vk::Texture& color = *scene_.color;
    vk::BarriersVk before(commands_);
    before.image(color.image, VK_IMAGE_ASPECT_COLOR_BIT, gpu::Use::ColorWrite, gpu::Use::CopyDest);
    before.issue();
    VkImageCopy region{};
    region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u};
    region.dstSubresource = region.srcSubresource;
    region.extent = {color.width, color.height, 1u};
    vkCmdCopyImage(commands_, source->image, VK_IMAGE_LAYOUT_GENERAL, color.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1u, &region);
    vk::BarriersVk after(commands_);
    after.image(color.image, VK_IMAGE_ASPECT_COLOR_BIT, gpu::Use::CopyDest, gpu::Use::ColorWrite);
    after.issue();
}

// The occlusion of the opaque scene: the scene target pass ends for its compute passes, which read the depth, and the
// multiply draws in it again.
void RendererVk::record_scene_ao() {
    if (!ao_.ready() || !scene_.color || !scene_.depth) {
        return;
    }
    const GpuPassScope pass{*this, GpuPass::SceneAo};

    end_scene_pass();
    settle_depth_clear();
    if (scene_.samples > 1u) {
        resolve_scene_depth(commands_);
    }
    const VkImageAspectFlags aspects = depth_aspects(depth_format_);
    image_barrier(commands_, scene_.depth->image, aspects, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                  kDepthSampleLayout, VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                  VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                  VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    const auto allocate = [this](VkDescriptorSetLayout layout) { return allocate_frame_set(layout); };
    const vk::AmbientOcclusionVk::StageMarker mark = profile_frame_
        ? vk::AmbientOcclusionVk::StageMarker{[this](vk::AmbientOcclusionVk::Stage stage) {
              static constexpr GpuPass passes[] = {GpuPass::AoDistance, GpuPass::AoOcclusion,
                                                   GpuPass::AoDenoise, GpuPass::AoUpsample};
              gpu_pass(passes[static_cast<unsigned>(stage)]);
          }} : vk::AmbientOcclusionVk::StageMarker{};
    const bool made = ao_.prepare(commands_, *scene_.depth, kDepthSampleLayout, scene_ao_view_, get_render_config().ambient_occlusion_ssao,
                                  scene_ao_objects_, matrix_ring_, allocate, mark);
    gpu_pass(GpuPass::AoApply);
    image_barrier(commands_, scene_.depth->image, aspects, kDepthSampleLayout,
                  VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_NONE,
                  VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
                  VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
    begin_scene_pass();
    if (made) {
        ao_.finish(commands_, draw_layout_.pass_for(scene_variant(), false), scene_samples_log2(), allocate);
    }
    reset_draw_bindings();
}

// The wide colour targets for the live scene target's size and samples, over its depth; its resolve framebuffer too, since
// the depth resolve writes depth alone. False when the device cannot make them: the scene stays in the scene target.
bool RendererVk::ensure_wide_set(SceneSet& set) {
    if (set.color && set.depth_serial == scene_.depth_serial && set.samples == scene_.samples) {
        set.resolve_framebuffer = scene_.samples > 1u ? scene_framebuffer(true) : VK_NULL_HANDLE;
        return true;
    }
    vk::Context& context = vk::Context::get();
    const VkDevice device = context.device();
    const std::uint32_t width = scene_.color->width;
    const std::uint32_t height = scene_.color->height;
    if (set.framebuffer != VK_NULL_HANDLE) {
        const VkFramebuffer retired = set.framebuffer;
        context.defer([device, retired] { vkDestroyFramebuffer(device, retired, nullptr); });
        set.framebuffer = VK_NULL_HANDLE;
    }
    retire(std::move(set.color));
    retire(std::move(set.ms_color));
    gpu::TextureRef color = context.create_texture(
        width, height, 1u, VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        true, "wide scene color");
    gpu::TextureRef ms_color;
    if (scene_.samples > 1u) {
        ms_color = context.create_texture(width, height, 1u, VK_FORMAT_R16G16B16A16_SFLOAT,
                                          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, true,
                                          "wide scene color MSAA", static_cast<VkSampleCountFlagBits>(scene_.samples));
    }
    if (!color || (scene_.samples > 1u && !ms_color)) {
        std::cerr << "[scene-camera] wide scene " << width << 'x' << height << " allocation failed\n";
        return false;
    }
    const unsigned samples_log2 = scene_samples_log2();
    const VkImageView single[2] = {color->target_view, scene_.depth->target_view};
    const VkImageView multi[3] = {scene_.samples > 1u ? ms_color->target_view : VK_NULL_HANDLE,
                                  scene_.samples > 1u ? scene_.ms_depth->target_view : VK_NULL_HANDLE, color->target_view};
    VkFramebufferCreateInfo info{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    info.renderPass = draw_layout_.pass_for(samples_log2 | kSceneVariantHdr, false);
    info.attachmentCount = scene_.samples > 1u ? 3u : 2u;
    info.pAttachments = scene_.samples > 1u ? multi : single;
    info.width = width;
    info.height = height;
    info.layers = 1;
    vk::check(vkCreateFramebuffer(device, &info, nullptr, &set.framebuffer), "wide scene framebuffer");
    run_one_time([&](VkCommandBuffer commands) {
        for (const vk::Texture* target : {color.get(), ms_color.get()}) {
            if (target == nullptr) continue;
            image_barrier(commands, target->image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
                          VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                          VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
        }
    });
    set.color = std::move(color);
    set.ms_color = std::move(ms_color);
    set.depth = scene_.depth;
    set.ms_depth = scene_.ms_depth;
    set.ms_depth_source = scene_.ms_depth_source;
    set.resolve_framebuffer = scene_.samples > 1u ? scene_framebuffer(true) : VK_NULL_HANDLE;
    set.samples = scene_.samples;
    set.depth_serial = scene_.depth_serial;
    set.wide = true;
    set.wide_read = false;
    set.turned = false;
    if (set.color_source == VK_NULL_HANDLE) {
        set.color_source = allocate_persistent_set(utility_set_layout_);
    }
    write_conversion_input(set.color_source, set.color->view);
    return true;
}

bool RendererVk::begin_wide_scene(bool wanted, const SceneClearValues* initial_clear) {
    if (!wanted || scene_.wide || !scene_camera_.ready() || !scene_.color || output_scene_active_ || target_pass_active_) {
        return false;
    }
    end_scene_pass();
    wide_scene_used_frame_ = frame_index_;
    if (!ensure_wide_set(wide_scene_)) {
        return false;
    }

    record_wide_begin(initial_clear);
    return true;
}

// What the scene target holds (its pass end resolved a multisampled one) goes on under the scene.
void RendererVk::record_wide_begin(const SceneClearValues* initial_clear) {
    const GpuPassScope pass{*this, GpuPass::Camera};
    end_scene_pass();
    SceneSet& set = wide_scene_;
    if (set.wide_read) {
        for (const vk::Texture* target : {set.color.get(), set.ms_color.get()}) {
            if (target == nullptr) continue;
            image_barrier(commands_, target->image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_NONE,
                          VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                          VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
        }
        set.wide_read = false;
    }
    if (initial_clear != nullptr) {
        // A full clear immediately before the first scene draw has no background picture to copy. Clear
        // the HDR attachment and its shared depth together, and keep the tile pass open for geometry.
        std::swap(scene_, set);
        wide_base_sampled_ = false;
        begin_scene_pass(initial_clear);
        reset_draw_bindings();
        return;
    }
    gpu::TextureRef picture = scene_.color;
    image_barrier(commands_, picture->image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    set.depth_dirty = scene_.depth_dirty;
    std::swap(scene_, set);
    begin_scene_pass_with(0u, nullptr, true);
    const auto allocate = [this](VkDescriptorSetLayout layout) { return allocate_frame_set(layout); };
    scene_camera_.widen(commands_, draw_layout_.pass_for(scene_variant(), false), scene_samples_log2(), *picture, allocate);
    // Single-sample depth needs no resolve into the base framebuffer. Keep this pass open for the scene's
    // geometry instead of spilling and reloading its full HDR colour just to restore an unused base image.
    wide_base_sampled_ = scene_.samples == 1u;
    if (!wide_base_sampled_) {
        end_scene_pass();
        image_barrier(commands_, picture->image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                      VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                      VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    }
    reset_draw_bindings();
}

void RendererVk::end_wide_scene(bool finish) {
    if (!scene_.wide) {
        return;
    }
    end_scene_pass();

    record_wide_end(finish);
}

void RendererVk::record_wide_end(bool finish) {
    if (!scene_.wide) {
        return;
    }
    const GpuPassScope pass{*this, GpuPass::Camera};
    end_scene_pass();
    settle_depth_clear();
    SceneSet& set = wide_scene_;
    const bool per_sample = scene_.samples > 1u && vk::Context::get().sample_rate_shading();
    gpu::TextureRef wide = scene_.color;
    gpu::TextureRef wide_ms = scene_.ms_color;
    for (const vk::Texture* target : {wide.get(), wide_ms.get()}) {
        if (target == nullptr) continue;
        image_barrier(commands_, target->image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                      VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                      VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                      VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    }
    const auto allocate = [this](VkDescriptorSetLayout layout) { return allocate_frame_set(layout); };
    const auto now = std::chrono::steady_clock::now();
    const float seconds = std::chrono::duration<float>(now - camera_metered_).count();
    camera_metered_ = now;
    if (scene_.samples > 1u) {
        resolve_scene_depth(commands_);
    }
    const VkImageAspectFlags aspects = depth_aspects(depth_format_);
    image_barrier(commands_, scene_.depth->image, aspects, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, kDepthSampleLayout,
                  VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                  VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    // After a load or a long pause the eye starts open.
    scene_camera_.meter(commands_, *wide, *scene_.depth, kDepthSampleLayout, seconds > 1.0f ? -1.0f : seconds, finish, allocate);
    image_barrier(commands_, scene_.depth->image, aspects, kDepthSampleLayout, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                  VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
                  VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
    const bool depth_dirty = scene_.depth_dirty;
    const bool resolve_output = scene_.samples > 1u && output_frame_for_window() == scene_map_ &&
                                ensure_output_set(scene_map_, false);
    if (wide_base_sampled_) {
        image_barrier(commands_, set.color->image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                      VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
        wide_base_sampled_ = false;
    }
    std::swap(scene_, set);
    set.wide_read = true;
    scene_.depth_dirty = depth_dirty;
    if (resolve_output) {
        // All 3D samples remain available to the tone mapper; its averaged result goes straight to the
        // single-sample picture used by post-AA and 2D. Do not expand it back into an MSAA attachment.
        swap_scene_sets();
        clear_scene(0u, false, false, true);
    }
    begin_scene_pass_with(0u, nullptr, true);
    scene_camera_.draw(commands_, draw_layout_.pass_for(scene_variant(), false), scene_samples_log2(),
                       resolve_output || per_sample ? *wide_ms : *wide, per_sample, allocate, resolve_output, finish);
    reset_draw_bindings();
}

// The game's bloom (game_bloom_vk.h) at this point of the stream: the scene target pass ends for the passes that copy or read
// the scene, and the composite is drawn in it again.
void RendererVk::game_bloom(std::uint32_t kind, std::uint32_t a, std::uint32_t b) {
    if (!bloom_.ready() || !scene_.color) {
        return;
    }
    const GpuPassScope pass{*this, GpuPass::Bloom};
    end_scene_pass();
    vk::GameBloomVk::Inputs in{};
    in.color = scene_.color.get();
    in.scale = static_cast<float>(scene_map_.height / kAuthoredLines);
    in.frame_width = std::min(scene_map_.width, scene_.color->width);
    in.frame_height = std::min(scene_map_.height, scene_.color->height);
    const auto allocate = [this](VkDescriptorSetLayout layout) { return allocate_frame_set(layout); };
    end_scene_pass();
    if (kind == NEBULA_PASS_BLOOM_HOLD) {
        if (scene_.samples <= 1u && !scene_.wide) {
            // The scene and the bloom's image trade places instead of the scene being copied. The game draws the
            // bloom's objects on black into the new target, over the same depth.
            bloom_.keep_picture(swap_scene_color(bloom_.take_kept_image()));
            clear_scene(0u, true, true, false);
            return;
        }
        static_cast<void>(bloom_.keep(commands_, in));
    } else {
        vk::GameBloomVk::Params p{};
        p.threshold = a & 0xFFu;
        p.intensity = a >> 24u;
        if (kind == NEBULA_PASS_BLOOM_NORMAL) {
            p.kind = vk::GameBloomVk::Kind::Normal;
            p.tap1 = (a >> 8u) & 0xFFu;
            p.tap2 = (a >> 16u) & 0xFFu;
            p.ring_x = std::bit_cast<float>(b);
        } else {
            p.kind = vk::GameBloomVk::Kind::Simple;
            p.channel = (a >> 8u) & 0xFFu;
            p.tap1 = (a >> 16u) & 0xFFu;
            p.ring_y = std::bit_cast<float>(b);
        }
        if (!(p.ring_x > 0.0f && p.ring_x < 16.0f) || !(p.ring_y > 0.0f && p.ring_y < 16.0f)) {
            p.ring_x = p.ring_y = 1.0f;
        }
        p.strength = static_cast<float>(std::min(get_render_config().bloom, kMaxBloom)) / 100.0f;
        if (bloom_.prepare(commands_, in, p, allocate)) {
            // The composite writes every channel of each pixel it covers, unblended (from the kept scene and the
            // levels): over the whole target the pass opens without loading the colour it replaces.
            begin_scene_pass_with(0u, nullptr,
                                  in.frame_width == scene_.color->width && in.frame_height == scene_.color->height);
            bloom_.finish(commands_, draw_layout_.pass_for(scene_variant(), false), scene_samples_log2(), allocate);
        }
    }
    reset_draw_bindings();
}

// A texture the GPU may still read: released once the submissions that can name it completed.
void RendererVk::retire(gpu::TextureRef texture) {
    if (!texture) {
        return;
    }
    frames_[frame_slot_].retired.push_back(std::move(texture));
}

VkDescriptorSet RendererVk::allocate_persistent_set(VkDescriptorSetLayout layout) {
    const VkDevice device = vk::Context::get().device();
    VkDescriptorSetAllocateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    info.descriptorPool = persistent_overflow_.empty() ? persistent_pool_ : persistent_overflow_.back();
    info.descriptorSetCount = 1;
    info.pSetLayouts = &layout;
    VkDescriptorSet set = VK_NULL_HANDLE;
    VkResult result = vkAllocateDescriptorSets(device, &info, &set);
    if (result == VK_ERROR_OUT_OF_POOL_MEMORY || result == VK_ERROR_FRAGMENTED_POOL) {
        // Twice the sets of the last pool (these pools live until the device does).
        persistent_overflow_sets_ = persistent_overflow_sets_ == 0u ? vk_detail::kPersistentSets : persistent_overflow_sets_ * 2u;
        const VkDescriptorPool pool = vk_detail::create_pool(
            device, persistent_overflow_sets_, vk_detail::scaled(vk_detail::kPersistentPool, persistent_overflow_sets_ / vk_detail::kPersistentSets));
        persistent_overflow_.push_back(pool);
        info.descriptorPool = pool;
        result = vkAllocateDescriptorSets(device, &info, &set);
    }
    vk::check(result, "vkAllocateDescriptorSets");
    return set;
}

void RendererVk::attach_texture_uploads(TextureStore& cache) {
    const VkCommandBuffer uploads = frames_[frame_slot_].uploads;
    if (uploads_store_ != &cache) {
        // New images are written on the transfer queue where the device has one of its own (texture_backend_vk.h).
        uploads_store_ = &cache;
        cache.backend().enable_async_uploads(kFramesInFlight, frame_slot_);
    }
    cache.backend().set_upload_list(uploads);
    cache.backend().prepare_null_view(uploads);
    if (bindless_ && mirrored_backend_ != &cache.backend() && cache.backend().ready()) {
        // Every view goes to the image array at its index from now on: a set for twice the views made so far (at least
        // kBindlessTextureSlotsMin), the null view in its last slot.
        const std::uint64_t wanted = std::max<std::uint64_t>(vk::kBindlessTextureSlotsFirst,
                                                             2u * std::uint64_t{cache.backend().live_views()});
        install_texture_array(cache, static_cast<std::uint32_t>(std::min<std::uint64_t>(wanted, texture_array_limit_)));
    }
}

// A set of `slots` image slots from a pool of its own (a pool is freed whole, so an old set goes with its pool).
void RendererVk::install_texture_array(TextureStore& cache, std::uint32_t slots) {
    const VkDevice device = vk::Context::get().device();
    VkDescriptorPool pool = VK_NULL_HANDLE;
    const VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, slots};
    VkDescriptorPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    pool_info.maxSets = 1;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes = &size;
    vk::check(vkCreateDescriptorPool(device, &pool_info, nullptr, &pool), "image array descriptor pool");
    VkDescriptorSetVariableDescriptorCountAllocateInfo variable{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO};
    variable.descriptorSetCount = 1;
    variable.pDescriptorCounts = &slots;
    VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocate.pNext = &variable;
    allocate.descriptorPool = pool;
    allocate.descriptorSetCount = 1;
    allocate.pSetLayouts = &draw_layout_.sets[1];
    VkDescriptorSet set = VK_NULL_HANDLE;
    const VkResult made = vkAllocateDescriptorSets(device, &allocate, &set);
    if (made != VK_SUCCESS) {
        vkDestroyDescriptorPool(device, pool, nullptr);
        vk::check(made, "image array descriptor set");
    }
    // Every slot of a new set holds the null view before any view is written (in chunks of kFillChunk), so a grow copies
    // defined descriptors only and a slot no view has taken yet reads an empty map.
    const std::uint32_t null_slot = slots - 1u;
    fill_slots(set, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 0u, slots,
               VkDescriptorImageInfo{VK_NULL_HANDLE, cache.backend().null_view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
    if (bindless_images_ == VK_NULL_HANDLE) {
        // First set: every view made so far is written, each later one when it is made.
        bindless_images_ = set;
        bindless_images_pool_ = pool;
        texture_array_slots_ = slots;
        null_texture_slot_ = null_slot;
        mirrored_store_ = &cache;
        mirrored_backend_ = &cache.backend();
        cache.backend().mirror_into(set, null_slot,
                                    [](void* renderer, std::uint32_t index) { static_cast<RendererVk*>(renderer)->grow_texture_array(index); },
                                    this);
        return;
    }
    // A larger set: every slot of the old one is copied (all hold a view or the null view; a slot of a view since freed
    // is copied as it is and read by nothing; the old null slot is the next view's index). Draws recorded so far keep the
    // old set; its pool is freed when this frame's slot comes round again, after its fence. The null slot moved, so the
    // walker writes its texture lists again (sampler_generation).
    VkCopyDescriptorSet copy{VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET};
    copy.srcSet = bindless_images_;
    copy.srcBinding = 0;
    copy.dstSet = set;
    copy.dstBinding = 0;
    copy.descriptorCount = texture_array_slots_;
    vkUpdateDescriptorSets(device, 0, nullptr, 1, &copy);
    frames_[frame_slot_].retired_pools.push_back(bindless_images_pool_);
    bindless_images_ = set;
    bindless_images_pool_ = pool;
    texture_array_slots_ = slots;
    null_texture_slot_ = null_slot;
    cache.backend().move_mirror(set, null_slot);
    ++sampler_generation_;
}

// A view index reached the end of the image array (TextureBackend::publish_view): twice as large, or as large as the
// index needs, up to what the device allows.
void RendererVk::grow_texture_array(std::uint32_t index) {
    std::uint64_t slots = std::max<std::uint64_t>(texture_array_slots_, vk::kBindlessTextureSlotsMin);
    while (slots < std::uint64_t{index} + 2u) {
        slots *= 2u;
    }
    slots = std::min<std::uint64_t>(slots, texture_array_limit_);
    if (mirrored_store_ == nullptr || slots < std::uint64_t{index} + 2u || slots <= texture_array_slots_) {
        return;  // publish_view reports the limit
    }
    install_texture_array(*mirrored_store_, static_cast<std::uint32_t>(slots));
}

void RendererVk::destroy_retired_pools(Frame& frame) noexcept {
    const VkDevice device = vk::Context::get().device();
    for (const VkDescriptorPool pool : frame.retired_pools) {
        vkDestroyDescriptorPool(device, pool, nullptr);
    }
    frame.retired_pools.clear();
}

std::uint32_t RendererVk::texture_slot(const TextureHandle& handle) const noexcept {
    return handle.resource != nullptr ? handle.srv_index : null_texture_slot_;
}

void RendererVk::write_sampler(unsigned slot, VkSampler sampler) {
    const VkDescriptorImageInfo image{sampler, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = bindless_samplers_;
    write.dstBinding = 0;
    write.dstArrayElement = slot;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    write.pImageInfo = &image;
    vkUpdateDescriptorSets(vk::Context::get().device(), 1, &write, 0, nullptr);
}

void RendererVk::fill_slots(VkDescriptorSet set, VkDescriptorType type, std::uint32_t first, std::uint32_t count,
                            const VkDescriptorImageInfo& info) {
    if (fill_infos_.size() != kFillChunk) {
        fill_infos_.resize(kFillChunk);
    }
    std::fill(fill_infos_.begin(), fill_infos_.end(), info);
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = set;
    write.dstBinding = 0;
    write.descriptorType = type;
    write.pImageInfo = fill_infos_.data();
    while (count > 0u) {
        write.dstArrayElement = first;
        write.descriptorCount = std::min(count, kFillChunk);
        vkUpdateDescriptorSets(vk::Context::get().device(), 1, &write, 0, nullptr);
        first += write.descriptorCount;
        count -= write.descriptorCount;
    }
}

// Every slot of the sampler array holds `sampler`: a slot no draw named yet reads the default sampler.
void RendererVk::fill_sampler_array(VkSampler sampler) {
    fill_slots(bindless_samplers_, VK_DESCRIPTOR_TYPE_SAMPLER, 0u, vk::kBindlessSamplerSlots,
               VkDescriptorImageInfo{sampler, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED});
}

std::uint32_t RendererVk::sampler_slot(const SamplerKey& key) {
    const RenderConfig cfg = get_render_config();
    const auto allocation = sampler_slots_.get_or_allocate(make_sampler_slot_key(key, cfg));
    if (allocation.index >= kSamplerHeapSlots) {
        throw std::runtime_error("[samplers] more new samplers in one frame than the sampler array holds");
    }
    if (allocation.created) {
        write_sampler(allocation.index, sampler_for(key, cfg));
    }
    return allocation.index;
}

void RendererVk::write_texture_list(const std::uint32_t* pairs, std::uint32_t count, gpu::Address& lists,
                                    std::uint32_t& first) {
    TextureListBuffer& list = texture_lists_[frame_slot_];
    if (!list.buffer || texture_lists_used_ + count > list.capacity) {
        // The old buffer is read by the draws already recorded: it goes with this frame.
        if (list.buffer) {
            frame_->retired_buffers.push_back(std::move(list.buffer));
        }
        const std::uint32_t capacity = std::max(4096u, 2u * std::max(list.capacity, count));
        list.buffer = vk::Context::get().create_buffer(std::uint64_t{capacity} * 8u, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                       "texture lists");
        if (!list.buffer) {
            list.capacity = 0u;
            throw std::runtime_error("[textures] the texture lists could not be made");
        }
        list.capacity = capacity;
        texture_lists_used_ = 0u;
    }
    std::memcpy(list.buffer->mapped + std::size_t{texture_lists_used_} * 8u, pairs, std::size_t{count} * 8u);
    lists = list.buffer->address;
    first = texture_lists_used_;
    texture_lists_used_ += count;
}

void RendererVk::bind_draw_textures(gpu::Address lists) {
    encoder_.set_texture_arrays(gpu::Table{reinterpret_cast<std::uint64_t>(bindless_images_)},
                                gpu::Table{reinterpret_cast<std::uint64_t>(bindless_samplers_)}, lists);
}

// Without descriptor indexing: a draw's table, as many maps as vk::kBoundTableSlots, the rest the null view.
void RendererVk::write_table(VkDescriptorSet set, const TextureHandle* handles, unsigned count, const TextureStore& cache) {
    std::array<VkDescriptorImageInfo, vk::kBoundTableSlots> images{};
    for (unsigned m = 0; m < vk::kBoundTableSlots; ++m) {
        const bool bound = m < count && handles[m].resource != nullptr;
        images[m] = {VK_NULL_HANDLE, bound ? cache.backend().view(handles[m].srv_index) : cache.backend().null_view(),
                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    }
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = set;
    write.dstBinding = 0;
    write.descriptorCount = vk::kBoundTableSlots;
    write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    write.pImageInfo = images.data();
    vkUpdateDescriptorSets(vk::Context::get().device(), 1, &write, 0, nullptr);
}

gpu::Table RendererVk::texture_table_for(const TextureHandle* handles, unsigned count, const TextureStore& cache) {
    const VkDescriptorSet set = allocate_frame_set(draw_layout_.sets[1]);
    write_table(set, handles, count, cache);
    return gpu::Table{reinterpret_cast<std::uint64_t>(set)};
}

gpu::Table RendererVk::persistent_texture_table(const TextureHandle* handles, unsigned count, const TextureStore& cache, gpu::Table old) {
    const std::uint64_t completed = completed_fence_value();
    std::erase_if(retired_tables_, [&](const RetiredTable& retired) {
        if (retired.fence > completed) {
            return false;
        }
        free_tables_.push_back(retired.set);
        return true;
    });
    if (old.ptr != 0u) {
        retire_texture_table(old);
    }
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (!free_tables_.empty()) {
        set = free_tables_.back();
        free_tables_.pop_back();
    } else {
        set = allocate_persistent_set(draw_layout_.sets[1]);
    }
    write_table(set, handles, count, cache);
    return gpu::Table{reinterpret_cast<std::uint64_t>(set)};
}

void RendererVk::retire_texture_table(gpu::Table table) {
    retired_tables_.push_back({reinterpret_cast<VkDescriptorSet>(table.ptr), last_submitted_ + 1u});
}

void RendererVk::free_target(std::uint32_t id, TextureStore& textures) {
    const VkDevice device = vk::Context::get().device();
    for (auto it = pictures_.begin(); it != pictures_.end();) {
        if (static_cast<std::uint32_t>(it->first) == id) {
            textures.retire_view(it->second.view, std::move(it->second.viewed));
            if (it->second.sampled != VK_NULL_HANDLE) {
                const VkImageView old = it->second.sampled;
                vk::Context::get().defer([device, old] { vkDestroyImageView(device, old, nullptr); });
            }
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
    // A target pass's set for this texture: ids are not used again, so no pass draws into it after this (a slot used to stay
    // until a later target pass swept it, and a scene with none kept the last scene's).
    for (auto it = target_slots_.begin(); it != target_slots_.end();) {
        if (static_cast<std::uint32_t>(it->first) == id && !(target_pass_active_ && it->first == target_key_)) {
            retire_target_slot(it->second);
            it = target_slots_.erase(it);
        } else {
            ++it;
        }
    }
}

gpu::TextureRef RendererVk::take_frame_picture(gpu::TextureRef fresh) {
    if (scene_.wide) {
        end_wide_scene();
    }
    end_scene_pass();
    return swap_scene_color(std::move(fresh));
}

void RendererVk::set_draw_aux(unsigned aux) {
    // Aux 3 needs the layer's window open (the walker asks for it only then); otherwise it is a reactive draw. Aux 4 draws in
    // the motion pass.
    const unsigned wanted = effective_draw_aux(aux);
    if (scene_pass_open_ && scene_pass_aux_ == wanted) {
        return;
    }
    begin_scene_pass_with(wanted, nullptr);
}

gpu::TextureArrays RendererVk::draw_texture_arrays(gpu::Address lists) {
    gpu::TextureArrays arrays;
    arrays.textures = gpu::Table{reinterpret_cast<std::uint64_t>(bindless_images_)};
    arrays.samplers = gpu::Table{reinterpret_cast<std::uint64_t>(bindless_samplers_)};
    arrays.lists = lists;
    return arrays;
}

// A chunk of a recorded pass replayed inline, as the walker would have drawn it.
void RendererVk::replay_draws(std::uint8_t aux, const gpu::DrawOp* ops, std::size_t count) {
    set_draw_aux(aux);
    gpu::replay_ops(encoder_, ops, count, [this](const gpu::TextureArrays& arrays) {
        encoder_.set_texture_arrays(arrays.textures, arrays.samplers, arrays.lists);
    });
}

bool RendererVk::parallel_record_begin(const std::uint8_t* chunk_aux, unsigned chunk_count) {
    constexpr unsigned kMaxRecordBuffers = 256u;
    std::vector<RecordCommands>& list = record_commands_[frame_slot_];
    if (chunk_count == 0u || record_used_[frame_slot_] + chunk_count > kMaxRecordBuffers) {
        return false;
    }
    const VkDevice device = vk::Context::get().device();
    record_base_ = record_used_[frame_slot_];
    while (list.size() < record_base_ + chunk_count) {
        RecordCommands made;
        VkCommandPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        pool_info.queueFamilyIndex = vk::Context::get().queue_family();
        vk::check(vkCreateCommandPool(device, &pool_info, nullptr, &made.pool), "vkCreateCommandPool (chunk)");
        VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocate.commandPool = made.pool;
        allocate.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
        allocate.commandBufferCount = 1u;
        const VkResult result = vkAllocateCommandBuffers(device, &allocate, &made.commands);
        if (result != VK_SUCCESS) {
            vkDestroyCommandPool(device, made.pool, nullptr);
            vk::check(result, "vkAllocateCommandBuffers (chunk)");
        }
        list.push_back(made);
    }
    record_used_[frame_slot_] += chunk_count;
    record_aux_.assign(chunk_aux, chunk_aux + chunk_count);
    record_variant_ = scene_variant();
    recording_parallel_.store(true, std::memory_order_release);
    return true;
}

void RendererVk::parallel_record_chunk(unsigned chunk, const gpu::DrawOp* ops, std::size_t count) {
    const VkCommandBuffer commands = record_commands_[frame_slot_][record_base_ + chunk].commands;
    const unsigned aux = effective_draw_aux(record_aux_[chunk]);
    // The render pass the chunk runs in (any compatible one: the load and store operations are the primary's).
    VkCommandBufferInheritanceInfo inherit{VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO};
    inherit.renderPass = aux != 0u ? draw_layout_.aux_pass_for(record_variant_, aux) : draw_layout_.pass_for(record_variant_, false);
    inherit.subpass = 0u;
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
    begin.pInheritanceInfo = &inherit;
    vk::check(vkBeginCommandBuffer(commands, &begin), "vkBeginCommandBuffer (chunk)");
    gpu::Encoder encoder;
    encoder.configure(encoding_layout_);
    encoder.attach(commands);
    gpu::replay_ops(encoder, ops, count, [&encoder](const gpu::TextureArrays& arrays) {
        encoder.set_texture_arrays(arrays.textures, arrays.samplers, arrays.lists);
    });
    vk::check(vkEndCommandBuffer(commands), "vkEndCommandBuffer (chunk)");
}

void RendererVk::parallel_record_end() {
    recording_parallel_.store(false, std::memory_order_release);
    const std::vector<RecordCommands>& list = record_commands_[frame_slot_];
    // The pass the walk had open ran its draws before; the chunks run in passes of their own (secondary buffers only).
    end_scene_pass("parallel recording");
    std::vector<VkCommandBuffer> run;
    for (std::size_t first = 0; first < record_aux_.size();) {
        const unsigned aux = effective_draw_aux(record_aux_[first]);
        run.clear();
        std::size_t next = first;
        while (next < record_aux_.size() && effective_draw_aux(record_aux_[next]) == aux) {
            run.push_back(list[record_base_ + next].commands);
            ++next;
        }
        begin_scene_pass_with(aux, nullptr, false, "parallel recording", 0u, true);
        vkCmdExecuteCommands(commands_, static_cast<std::uint32_t>(run.size()), run.data());
        end_scene_pass("parallel recording");
        first = next;
    }
    // After secondary buffers ran, the primary's bound pipeline, sets, buffers and dynamic state are undefined.
    encoder_.reset();
    reset_draw_bindings();
}

gpu::Address RendererVk::zero_stream(std::uint32_t bytes) {
    if (bytes > zero_stream_bytes_) {
        // Frames in flight read the old one: it goes with them.
        if (zero_vertices_) {
            frames_[frame_slot_].retired_buffers.push_back(std::move(zero_vertices_));
        }
        zero_stream_bytes_ = std::max(bytes, 1u << 16u);
        UploadBuffer memory = gpu::create_upload_memory(device(), zero_stream_bytes_, "zero stream");
        std::memset(memory.cpu, 0, zero_stream_bytes_);
        zero_vertices_ = std::move(memory.buffer);
        zero_stream_address_ = memory.gpu;
    }
    return zero_stream_address_;
}

// The sampler a key describes (layout: make_sampler_key in render_core.cpp; the D3D12 renderer's
// build_sampler_desc_from_key).
VkSampler RendererVk::sampler_for(const SamplerKey& key, const RenderConfig& cfg) {
    const SamplerCacheKey cache_key{key, sampler_config_key(cfg)};
    if (const auto it = samplers_.find(cache_key); it != samplers_.end()) {
        return it->second;
    }
    const vk::Context& context = vk::Context::get();
    const auto wrap = [](std::uint32_t wrap) {
        switch (wrap) {
        case 0: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        case 2: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        default: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        }
    };
    const float cfg_bias = static_cast<float>(std::clamp(cfg.texture_lod_bias, 0u, 8u)) * 0.5f;
    const unsigned anisotropy = std::clamp(cfg.anisotropic_filtering, 1u, 16u);
    const bool mag_linear = (key.filter & 16u) != 0u;
    const std::uint32_t minf = (key.filter >> 5u) & 7u;
    const float lod_bias = key.lod_bias;
    const float min_lod = key.min_lod, max_lod = key.max_lod;
    // The minified filter (texture_sampler.h min_filter_code): 4 linear, plus 1 the nearest level or 2 between levels.
    const bool min_linear = minf >= 4u;
    // 3: linear without anisotropy (make_sampler_key's arbitrary levels).
    const bool no_anisotropy = (minf & 3u) == 3u;
    const unsigned mip_mode = no_anisotropy ? 2u : minf & 3u;
    VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    info.magFilter = mag_linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    info.minFilter = min_linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    info.addressModeU = wrap(key.filter & 3u);
    info.addressModeV = wrap((key.filter >> 2u) & 3u);
    info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    const float max_bias = context.properties().limits.maxSamplerLodBias;
    info.mipLodBias = std::clamp(lod_bias + cfg_bias, -max_bias, max_bias);
    const bool use_anisotropic = anisotropy > 1u && mip_mode != 0u && min_linear && mag_linear &&
        !no_anisotropy && context.sampler_anisotropy();
    info.anisotropyEnable = use_anisotropic ? VK_TRUE : VK_FALSE;
    // D3D12_FILTER_ANISOTROPIC (the D3D12 renderer's) filters between mips linearly.
    info.mipmapMode = mip_mode == 2u || use_anisotropic ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    info.maxAnisotropy = use_anisotropic
        ? std::min(static_cast<float>(anisotropy), context.properties().limits.maxSamplerAnisotropy)
        : 1.0f;
    if (mip_mode == 0u) {
        info.minLod = 0.0f;
        info.maxLod = 0.0f;
    } else {
        info.minLod = min_lod;
        info.maxLod = max_lod > min_lod ? max_lod : min_lod;
    }
    VkSampler sampler = VK_NULL_HANDLE;
    vk::check(vkCreateSampler(context.device(), &info, nullptr, &sampler), "sampler");
    samplers_.emplace(cache_key, sampler);
    return sampler;
}

namespace {
constexpr SamplerKey kDefaultSamplerKey{1u | (1u << 2u) | (1u << 4u) | (5u << 5u), 0.0f, 32.0f, 0.0f};
}  // namespace

// A bound table's samplers (without descriptor indexing); the maps past `count` read the default sampler.
gpu::Table RendererVk::sampler_table_for(const SamplerKey* keys, unsigned count) {
    if (count == 0u) {
        return default_sampler_table_;
    }
    const RenderConfig cfg = get_render_config();
    const SamplerTableKey table_key = make_sampler_table_key(keys, count, cfg);
    if (const auto it = sampler_tables_.find(table_key); it != sampler_tables_.end()) {
        return gpu::Table{reinterpret_cast<std::uint64_t>(it->second)};
    }
    const VkDescriptorSet set = allocate_persistent_set(draw_layout_.sets[2]);
    std::array<VkDescriptorImageInfo, vk::kBoundTableSlots> samplers{};
    for (unsigned i = 0; i < vk::kBoundTableSlots; ++i) {
        samplers[i] = {sampler_for(i < count ? keys[i] : kDefaultSamplerKey, cfg), VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED};
    }
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = set;
    write.dstBinding = 0;
    write.descriptorCount = vk::kBoundTableSlots;
    write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    write.pImageInfo = samplers.data();
    vkUpdateDescriptorSets(vk::Context::get().device(), 1, &write, 0, nullptr);
    sampler_tables_.emplace(table_key, set);
    return gpu::Table{reinterpret_cast<std::uint64_t>(set)};
}

// Between frames: the samplers made again when the settings change, which also makes the walker ask for its lists or tables
// again. With descriptor indexing the sampler array starts again when it is nearly full too, slot 0 the default sampler
// (wrap, linear, mip point, every level) and the slots written once the GPU is past the frames that read them; else the
// default table of draws without samplers of their own is made again.
void RendererVk::reset_sampler_frame() {
    const RenderConfig cfg = get_render_config();
    const std::uint32_t config = sampler_config_key(cfg);
    if (bindless_) {
        const bool full = sampler_slots_.used() + vk_detail::kSamplerFrameRoom > kSamplerHeapSlots;
        if (sampler_slots_ready_ && config == sampler_config_ && !full) {
            return;
        }
        if (sampler_slots_ready_ && last_submitted_ != 0u) {
            wait_for_fence_value(last_submitted_);
        }
        sampler_slots_.reset();
        sampler_slots_ready_ = true;
        sampler_config_ = config;
        ++sampler_generation_;
        fill_sampler_array(sampler_for(kDefaultSamplerKey, cfg));
        return;
    }
    if (default_sampler_table_.ptr != 0u && config == sampler_config_) {
        return;
    }
    std::array<SamplerKey, vk::kBoundTableSlots> keys{};
    keys.fill(kDefaultSamplerKey);
    default_sampler_table_ = sampler_table_for(keys.data(), vk::kBoundTableSlots);
    sampler_config_ = config;
    ++sampler_generation_;
}

namespace {
// A material's constants as a binding sees them: vk::kMaterialConstantRange, cut to the device's limit.
[[nodiscard]] VkDeviceSize material_constant_bytes() {
    return std::min<VkDeviceSize>(vk::kMaterialConstantRange, vk::Context::get().properties().limits.maxUniformBufferRange);
}
}  // namespace

VkDescriptorSet RendererVk::constants_set(const VkBuffer* buffers, gpu::Address rows, gpu::Address instances,
                                          gpu::Address lists) {
    std::unique_lock<std::mutex> lock(constants_mutex_, std::defer_lock);
    if (recording_parallel_.load(std::memory_order_acquire)) {
        lock.lock();
    }
    for (const ConstantsKey& key : constants_sets_) {
        if (key.rows == rows && key.instances == instances && key.lists == lists && key.buffers[0] == buffers[0] &&
            key.buffers[1] == buffers[1] && key.buffers[2] == buffers[2]) {
            return key.set;
        }
    }
    const VkDevice device = vk::Context::get().device();
    const VkDescriptorSet set = allocate_frame_set(draw_layout_.sets[0]);
    const vk::Context::BufferOffset rows_at = vk::Context::get().resolve(rows);
    const vk::Context::BufferOffset instances_at = vk::Context::get().resolve(instances);
    // A dynamic binding's range must fit at every offset: the upload memory has room past its end.
    // The texture lists (descriptor indexing only); a draw without them reads the instance table there, never indexing it.
    const vk::Context::BufferOffset lists_at = lists != 0u ? vk::Context::get().resolve(lists) : instances_at;
    const VkDescriptorBufferInfo infos[6] = {
        {buffers[0], 0u, sizeof(core::ViewRows)},
        {buffers[1], 0u, material_constant_bytes()},
        {buffers[2], 0u, material_constant_bytes()},
        {rows_at.buffer, rows_at.offset, VK_WHOLE_SIZE},
        {instances_at.buffer, instances_at.offset, VK_WHOLE_SIZE},
        {lists_at.buffer, lists_at.offset, VK_WHOLE_SIZE},
    };
    const std::uint32_t bindings = bindless_ ? 6u : 5u;
    VkWriteDescriptorSet writes[6]{};
    for (std::uint32_t i = 0; i < bindings; ++i) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = set;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1u;
        writes[i].descriptorType = i < 3u ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[i].pBufferInfo = &infos[i];
    }
    vkUpdateDescriptorSets(device, bindings, writes, 0u, nullptr);
    constants_sets_.push_back({{buffers[0], buffers[1], buffers[2]}, rows, instances, lists, set});
    return set;
}

VkFramebuffer RendererVk::framebuffer_for(vk::Texture& texture) {
    if (texture.framebuffer == VK_NULL_HANDLE) {
        VkFramebufferCreateInfo info{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        info.renderPass = texture.format == vk::kSceneColorFormat ? color_pass_ : rgba8_pass_;
        info.attachmentCount = 1;
        info.pAttachments = &texture.target_view;
        info.width = texture.width;
        info.height = texture.height;
        info.layers = 1;
        vk::check(vkCreateFramebuffer(vk::Context::get().device(), &info, nullptr, &texture.framebuffer),
                  "copy framebuffer");
    }
    return texture.framebuffer;
}

void RendererVk::transition_scene_for_sampling(VkCommandBuffer commands, bool depth, VkImage color_image) {
    if (depth) {
        if (commands == commands_) {
            settle_depth_clear();
        }
        resolve_scene_depth(commands);
        image_barrier(commands, scene_.depth->image, depth_aspects(depth_format_),
                      VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, kDepthSampleLayout,
                      VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                      VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    } else {
        image_barrier(commands, color_image != VK_NULL_HANDLE ? color_image : scene_.color->image,
                      VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                      VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                      VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    }
}

void RendererVk::transition_scene_to_attachment(VkCommandBuffer commands, bool depth, VkImage color_image) {
    if (depth) {
        image_barrier(commands, scene_.depth->image, depth_aspects(depth_format_), kDepthSampleLayout,
                      VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_NONE,
                      VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
                      VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
    } else {
        image_barrier(commands, color_image != VK_NULL_HANDLE ? color_image : scene_.color->image,
                      VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_NONE,
                      VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                      VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    }
}

// The conversion blit (scene_conversion_shader_source): a full-screen triangle into `dest`, sampling the scene target colour
// or depth. The scene target leaves its attachment layout for the read and returns; `dest` ends shader-readable.
void RendererVk::record_conversion_blit(vk::Texture& dest, unsigned dest_w, unsigned dest_h,
                                        std::uint8_t copy_format, float src_x, float src_y, float step_x, float step_y) {
    end_scene_pass();
    const VkCommandBuffer commands = commands_;
    transition_scene_for_sampling(commands, false);
    VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    begin.renderPass = color_pass_;
    begin.framebuffer = framebuffer_for(dest);
    begin.renderArea = {{0, 0}, {dest_w, dest_h}};
    vkCmdBeginRenderPass(commands, &begin, VK_SUBPASS_CONTENTS_INLINE);
    const VkViewport viewport{0.0f, static_cast<float>(dest_h), static_cast<float>(dest_w),
                              -static_cast<float>(dest_h), 0.0f, 1.0f};
    const VkRect2D scissor{{0, 0}, {dest_w, dest_h}};
    vkCmdSetViewport(commands, 0, 1, &viewport);
    vkCmdSetScissor(commands, 0, 1, &scissor);
    // A display copy one scene target pixel to one display picture pixel without gamma: its filter alone (display_filter_shader_source).
    const bool display_filter = copy_format == kCopyLayoutDisplay &&
                                step_x == 1.0f && step_y == 1.0f &&
                                display_filter_pipeline_ != VK_NULL_HANDLE;
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      display_filter ? display_filter_pipeline_ : conversion_pipeline_);
    const VkDescriptorSet source = scene_color_source();
    vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, utility_layout_, 0, 1, &source, 0, nullptr);
    const SceneConversionShaderConstants constants =
        make_scene_conversion_constants(copy_format, src_x, src_y, step_x, step_y);
    vkCmdPushConstants(commands, utility_layout_, kPushStages, 0, sizeof(constants), &constants);
    vkCmdDraw(commands, 3, 1, 0, 0);
    vkCmdEndRenderPass(commands);
    dest.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    transition_scene_to_attachment(commands, false);
    reset_draw_bindings();
    bind_scene();
}

bool RendererVk::can_take_scene() const noexcept {
    return !frame_generation_running() && scene_.color &&
           !output_scene_active_ && !target_pass_active_ &&
           scene_.samples <= 1u && !scene_.wide;
}

bool RendererVk::take_scene(std::uint64_t key, gpu::TextureRef fresh, TextureStore& textures, gpu::TextureRef& released) {
    end_scene_pass();
    if (const auto it = pictures_.find(key); it != pictures_.end()) {
        released = it->second.viewed;
    }
    gpu::TextureRef picture = swap_scene_color(std::move(fresh));
    const FrameExtent size = frame_pass_extent(scene_map_);
    return publish_picture(key, size.width, size.height, std::move(picture), 0u, textures);
}

void RendererVk::open_frame_for_pass(std::uint64_t color_key, std::uint8_t channels, std::uint64_t depth_key,
                                     TextureStore& textures) {
    end_scene_pass();
    const VkCommandBuffer commands = commands_;
    const FrameExtent size = frame_pass_extent(scene_map_);
    if (color_key != 0u) {
        transition_scene_for_sampling(commands, false);
        (void)publish_picture(color_key, size.width, size.height, scene_.color, channels, textures);
    }
    if (depth_key != 0u) {
        settle_depth_clear();
        // Materials read their textures in the shader-read layout (TextureBackend).
        resolve_scene_depth(commands);
        image_barrier(commands, scene_.depth->image, depth_aspects(depth_format_),
                      VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                      VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                      VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        (void)publish_picture(depth_key, size.width, size.height, scene_.depth, 0u, textures);
    }
    frame_pass_color_key_ = color_key;
    frame_pass_depth_key_ = depth_key;
}

void RendererVk::close_frame_for_pass(TextureStore& textures) {
    const VkDevice device = vk::Context::get().device();
    const VkCommandBuffer commands = commands_;
    for (const std::uint64_t key : {frame_pass_color_key_, frame_pass_depth_key_}) {
        if (key == 0u) {
            continue;
        }
        if (const auto it = pictures_.find(key); it != pictures_.end()) {
            textures.retire_view(it->second.view, std::move(it->second.viewed));
            if (it->second.sampled != VK_NULL_HANDLE) {
                const VkImageView old = it->second.sampled;
                vk::Context::get().defer([device, old] { vkDestroyImageView(device, old, nullptr); });
            }
            pictures_.erase(it);
        }
    }
    if (frame_pass_color_key_ != 0u) {
        transition_scene_to_attachment(commands, false);
    }
    if (frame_pass_depth_key_ != 0u) {
        image_barrier(commands, scene_.depth->image, depth_aspects(depth_format_), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                      VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_NONE,
                      VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                      VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
    }
    frame_pass_color_key_ = 0u;
    frame_pass_depth_key_ = 0u;
    reset_draw_bindings();
}

gpu::TextureRef RendererVk::swap_scene_color(gpu::TextureRef fresh) {
    const GpuPassScope pass{*this, GpuPass::DisplayCopy};
    end_scene_pass();
    vk::Texture& picture = *scene_.color;
    if (fresh && (fresh->width != picture.width || fresh->height != picture.height ||
                  fresh->format != picture.format || fresh->target_view == VK_NULL_HANDLE ||
                  (fresh->usage & picture.usage) != picture.usage)) {
        retire(std::move(fresh));
    }
    if (!fresh) {
        fresh = vk::Context::get().create_texture(
            picture.width, picture.height, 1u, picture.format,
            picture.usage,
            true, "scene target color");
        if (!fresh) {
            throw std::runtime_error("scene target colour unavailable");
        }
    }
    const VkCommandBuffer commands = commands_;
    // The frame becomes a picture: shader-readable, as presentation reads it.
    image_barrier(commands, picture.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                  VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COPY_BIT,
                  VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_TRANSFER_READ_BIT);
    picture.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    // The idle picture becomes the scene target's colour after the reads of its last presents.
    image_barrier(commands, fresh->image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                  VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_NONE,
                  VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    fresh->layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    gpu::TextureRef handed = std::move(scene_.color);
    scene_.color = std::move(fresh);
    scene_.turned = true;
    turn_source_ = VK_NULL_HANDLE;
    reset_draw_bindings();
    bind_scene();
    return handed;
}

VkFramebuffer RendererVk::scene_framebuffer(bool resolved) {
    const bool multisampled = scene_.samples > 1u;
    if (!scene_.turned) {
        return resolved && multisampled ? scene_.resolve_framebuffer : scene_.framebuffer;
    }
    vk::Texture& color = *scene_.color;
    if (color.scene_framebuffer == VK_NULL_HANDLE || color.scene_depth_serial != scene_.depth_serial) {
        const VkDevice device = vk::Context::get().device();
        for (auto* stored : {&color.scene_framebuffer, &color.scene_resolve_framebuffer}) {
            if (*stored != VK_NULL_HANDLE) {
                const auto old = *stored;
                vk::Context::get().defer([device, old] { vkDestroyFramebuffer(device, old, nullptr); });
                *stored = VK_NULL_HANDLE;
            }
        }
        const auto make = [&](bool resolve) {
            const bool ms = multisampled && !resolve;
            const VkImageView views[] = {ms ? scene_.ms_color->target_view : color.target_view,
                ms ? scene_.ms_depth->target_view : scene_.depth->target_view, color.target_view};
            VkFramebufferCreateInfo info{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            info.renderPass = draw_layout_.pass_for(ms ? scene_samples_log2() : 0u, false);
            info.attachmentCount = ms ? 3u : 2u;
            info.pAttachments = views;
            info.width = color.width;
            info.height = color.height;
            info.layers = 1u;
            VkFramebuffer made = VK_NULL_HANDLE;
            vk::check(vkCreateFramebuffer(device, &info, nullptr, &made), "scene framebuffer (handed over)");
            return made;
        };
        color.scene_framebuffer = make(false);
        if (multisampled) color.scene_resolve_framebuffer = make(true);
        color.scene_depth_serial = scene_.depth_serial;
    }
    return resolved && multisampled ? color.scene_resolve_framebuffer : color.scene_framebuffer;
}

VkDescriptorSet RendererVk::scene_color_source() {
    if (!scene_.turned) {
        return scene_.color_source;
    }
    if (turn_source_ == VK_NULL_HANDLE || turn_source_image_ != scene_.color.get() || turn_source_frame_ != frame_index_ ||
        frame_ != &frames_[frame_slot_]) {
        turn_source_ = allocate_frame_set(utility_set_layout_);
        write_conversion_input(turn_source_, scene_.color->view);
        turn_source_image_ = scene_.color.get();
        turn_source_frame_ = frame_index_;
    }
    return turn_source_;
}

bool RendererVk::draw_frame_pass(const FramePassRequest& params, TextureStore& textures) {
    const GpuPassScope pass{*this, GpuPass::FramePass};
    ++gpu_window_.copies;
    const FrameExtent extent = frame_pass_extent(scene_map_);
    const unsigned w = extent.width;
    const unsigned h = extent.height;
    if (w == 0u || h == 0u) {
        return false;
    }
    // Keyed by the texture and its channels.
    const std::uint64_t dest_key = static_cast<std::uint64_t>(params.key) |
        (static_cast<std::uint64_t>(params.channels) << 48u);
    FramePassTarget& dest = frame_pass_targets_[dest_key];
    if (!dest.texture || dest.width != w || dest.height != h) {
        if (dest.texture) {
            retire(std::move(dest.texture));
        }
        dest.texture = vk::Context::get().create_texture(
            w, h, 1u, vk::kSceneColorFormat,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            true, nullptr);
        if (!dest.texture) {
            frame_pass_targets_.erase(dest_key);
            static std::atomic<unsigned> reported{0u};
            if (reported.fetch_add(1u, std::memory_order_relaxed) < 8u) {
                std::cerr << "[frame-pass] destination allocation failed size=" << w << 'x' << h
                          << "; copy skipped\n";
            }
            return false;
        }
        dest.width = w;
        dest.height = h;
    }
    FramePassTarget& current = dest;
    vk::Texture& texture = *current.texture;
    const FramePassGeometry sampling = frame_pass_geometry(w, h, scene_map_);
    record_conversion_blit(texture, w, h, params.channels, sampling.src_x, sampling.src_y, sampling.step_x, sampling.step_y);
    return publish_frame_pass(params, current.texture, textures);
}

// One dispatch (ScreenProbeCs): each point's pixel (screen_probe_pixel) into the slot's pixels, the scene depth sampled
// there into its values, which the CPU reads once the frame's fence has passed.
bool RendererVk::record_screen_probes(std::size_t slot, std::span<const ScreenProbe> probes, bool compared) {
    if (slot >= probe_slots_.size()) probe_slots_.resize(slot + 1u);
    ProbeSlot& out = probe_slots_[slot];
    if (out.fence != 0u || probes.empty()) {
        return false;
    }
    out.tested = 0u;
    settle_depth_clear();  // asked after a waiting clear: they see it
    // Asked inside the scene target pass, compared points are tested there: the pass goes on, where reading the depth
    // would end it (a store and a load of the whole target on a tiled GPU) before the next draw that writes depth.
    if (compared && scene_pass_open_ && test_screen_probes(out, probes)) {
        out.fence = kProbesInFrame;  // fenced at end_frame
        return true;
    }
    vk::Context& context = vk::Context::get();
    if (out.capacity < probes.size()) {
        out.pixels = context.create_buffer(probes.size() * sizeof(ScreenProbePixel), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                           "screen probe pixels");
        out.values = context.create_readback_buffer(probes.size() * sizeof(float), "screen probe values",
                                                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        if (!out.pixels || !out.values) throw std::runtime_error("screen probe buffers unavailable");
        out.capacity = probes.size();
    }
    // The slot is idle (fence 0): the GPU has finished with its pixels.
    for (std::size_t i = 0; i < probes.size(); ++i) {
        const ScreenProbePixel pixel = screen_probe_pixel(probes[i], scene_map_);
        std::memcpy(out.pixels->mapped + i * sizeof(ScreenProbePixel), &pixel, sizeof(pixel));
    }
    out.fence = kProbesInFrame;  // fenced at end_frame
    const auto count = static_cast<std::uint32_t>(probes.size());
    if (scene_pass_open_) {
        out.held = count;
        held_probe_slots_.push_back(slot);
        return true;
    }
    dispatch_screen_probes(slot, count);
    return true;
}

void RendererVk::read_held_probes() {
    std::vector<std::size_t> held;
    held.swap(held_probe_slots_);
    for (const std::size_t slot : held) {
        ProbeSlot& out = probe_slots_[slot];
        const std::uint32_t count = std::exchange(out.held, 0u);
        if (count != 0u) {
            dispatch_screen_probes(slot, count);
        }
    }
    held.clear();
    if (held_probe_slots_.empty()) {
        held_probe_slots_.swap(held);  // keeps the capacity
    }
}

void RendererVk::settle_screen_probes() {
    if (!scene_pass_open_) {
        read_held_probes();
        return;
    }
    const unsigned aux = scene_pass_aux_;
    end_scene_pass();
    begin_scene_pass_with(aux);
}

// Each compared point (seen: the scene's depth there at or behind user_z) as a one-pixel draw of the depth clear's shader at
// the point's reversed depth, 1 - user_z, depth-tested and writing nothing, in an occlusion query. A point the test cannot
// place is answered as the depth read would: one in front of the near plane is seen, one behind the far plane or not on
// the target hidden. The queries are reset in the frame's upload commands, which run before its draws.
bool RendererVk::test_screen_probes(ProbeSlot& out, std::span<const ScreenProbe> probes) {
    const VkPipeline pipeline = probe_test_pipeline();
    if (pipeline == VK_NULL_HANDLE) {
        return false;
    }
    const auto count = static_cast<std::uint32_t>(probes.size());
    const VkDevice device = vk::Context::get().device();
    if (out.query_capacity < count) {
        if (out.queries != VK_NULL_HANDLE) {
            vkDestroyQueryPool(device, out.queries, nullptr);  // idle: its frame has passed
            out.queries = VK_NULL_HANDLE;
            out.query_capacity = 0u;
        }
        VkQueryPoolCreateInfo info{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
        info.queryType = VK_QUERY_TYPE_OCCLUSION;
        info.queryCount = std::max(count, 32u);
        if (vkCreateQueryPool(device, &info, nullptr, &out.queries) != VK_SUCCESS) {
            out.queries = VK_NULL_HANDLE;
            return false;
        }
        out.query_capacity = info.queryCount;
    }
    vkCmdResetQueryPool(frames_[frame_slot_].uploads, out.queries, 0u, count);
    out.answers.assign(count, 0.0f);
    out.drawn.assign(count, 0u);
    const VkCommandBuffer commands = commands_;
    const float scene_w = static_cast<float>(scene_.color->width);
    const float scene_h = static_cast<float>(scene_.color->height);
    const VkViewport viewport{0.0f, scene_h, scene_w, -scene_h, 0.0f, 1.0f};
    vkCmdSetViewport(commands, 0, 1, &viewport);
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    for (std::uint32_t i = 0; i < count; ++i) {
        const ScreenProbePixel pixel = screen_probe_pixel(probes[i], scene_map_);
        const float user_z = probes[i].user_z;
        if (pixel.x == ~0u) {
            continue;  // 0, as the dispatch reads it
        }
        if (user_z <= 0.0f) {
            out.answers[i] = 1.0f;  // any depth is at or behind it
            continue;
        }
        if (!(user_z <= 1.0f)) {
            continue;  // behind every depth, or not a number: hidden
        }
        const VkRect2D rect{{static_cast<std::int32_t>(pixel.x), static_cast<std::int32_t>(pixel.y)}, {1u, 1u}};
        const float value[4] = {1.0f - user_z, 0.0f, 0.0f, 0.0f};
        vkCmdSetScissor(commands, 0, 1, &rect);
        vkCmdPushConstants(commands, utility_layout_, kPushStages, 0, sizeof(value), value);
        vkCmdBeginQuery(commands, out.queries, i, 0u);
        vkCmdDraw(commands, 3, 1, 0, 0);
        vkCmdEndQuery(commands, out.queries, i);
        out.drawn[i] = 1u;
    }
    out.tested = count;
    reset_draw_bindings();
    bind_scene();
    return true;
}

// The probe dispatch, with the scene target pass closed: the depth sampled at each point into the slot's values.
void RendererVk::dispatch_screen_probes(std::size_t slot, std::uint32_t count) {
    ProbeSlot& out = probe_slots_[slot];
    vk::Context& context = vk::Context::get();
    const VkCommandBuffer commands = commands_;
    resolve_scene_depth(commands);
    const VkImageAspectFlags aspects = depth_aspects(depth_format_);
    image_barrier(commands, scene_.depth->image, aspects, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                  kDepthSampleLayout,
                  VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                  VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                  VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

    const VkDescriptorSet set = allocate_frame_set(probe_set_layout_);
    const VkDescriptorImageInfo depth{VK_NULL_HANDLE, scene_.depth->view, kDepthSampleLayout};
    const VkDescriptorBufferInfo pixels{out.pixels->buffer, 0, count * sizeof(ScreenProbePixel)};
    const VkDescriptorBufferInfo values{out.values->buffer, 0, count * sizeof(float)};
    VkWriteDescriptorSet writes[3]{};
    for (std::uint32_t i = 0; i < 3u; ++i) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = set;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = i == 0u ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    }
    writes[0].pImageInfo = &depth;
    writes[1].pBufferInfo = &pixels;
    writes[2].pBufferInfo = &values;
    vkUpdateDescriptorSets(context.device(), 3, writes, 0, nullptr);

    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, probe_pipeline_);
    vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_COMPUTE, probe_layout_, 0, 1, &set, 0, nullptr);
    vkCmdPushConstants(commands, probe_layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(count), &count);
    vkCmdDispatch(commands, (count + kScreenProbeGroup - 1u) / kScreenProbeGroup, 1, 1);

    // The depth back to the scene pass, and the probe values to the host, in one call.
    vk::BarriersVk after(commands);
    after.image_scoped(scene_.depth->image, aspects, kDepthSampleLayout, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                       VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_NONE,
                       VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                       VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
    after.buffer(out.values->buffer, 0u, VK_WHOLE_SIZE, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                 VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT);
    after.issue();
    reset_draw_bindings();
    bind_scene();
}

const float* RendererVk::screen_probes_finished(std::size_t slot) {
    ProbeSlot& out = probe_slots_[slot];
    if (out.fence == 0u || out.fence == kProbesInFrame || completed_fence_value() < out.fence) {
        return nullptr;
    }
    if (out.tested != 0u) {
        // A query reset and never begun stays unavailable: each drawn point is read alone.
        const VkDevice device = vk::Context::get().device();
        for (std::uint32_t i = 0; i < out.tested; ++i) {
            std::uint64_t samples = 0u;
            if (out.drawn[i] != 0u &&
                vkGetQueryPoolResults(device, out.queries, i, 1u, sizeof(samples), &samples, sizeof(samples),
                                      VK_QUERY_RESULT_64_BIT) == VK_SUCCESS) {
                out.answers[i] = samples != 0u ? 1.0f : 0.0f;
            }
        }
        out.fence = 0u;
        return out.answers.data();
    }
    vk::Context::get().invalidate(*out.values);
    out.fence = 0u;
    return reinterpret_cast<const float*>(out.values->mapped);
}

void RendererVk::set_debug_capture_paths(std::string scene_ppm_path, std::string backbuffer_ppm_path) {
    scene_capture_path_ = std::move(scene_ppm_path);
    backbuffer_capture_path_ = std::move(backbuffer_ppm_path);
}

bool RendererVk::debug_copy_scene_to_readback() {
    debug_log_readback();
    if (scene_capture_path_.empty() || !scene_capture_pending_path_.empty()) {
        return false;
    }
    vk::Context& context = vk::Context::get();
    const unsigned w = scene_map_.width;
    const unsigned h = scene_map_.height;
    // The 10-bit scene target, or the wide one (RGBA16F, a traced scene's since l69): 8 bytes a pixel, its halves
    // read as the 10-bit target's words (debug_log_readback; FB b83). Any other format is not read.
    const bool wide = scene_.color->format == VK_FORMAT_R16G16B16A16_SFLOAT;
    if (!wide && scene_.color->format != vk::kSceneColorFormat) {
        return false;
    }
    const std::uint64_t bytes = static_cast<std::uint64_t>(w) * h * (wide ? 8u : 4u);
    if (!scene_capture_.buffer || scene_capture_.buffer->size < bytes) {
        scene_capture_.buffer = context.create_readback_buffer(bytes, "scene target capture");
        if (!scene_capture_.buffer) {
            return false;
        }
    }
    end_scene_pass();
    const VkCommandBuffer commands = commands_;
    image_barrier(commands, scene_.color->image, VK_IMAGE_ASPECT_COLOR_BIT, gpu::Use::ColorWrite, gpu::Use::CopySource);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {w, h, 1};
    vkCmdCopyImageToBuffer(commands, scene_.color->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           scene_capture_.buffer->buffer, 1, &region);
    image_barrier(commands, scene_.color->image, VK_IMAGE_ASPECT_COLOR_BIT, gpu::Use::CopySource, gpu::Use::ColorWrite);
    scene_capture_.fence = 0u;
    scene_capture_pending_path_ = scene_capture_path_;
    scene_capture_width_ = w;
    scene_capture_height_ = h;
    scene_capture_wide_ = wide;
    reset_draw_bindings();
    bind_scene();
    return true;
}

void RendererVk::debug_log_readback() {
    if (scene_capture_pending_path_.empty() || scene_capture_.fence == 0u ||
        completed_fence_value() < scene_capture_.fence) {
        return;
    }
    vk::Context::get().invalidate(*scene_capture_.buffer);
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(scene_capture_width_) * scene_capture_height_ * 4u);
    const auto* copied = reinterpret_cast<const std::uint8_t*>(scene_capture_.buffer->mapped);
    std::vector<std::uint8_t> words;
    if (scene_capture_wide_) {
        words.resize(rgba.size());
        half4_to_ten_bit(copied, words.data(), scene_capture_width_, scene_capture_height_, scene_capture_width_ * 8u,
                         scene_capture_width_ * 4u);
        copied = words.data();
    }
    ten_bit_to_rgba8(copied, rgba.data(), scene_capture_width_, scene_capture_height_, scene_capture_width_ * 4u,
                     scene_capture_width_ * 4u);
    std::vector<std::uint8_t> rgb(static_cast<std::size_t>(scene_capture_width_) * scene_capture_height_ * 3u);
    for (std::size_t i = 0, n = rgb.size() / 3u; i < n; ++i) {
        rgb[i * 3u] = rgba[i * 4u];
        rgb[i * 3u + 1u] = rgba[i * 4u + 1u];
        rgb[i * 3u + 2u] = rgba[i * 4u + 2u];
    }
    write_ppm_async(std::move(scene_capture_pending_path_), std::move(rgb), scene_capture_width_, scene_capture_height_);
    scene_capture_pending_path_.clear();
    scene_capture_.fence = 0u;
}

bool RendererVk::debug_copy_selected_display_to_readback(const DisplayTexture& source) {
    if (backbuffer_capture_path_.empty() || !display_capture_pending_path_.empty() || !source.texture || source.width == 0u ||
        source.height == 0u) {
        return false;
    }
    vk::Texture& texture = *source.texture;
    const bool ten_bit = texture.format == VK_FORMAT_A2B10G10R10_UNORM_PACK32;
    // The picture's rows (a handed-over scene target is taller); the formats the display pictures have.
    if ((!ten_bit && texture.format != VK_FORMAT_R8G8B8A8_UNORM) || texture.width < source.width || texture.height < source.height) {
        return false;
    }
    vk::Context& context = vk::Context::get();
    const std::uint64_t bytes = static_cast<std::uint64_t>(source.width) * source.height * 4u;
    if (!display_capture_.buffer || display_capture_.buffer->size < bytes) {
        display_capture_.buffer = context.create_readback_buffer(bytes, "display picture capture");
        if (!display_capture_.buffer) {
            return false;
        }
    }
    image_barrier(commands_, texture.image, VK_IMAGE_ASPECT_COLOR_BIT, gpu::Use::PixelRead, gpu::Use::CopySource);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {source.width, source.height, 1};
    vkCmdCopyImageToBuffer(commands_, texture.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, display_capture_.buffer->buffer, 1,
                           &region);
    image_barrier(commands_, texture.image, VK_IMAGE_ASPECT_COLOR_BIT, gpu::Use::CopySource, gpu::Use::PixelRead);
    display_capture_.fence = 0u;
    display_capture_pending_path_ = backbuffer_capture_path_ + ".display.ppm";
    display_capture_width_ = source.width;
    display_capture_height_ = source.height;
    display_capture_ten_bit_ = ten_bit;
    return true;
}

void RendererVk::debug_log_display_readback() {
    if (display_capture_pending_path_.empty() || display_capture_.fence == 0u || completed_fence_value() < display_capture_.fence) {
        return;
    }
    vk::Context::get().invalidate(*display_capture_.buffer);
    const auto* pixels = reinterpret_cast<const std::uint8_t*>(display_capture_.buffer->mapped);
    const std::size_t count = static_cast<std::size_t>(display_capture_width_) * display_capture_height_;
    std::vector<std::uint8_t> rgba(count * 4u);
    if (display_capture_ten_bit_) {
        ten_bit_to_rgba8(pixels, rgba.data(), display_capture_width_, display_capture_height_, display_capture_width_ * 4u,
                         display_capture_width_ * 4u);
    } else {
        std::memcpy(rgba.data(), pixels, rgba.size());
    }
    std::vector<std::uint8_t> rgb(count * 3u);
    for (std::size_t i = 0; i < count; ++i) {
        rgb[i * 3u] = rgba[i * 4u];
        rgb[i * 3u + 1u] = rgba[i * 4u + 1u];
        rgb[i * 3u + 2u] = rgba[i * 4u + 2u];
    }
    write_ppm_async(std::move(display_capture_pending_path_), std::move(rgb), display_capture_width_, display_capture_height_);
    display_capture_pending_path_.clear();
    display_capture_.fence = 0u;
}

void RendererVk::debug_log_backbuffer_readback() {
    debug_log_display_readback();
    if (backbuffer_capture_pending_path_.empty() || backbuffer_capture_.fence == 0u ||
        completed_fence_value() < backbuffer_capture_.fence) {
        return;
    }
    vk::Context::get().invalidate(*backbuffer_capture_.buffer);
    const auto* pixels = reinterpret_cast<const std::uint8_t*>(backbuffer_capture_.buffer->mapped);
    const unsigned red = backbuffer_capture_bgra_ ? 2u : 0u;
    const unsigned blue = backbuffer_capture_bgra_ ? 0u : 2u;
    std::vector<std::uint8_t> rgb(static_cast<std::size_t>(backbuffer_capture_width_) * backbuffer_capture_height_ *
                                  3u);
    for (std::size_t i = 0, n = rgb.size() / 3u; i < n; ++i) {
        rgb[i * 3u] = pixels[i * 4u + red];
        rgb[i * 3u + 1u] = pixels[i * 4u + 1u];
        rgb[i * 3u + 2u] = pixels[i * 4u + blue];
    }
    write_ppm_async(std::move(backbuffer_capture_pending_path_), std::move(rgb), backbuffer_capture_width_,
                    backbuffer_capture_height_);
    backbuffer_capture_pending_path_.clear();
    backbuffer_capture_.fence = 0u;
}


bool RendererVk::interpolation_mode() const {
    const RenderConfig cfg = get_render_config();
    const double rate = present_frame_rate(presented_max_fps(cfg), cfg.vsync_interval, display_refresh_hz_);
    if (temporal_mode() && temporal_interp_suspended_) {
        return false;
    }
    return interpolated_frames_per_game_frame(rate) != 0u;
}

void RendererVk::suspend_temporal_interpolation(double ms_per_frame) {
    if (temporal_interp_suspended_) {
        return;
    }
    temporal_interp_suspended_ = true;
    temporal_interp_key_ = temporal_interp_key();
    std::cerr << "[interp] temporal upscaling of generated frames off: " << ms_per_frame
              << " ms GPU per presented frame\n";
}

RendererVk::TemporalInterpKey RendererVk::temporal_interp_key() const {
    const RenderConfig cfg = get_render_config();
    TemporalInterpKey key{};
    key.upscaler = cfg.upscaler;
    key.quality = cfg.temporal_quality;
    key.scene_map = output_scene_active_ ? alt_scene_map_ : scene_map_;
    key.output_map = upscale_output_;
    key.max_fps = presented_max_fps(cfg);
    key.vsync = cfg.vsync_interval;
    key.refresh_hz = display_refresh_hz_;
    return key;
}

}  // namespace galaxy::render
