// SPDX-License-Identifier: GPL-3.0-only
// The Vulkan renderer's scene target (renderer_vk.h): its targets, draws, clears, copies, screen probes and test captures.

#include "renderer_vk_internal.h"
#include "galaxy/render/vk/renderer_vk.h"

#include "galaxy/content_viewport.h"
#include "galaxy/experimental_ultrawide_aspect.h"
#include "galaxy/render/host_window.h"
#include "galaxy/render/render_config.h"
#include "galaxy/render/curved_surfaces.h"
#include "galaxy/motion_signals.h"
#include "galaxy/platform_time.h"
#include "galaxy/render/draw_matrices.h"
#include "galaxy/render/texture_store.h"
#include "galaxy/render/draw_constants.h"

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

}  // namespace

// An scene target set at `scale`: targets in their attachment layouts, the framebuffer, and the conversion inputs (allocated
// once per set; rewriting them needs a GPU that no longer reads the old views, which callers ensure).
bool RendererVk::create_scene_set(SceneSet& set, const FrameMap& map, unsigned samples, unsigned set_width,
                                unsigned set_height) {
    vk::Context& context = vk::Context::get();
    const VkDevice device = context.device();
    const std::uint32_t width = static_cast<std::uint32_t>(std::lround(static_cast<double>(set_width) * map.sx));
    const std::uint32_t height = static_cast<std::uint32_t>(std::lround(static_cast<double>(set_height) * map.sy));
    gpu::TextureRef color = context.create_texture(
        width, height, 1u, VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
            VK_IMAGE_USAGE_TRANSFER_DST_BIT,
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
        ms_color = context.create_texture(width, height, 1u, VK_FORMAT_R8G8B8A8_UNORM,
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
        framebuffer = make_framebuffer(draw_layout_.scene_pass, single);
    }
    // The new targets enter their attachment layouts now, so every later submission finds them so.
    run_one_time([&](VkCommandBuffer commands) {
        for (const vk::Texture* target : {color.get(), ms_color.get()}) {
            if (target == nullptr) continue;
            image_barrier(commands, target->image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
                          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                          VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
        }
        for (const vk::Texture* target : {depth.get(), ms_depth.get()}) {
            if (target == nullptr) continue;
            image_barrier(commands, target->image, depth_aspects(depth_format_), VK_IMAGE_LAYOUT_UNDEFINED,
                          VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
                          VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                          VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
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
    for (VkDescriptorSet* source : {&set.color_source, &set.depth_source}) {
        if (*source == VK_NULL_HANDLE) {
            *source = allocate_persistent_set(conversion_set_layout_);
        }
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
    const VkDescriptorImageInfo color_info{VK_NULL_HANDLE, set.color->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    const VkDescriptorImageInfo depth_info{VK_NULL_HANDLE, set.depth->view, kDepthSampleLayout};
    // The conversion's filtering sampler: nearest for a depth format the device cannot filter linearly.
    const VkDescriptorImageInfo color_sampler{linear_clamp_, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED};
    const VkDescriptorImageInfo depth_sampler{depth_linear_ ? linear_clamp_ : point_clamp_, VK_NULL_HANDLE,
                                              VK_IMAGE_LAYOUT_UNDEFINED};
    VkWriteDescriptorSet writes[4]{};
    for (unsigned i = 0; i < 4u; ++i) {
        const bool is_color = (i & 1u) == 0u;
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = is_color ? set.color_source : set.depth_source;
        writes[i].dstBinding = i < 2u ? 0u : 1u;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = i < 2u ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE : VK_DESCRIPTOR_TYPE_SAMPLER;
        writes[i].pImageInfo = i < 2u ? (is_color ? &color_info : &depth_info)
                                      : (is_color ? &color_sampler : &depth_sampler);
    }
    vkUpdateDescriptorSets(device, 4, writes, 0, nullptr);
    return true;
}

// Releases a set's targets and framebuffer once the GPU is past the commands recorded so far (its conversion inputs
// stay allocated for the next create_scene_set).
void RendererVk::destroy_scene_set(SceneSet& set) {
    const VkDevice device = vk::Context::get().device();
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
    // The previous targets are read once more (carry_over_scene_contents), then released after the GPU is done.
    SceneSet made;
    made.color_source = scene_.color_source;
    made.depth_source = scene_.depth_source;
    made.ms_depth_source = scene_.ms_depth_source;
    if (!create_scene_set(made, map, samples)) {
        return false;
    }
    if (scene_.framebuffer != VK_NULL_HANDLE) {
        // With MSAA on either side the new targets start cleared instead (carry_over_scene_contents).
        const bool carry = scene_.samples == 1u && made.samples == 1u;
        gpu::TextureRef old_color = carry ? scene_.color : nullptr;
        gpu::TextureRef old_depth = carry ? scene_.depth : nullptr;
        scene_carry_clear_ = !carry;
        destroy_scene_set(scene_);
        scene_carry_color_ = std::move(old_color);
        scene_carry_depth_ = std::move(old_depth);
    }
    scene_ = std::move(made);
    scene_map_ = map;
    if (scene_.samples > 1u) {
        std::cout << "[Renderer] scene target MSAA " << scene_.samples << "x\n";
    }
    return true;
}

unsigned RendererVk::wanted_msaa_samples() const {
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
                  VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
    const std::uint32_t width = scene_map_.width;
    const std::uint32_t height = scene_map_.height;
    VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    begin.renderPass = draw_layout_.scene_pass;
    begin.framebuffer = set.resolve_framebuffer;
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
                  VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                  VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
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

// The scale-dependent memory a new internal resolution adds must fit 90% of the device's budget (RendererD3D12's
// rule): scene target colour and depth (8 bytes per pixel) and, at most, the display picture pair plus one full-picture copy (12 bytes per
// pixel of the picture).
bool RendererVk::scene_fits_memory(const FrameMap& map) const {
    std::uint64_t usage = 0u;
    std::uint64_t budget = 0u;
    if (!vk::Context::get().device_memory_budget(usage, budget)) {
        return true;
    }
    const auto bytes = [](const FrameMap& m) { return (8.0 + 12.0) * m.width * m.height; };
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
    const FrameMap wanted(static_cast<unsigned>(content.width), static_cast<unsigned>(content.height));
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
    drain_gpu();
    frame_pass_targets_.clear();
    for (Frame& frame : frames_) {
        frame.retired.clear();
    }
    const FrameMap previous = scene_map_;
    if (!create_scene_targets(wanted, samples)) {
        return;
    }
    reset_draw_bindings();
    std::cout << "[Renderer] internal resolution " << previous.width << "x" << previous.height << " -> " << wanted.width
              << "x" << wanted.height << '\n';
}

// After an internal-resolution change the new scene target starts with the previous targets' contents, resampled: the game
// expects the scene target it left (normally the display copy's clear, whose alpha its shadow and glow passes read).
void RendererVk::carry_over_scene_contents() {
    const VkCommandBuffer commands = commands_;
    if (scene_carry_clear_) {
        scene_carry_clear_ = false;
        const SceneClearValues clear = make_scene_clear_values(0u, 0u);
        end_scene_pass();
        begin_scene_pass(&clear);
        end_scene_pass();
        reset_draw_bindings();
    }
    if (!scene_carry_color_ || !scene_carry_depth_) {
        scene_carry_color_.reset();
        scene_carry_depth_.reset();
        return;
    }
    vk::Texture& old_color = *scene_carry_color_;
    vk::Texture& old_depth = *scene_carry_depth_;
    image_barrier(commands, old_color.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                  VK_ACCESS_SHADER_READ_BIT);
    image_barrier(commands, old_depth.image, depth_aspects(depth_format_),
                  VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, kDepthSampleLayout,
                  VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
    const VkDescriptorSet color_set = allocate_frame_set(utility_set_layout_);
    const VkDescriptorSet depth_set = allocate_frame_set(utility_set_layout_);
    const VkDescriptorImageInfo color_info{VK_NULL_HANDLE, old_color.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    const VkDescriptorImageInfo depth_info{VK_NULL_HANDLE, old_depth.view, kDepthSampleLayout};
    VkWriteDescriptorSet writes[2]{};
    for (unsigned i = 0; i < 2u; ++i) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = i == 0u ? color_set : depth_set;
        writes[i].dstBinding = 0;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        writes[i].pImageInfo = i == 0u ? &color_info : &depth_info;
    }
    vkUpdateDescriptorSets(vk::Context::get().device(), 2, writes, 0, nullptr);
    begin_scene_pass();
    const float w = static_cast<float>(scene_map_.width);
    const float h = static_cast<float>(scene_map_.height);
    const VkViewport viewport{0.0f, h, w, -h, 0.0f, 1.0f};
    const VkRect2D scissor{{0, 0}, {scene_map_.width, scene_map_.height}};
    vkCmdSetViewport(commands, 0, 1, &viewport);
    vkCmdSetScissor(commands, 0, 1, &scissor);
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, color_carry_pipeline_);
    vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, utility_layout_, 0, 1, &color_set, 0, nullptr);
    vkCmdDraw(commands, 3, 1, 0, 0);
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, depth_carry_pipeline_);
    vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, utility_layout_, 0, 1, &depth_set, 0, nullptr);
    vkCmdDraw(commands, 3, 1, 0, 0);
    end_scene_pass();
    // The old targets are read by this frame: released when its slot comes round.
    retire(std::move(scene_carry_color_));
    retire(std::move(scene_carry_depth_));
    reset_draw_bindings();
}

void RendererVk::begin_scene_pass(const SceneClearValues* clear) {
    if (scene_pass_open_) {
        return;
    }
    if (scene_owed_ && !replaying_) [[unlikely]] {
        if (clear != nullptr) {
            // The pass begins with the whole scene target cleared: nothing of the owed picture remains.
            scene_owed_.reset();
            ++gpu_window_.owed_free;
        } else {
            restore_owed_scene(nullptr);
        }
    }
    const VkCommandBuffer commands = commands_;
    VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    begin.renderPass = draw_layout_.pass_for(scene_variant(), clear != nullptr);
    begin.framebuffer = scene_framebuffer();
    scene_.depth_dirty = scene_.samples > 1u;
    // The set's own size: the scene target, or a target pass's smaller target (E-07).
    begin.renderArea = {{0, 0}, {scene_.color->width, scene_.color->height}};
    VkClearValue values[2]{};
    if (clear != nullptr) {
        std::memcpy(values[0].color.float32, clear->color.data(), sizeof(float) * 4u);
        values[1].depthStencil = {clear->reversed_depth, 0u};  // reversed-Z, as clear_scene's
        begin.clearValueCount = 2;
        begin.pClearValues = values;
    }
    vkCmdBeginRenderPass(commands, &begin, VK_SUBPASS_CONTENTS_INLINE);
    scene_pass_open_ = true;
    ++gpu_window_.passes;
    // Dynamic state and bindings outlive the pass, but the renderer's own passes in between changed them.
    if (bound_.viewport_valid) {
        vkCmdSetViewport(commands, 0, 1, &bound_.viewport);
    }
    if (bound_.scissor_valid) {
        vkCmdSetScissor(commands, 0, 1, &bound_.scissor);
    }
}

void RendererVk::end_scene_pass() {
    if (!scene_pass_open_) {
        return;
    }
    vkCmdEndRenderPass(commands_);
    scene_pass_open_ = false;
}

void RendererVk::reset_draw_bindings() noexcept {
    bound_ = Bound{};
}

void RendererVk::bind_scene() {
    if (logging()) {
        recording_log_.ops.push_back({ReplayKind::BindScene, 0u});
    }
    const float x = 0.0f, y = 0.0f;
    const float w = static_cast<float>(scene_.color->width);
    const float h = static_cast<float>(scene_.color->height);
    bound_.viewport = VkViewport{x, y + h, w, -h, 0.0f, 1.0f};
    bound_.viewport_valid = true;
    bound_.scissor = VkRect2D{{0, 0}, {scene_.color->width, scene_.color->height}};
    bound_.scissor_valid = true;
    bound_.scissor_input_valid = false;
    if (scene_pass_open_) {
        const VkCommandBuffer commands = commands_;
        vkCmdSetViewport(commands, 0, 1, &bound_.viewport);
        vkCmdSetScissor(commands, 0, 1, &bound_.scissor);
    }
}

void RendererVk::clear_scene(std::uint32_t clear_rgba,
                           bool color_enable, bool alpha_enable, bool depth_enable, unsigned rect_x, unsigned rect_y,
                           unsigned rect_w, unsigned rect_h, std::uint8_t pixel_format) {
    const ReplayClear logged{clear_rgba, color_enable, alpha_enable, depth_enable,
                             rect_x, rect_y, rect_w, rect_h, pixel_format};
    if (logging()) {
        recording_log_.ops.push_back({ReplayKind::Clear, static_cast<std::uint32_t>(recording_log_.clears.size())});
        recording_log_.clears.push_back(logged);
    } else if (ring_mode_ == RingMode::Interp && ring_advance_pending_ && !replaying_) {
        // The display copy's copy-clear, after the log closed: the next log starts with it, so its sub-frames begin
        // from the scene target the frame began from.
        start_clear_ = logged;
        start_clear_valid_ = true;
    }
    const LogPause pause{*this};
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
    const SceneClearValues clear = make_scene_clear_values(clear_rgba, pixel_format);
    // The whole scene target, every channel, while the pass is closed (a copy-clear): the pass begins cleared.
    const bool has_alpha = pixel_format == 1u;
    if (scene_owed_ && !replaying_) [[unlikely]] {
        // A handed-over picture: what the clear leaves of it (all of it unless every colour channel is cleared).
        const bool rgba = color_enable && (alpha_enable || !has_alpha);
        restore_owed_scene(rgba ? &rect : nullptr);
    }
    if (!scene_pass_open_ && color_enable && (alpha_enable || !has_alpha) && depth_enable && rect.offset.x == 0 &&
        rect.offset.y == 0 && rect.extent.width == static_cast<std::uint32_t>(scene_w) &&
        rect.extent.height == static_cast<std::uint32_t>(scene_h)) {
        begin_scene_pass(&clear);
        return;
    }
    begin_scene_pass();
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
    const VkPipeline depth_clear = depth_enable ? scene_utility_pipeline(SceneUtility::ClearDepth) : VK_NULL_HANDLE;
    if (depth_clear != VK_NULL_HANDLE) {
        const VkViewport viewport{0.0f, static_cast<float>(scene_h), static_cast<float>(scene_w),
                                  -static_cast<float>(scene_h), 0.0f, 1.0f};
        const float value[4] = {clear.reversed_depth, 0.0f, 0.0f, 0.0f};
        vkCmdSetViewport(commands, 0, 1, &viewport);
        vkCmdSetScissor(commands, 0, 1, &rect);
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, depth_clear);
        vkCmdPushConstants(commands, utility_layout_, kPushStages, 0, sizeof(value), value);
        vkCmdDraw(commands, 3, 1, 0, 0);
        reset_draw_bindings();
        bind_scene();
    } else if (depth_enable) {
        // Reversed-Z: the game's farthest (0xFFFFFF) clears to about 0.
        VkClearAttachment attachment{};
        attachment.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        attachment.clearValue.depthStencil = {clear.reversed_depth, 0u};
        vkCmdClearAttachments(commands, 1, &attachment, 1, &clear_rect);
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
        // A frame with more distinct tables than the pool holds: another pool, freed with the slot.
        const VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 4096u * 8u},
                                              {VK_DESCRIPTOR_TYPE_SAMPLER, 256u},
                                              {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, vk_detail::kPassStorageImages}};
        VkDescriptorPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool_info.maxSets = 4096u;
        pool_info.poolSizeCount = static_cast<std::uint32_t>(std::size(sizes));
        pool_info.pPoolSizes = sizes;
        VkDescriptorPool pool = VK_NULL_HANDLE;
        vk::check(vkCreateDescriptorPool(device, &pool_info, nullptr, &pool), "overflow descriptor pool");
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
    settle_owed_scene();
    std::swap(scene_, alt_scene_);
    std::swap(scene_map_, alt_scene_map_);
    output_scene_active_ = !output_scene_active_;
    reset_draw_bindings();
}

// The scene target and the target passes' set trade places (begin_target_pass): targets and scale, the output scene target's state kept.
void RendererVk::swap_target_set() {
    end_scene_pass();
    settle_owed_scene();
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
    if (slot.set.color_source == VK_NULL_HANDLE && !target_free_sources_.empty()) {
        slot.set.color_source = target_free_sources_.back().first;
        slot.set.depth_source = target_free_sources_.back().second;
        target_free_sources_.pop_back();
    }
    const bool kept = slot.set.color && slot.map == scene_map_ && slot.width == width && slot.height == height;
    if (!kept) {
        if (slot.set.color || slot.set.depth) {
            destroy_scene_set(slot.set);
        }
        // A new set enters its attachment layouts (create_scene_set); the pass's first clear (RenderCore) fills it.
        if (!create_scene_set(slot.set, scene_map_, 1u, width, height)) {
            if (slot.set.color_source != VK_NULL_HANDLE) {
                target_free_sources_.push_back({slot.set.color_source, slot.set.depth_source});
            }
            target_slots_.erase(key);
            return false;
        }
        slot.set.color->layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        slot.map = scene_map_;
        slot.width = width;
        slot.height = height;
    }
    slot.used = frame_index_;
    target_key_ = key;
    std::swap(target_scene_, slot.set);
    target_scene_map_ = slot.map;
    // The pass is the game frame's alone: sub-frames sample its picture, never draw it again.
    target_log_paused_ = log_paused_;
    log_paused_ = true;
    swap_target_set();
    if (kept) {
        // Sampled since its last pass: drawn into again, after those reads.
        image_barrier(commands_, scene_.color->image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                      VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
        scene_.color->layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }
    bind_scene();
    return true;
}

void RendererVk::end_target_pass(std::uint8_t channels, TextureCache& textures) {
    if (!target_pass_active_) {
        return;
    }
    swap_target_set();
    if (frame_pass_color_key_ != 0u || frame_pass_depth_key_ != 0u) {
        close_frame_for_pass(textures);
    }
    log_paused_ = target_log_paused_;
    // The picture is sampled until the key's next pass.
    vk::Texture& picture = *target_scene_.color;
    image_barrier(commands_, picture.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                  VK_ACCESS_SHADER_READ_BIT);
    picture.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    TargetSlot& slot = target_slots_[target_key_];
    std::swap(slot.set, target_scene_);
    slot.map = target_scene_map_;
    target_scene_map_ = FrameMap{};
    (void)publish_picture(target_key_, slot.width, slot.height, slot.set.color, channels, 0u, textures);
    // Keys unused for kTargetIdleFrames frames give back their render resources (their pictures stay).
    for (auto it = target_slots_.begin(); it != target_slots_.end();) {
        TargetSlot& idle = it->second;
        if (idle.used + kTargetIdleFrames >= frame_index_) {
            ++it;
            continue;
        }
        destroy_scene_set(idle.set);
        target_free_sources_.push_back({idle.set.color_source, idle.set.depth_source});
        it = target_slots_.erase(it);
    }
    bind_scene();
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
    out.width = static_cast<std::uint16_t>(it->second.viewed->width);
    out.height = static_cast<std::uint16_t>(it->second.viewed->height);
    out.mip_levels = 1u;
    out.from_capture = true;
    out.source_bytes = it->second.bytes != 0u ? it->second.bytes : static_cast<std::uint32_t>(it->second.width) * it->second.height * 4u;
    return true;
}

bool RendererVk::publish_picture(std::uint64_t key, std::uint32_t width, std::uint32_t height, gpu::TextureRef texture,
                                 std::uint8_t channels, std::uint32_t bytes, TextureCache& textures) {
    const VkDevice device = vk::Context::get().device();
    bool changed = false;
    if (bytes != 0u) {
        // A capture over memory another capture's picture names replaces it (the game reuses capture buffers).
        for (auto it = pictures_.begin(); it != pictures_.end();) {
            Picture& other = it->second;
            if (it->first != key && other.bytes != 0u && it->first < key + bytes && key < it->first + other.bytes) {
                textures.retire_view(other.view, std::move(other.viewed));
                if (other.sampled != VK_NULL_HANDLE) {
                    const VkImageView old = other.sampled;
                    vk::Context::get().defer([device, old] { vkDestroyImageView(device, old, nullptr); });
                }
                it = pictures_.erase(it);
                changed = true;
            } else {
                ++it;
            }
        }
    }
    Picture& picture = pictures_[key];
    if (picture.view != ~0u && picture.viewed == texture && picture.channels == channels && picture.width == width &&
        picture.height == height) {
        changed = changed || picture.bytes != bytes;
        picture.bytes = bytes;
        return changed;
    }
    if (picture.view != ~0u) {
        textures.retire_view(picture.view, std::move(picture.viewed));
    }
    picture.view = textures.allocate_view();
    picture.viewed = std::move(texture);
    picture.width = width;
    picture.height = height;
    picture.channels = channels;
    picture.bytes = bytes;
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

bool RendererVk::publish_frame_pass(const FramePassRequest& params, gpu::TextureRef texture, TextureCache& textures) {
    const FrameExtent size = frame_pass_extent(scene_map_);
    return publish_picture(params.key, size.width, size.height, std::move(texture), 0u, 0u, textures);
}

bool RendererVk::begin_output_scene() {
    if (output_scene_active_) {
        return false;
    }
    const FrameMap output = output_frame_for_window();
    if (!get_render_config().hud_output_resolution || output.empty() || output.width <= scene_map_.width) {
        return false;
    }
    if (logging()) {
        recording_log_.ops.push_back({ReplayKind::OutputSceneBegin, 0u});
    }
    return enter_output_scene(output);
}

// The whole scene scene target resampled into the output scene target with the display copy's identity settings (no filter, gamma 1.0,
// alpha kept), which then takes the scene target's place with its depth cleared to the far plane.
bool RendererVk::enter_output_scene(const FrameMap& output) {
    const LogPause pause(*this);
    if (!alt_scene_.color || alt_scene_map_ != output) {
        if (alt_scene_.color) {
            destroy_scene_set(alt_scene_);
        }
        if (!create_scene_set(alt_scene_, output)) {
            alt_scene_map_ = FrameMap{};
            return false;
        }
        alt_scene_map_ = output;
    }
    const unsigned width = output.width;
    const unsigned height = output.height;
    const FramePassGeometry sampling = frame_pass_geometry(width, height, scene_map_);
    record_conversion_blit(*alt_scene_.color, width, height, kCopyLayoutDisplay, sampling.src_x, sampling.src_y, sampling.step_x, sampling.step_y);
    end_scene_pass();
    image_barrier(commands_, alt_scene_.color->image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
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
    if (logging()) {
        recording_log_.ops.push_back({ReplayKind::SwapSceneSets, 0u});
    }
    swap_scene_sets();
    bind_scene();
}

bool RendererVk::draw_frame_pass_from_output(const FramePassRequest& params, TextureCache& textures) {
    if (output_scene_active_ || !alt_scene_.color) {
        return draw_frame_pass(params, textures);
    }
    if (logging()) {
        recording_log_.ops.push_back({ReplayKind::SwapSceneSets, 0u});
    }
    swap_scene_sets();
    bind_scene();
    const bool updated = draw_frame_pass(params, textures);
    if (logging()) {
        recording_log_.ops.push_back({ReplayKind::SwapSceneSets, 0u});
    }
    swap_scene_sets();
    bind_scene();
    return updated;
}

VkDescriptorSet RendererVk::allocate_post_set(VkDescriptorSetLayout layout) {
    if (replay_list_ == nullptr) {
        return allocate_frame_set(layout);
    }
    VkDescriptorSetAllocateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    info.descriptorPool = replay_list_->descriptors;
    info.descriptorSetCount = 1;
    info.pSetLayouts = &layout;
    VkDescriptorSet set = VK_NULL_HANDLE;
    vk::check(vkAllocateDescriptorSets(vk::Context::get().device(), &info, &set), "replay post-process set");
    return set;
}

// SMAA or FXAA of the 3D scene at its end, before the 2D layer draws: the scene target pass ends for the passes that read
// a copy of the scene, then the result is drawn into it again.
void RendererVk::record_scene_aa() {
    if (!post_.ready() || !scene_.color || post_.settings().aa == PostProcessSettings::Aa::kOff) {
        return;
    }
    const GpuPassScope pass{*this, GpuPass::SceneAa};
    if (logging()) {
        recording_log_.ops.push_back({ReplayKind::SceneAa, 0u});
    }
    // The picture the display copy takes, against its size on screen, decides AUTO.
    const auto aspect = galaxy::experimental_effective_host_aspect();
    const galaxy::ContentViewport content = galaxy::fit_content_viewport(
        static_cast<int>(host_window::backbuffer_width), static_cast<int>(host_window::backbuffer_height),
        aspect ? aspect->content_aspect : galaxy::kDesignContentAspect);
    end_scene_pass();
    settle_owed_scene();
    const vk::Texture& color = *scene_.color;
    const auto allocate = [this](VkDescriptorSetLayout layout) { return allocate_post_set(layout); };
    const AaRun ran = post_.prepare_scene_aa(
        commands_, color, color.width, color.height, scene_map_.width, scene_map_.height,
        static_cast<unsigned>(std::max(content.width, 1)), static_cast<unsigned>(std::max(content.height, 1)),
        allocate);
    begin_scene_pass();
    post_.finish_scene_aa(commands_, ran, color.width, color.height, allocate,
                          draw_layout_.pass_for(scene_samples_log2(), false), scene_samples_log2());
    if (replay_list_ != nullptr && ran != AaRun::kNone) {
        post_.hold_scene_targets(replay_list_->held);
    }
    reset_draw_bindings();
}

// The occlusion of the opaque scene: the scene target pass ends for its compute passes, which read the depth, and the
// multiply draws in it again.
void RendererVk::record_scene_ao() {
    if (!ao_.ready() || !scene_.color || !scene_.depth) {
        return;
    }
    const GpuPassScope pass{*this, GpuPass::SceneAo};
    if (logging()) {
        recording_log_.ops.push_back({ReplayKind::SceneAo, 0u});
    }
    end_scene_pass();
    settle_owed_scene();
    if (scene_.samples > 1u) {
        resolve_scene_depth(commands_);
    }
    const VkImageAspectFlags aspects = depth_aspects(depth_format_);
    image_barrier(commands_, scene_.depth->image, aspects, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                  kDepthSampleLayout, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                  VK_ACCESS_SHADER_READ_BIT);
    const auto allocate = [this](VkDescriptorSetLayout layout) { return allocate_post_set(layout); };
    const bool made = ao_.prepare(commands_, *scene_.depth, kDepthSampleLayout, scene_ao_view_, allocate);
    image_barrier(commands_, scene_.depth->image, aspects, kDepthSampleLayout,
                  VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                  VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
    begin_scene_pass();
    if (made) {
        ao_.finish(commands_, draw_layout_.pass_for(scene_variant(), false), scene_samples_log2(), allocate);
        if (replay_list_ != nullptr) {
            ao_.hold(replay_list_->held);
        }
    }
    reset_draw_bindings();
}

// The wide colour targets for the live scene target's size and samples, over its depth; its resolve framebuffer too, since
// the depth resolve writes depth alone. False when the device cannot make them: the scene stays in the scene target.
bool RendererVk::ensure_wide_set(SceneSet& set) {
    if (set.color && set.depth_serial == scene_.depth_serial && set.samples == scene_.samples) {
        set.resolve_framebuffer = scene_.resolve_framebuffer;
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
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, true, "wide scene color");
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
                          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
                          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                          VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
        }
    });
    set.color = std::move(color);
    set.ms_color = std::move(ms_color);
    set.depth = scene_.depth;
    set.ms_depth = scene_.ms_depth;
    set.ms_depth_source = scene_.ms_depth_source;
    set.resolve_framebuffer = scene_.resolve_framebuffer;
    set.samples = scene_.samples;
    set.depth_serial = scene_.depth_serial;
    set.wide = true;
    set.wide_read = false;
    set.turned = false;
    if (set.color_source == VK_NULL_HANDLE) {
        set.color_source = allocate_persistent_set(conversion_set_layout_);
    }
    set.depth_source = scene_.depth_source;
    const VkDescriptorImageInfo color_info{VK_NULL_HANDLE, set.color->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    const VkDescriptorImageInfo color_sampler{linear_clamp_, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED};
    VkWriteDescriptorSet writes[2]{};
    for (unsigned i = 0; i < 2u; ++i) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = set.color_source;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = i == 0u ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE : VK_DESCRIPTOR_TYPE_SAMPLER;
        writes[i].pImageInfo = i == 0u ? &color_info : &color_sampler;
    }
    vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
    return true;
}

void RendererVk::begin_wide_scene(bool wanted) {
    if (!wanted || scene_.wide || !scene_camera_.ready() || !scene_.color || output_scene_active_ || target_pass_active_) {
        return;
    }
    end_scene_pass();
    settle_owed_scene();
    if (!ensure_wide_set(wide_scene_)) {
        return;
    }
    if (logging()) {
        recording_log_.ops.push_back({ReplayKind::WideBegin, 0u});
    }
    record_wide_begin();
}

// What the scene target holds (its pass end resolved a multisampled one) goes on under the scene.
void RendererVk::record_wide_begin() {
    const GpuPassScope pass{*this, GpuPass::Camera};
    end_scene_pass();
    SceneSet& set = replaying_ ? replay_wide_scene_ : wide_scene_;
    if (replaying_ && !ensure_wide_set(set)) {
        return;
    }
    if (set.wide_read) {
        for (const vk::Texture* target : {set.color.get(), set.ms_color.get()}) {
            if (target == nullptr) continue;
            image_barrier(commands_, target->image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                          VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
        }
        set.wide_read = false;
    }
    gpu::TextureRef picture = scene_.color;
    image_barrier(commands_, picture->image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
    set.depth_dirty = scene_.depth_dirty;
    std::swap(scene_, set);
    begin_scene_pass();
    const auto allocate = [this](VkDescriptorSetLayout layout) { return allocate_post_set(layout); };
    scene_camera_.widen(commands_, draw_layout_.pass_for(scene_variant(), false), scene_samples_log2(), *picture, allocate);
    // The scene target's colour goes back to its attachment layout: the depth resolve's framebuffer holds it.
    end_scene_pass();
    image_barrier(commands_, picture->image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    reset_draw_bindings();
}

void RendererVk::end_wide_scene() {
    if (!scene_.wide) {
        return;
    }
    end_scene_pass();
    settle_owed_scene();
    if (logging()) {
        recording_log_.ops.push_back({ReplayKind::WideEnd, 0u});
    }
    record_wide_end();
}

void RendererVk::record_wide_end() {
    if (!scene_.wide) {
        return;
    }
    const GpuPassScope pass{*this, GpuPass::Camera};
    end_scene_pass();
    SceneSet& set = replaying_ ? replay_wide_scene_ : wide_scene_;
    const bool per_sample = scene_.samples > 1u && vk::Context::get().sample_rate_shading();
    gpu::TextureRef wide = scene_.color;
    gpu::TextureRef wide_ms = scene_.ms_color;
    for (const vk::Texture* target : {wide.get(), wide_ms.get()}) {
        if (target == nullptr) continue;
        image_barrier(commands_, target->image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                      VK_ACCESS_SHADER_READ_BIT);
    }
    const auto allocate = [this](VkDescriptorSetLayout layout) { return allocate_post_set(layout); };
    const auto now = std::chrono::steady_clock::now();
    const float seconds = std::chrono::duration<float>(now - camera_metered_).count();
    camera_metered_ = now;
    // After a load or a long pause the eye starts open.
    scene_camera_.meter(commands_, *wide, seconds > 1.0f ? -1.0f : seconds, allocate);
    const bool depth_dirty = scene_.depth_dirty;
    std::swap(scene_, set);
    set.wide_read = true;
    scene_.depth_dirty = depth_dirty;
    begin_scene_pass();
    scene_camera_.draw(commands_, draw_layout_.pass_for(scene_variant(), false), scene_samples_log2(),
                       per_sample ? *wide_ms : *wide, per_sample, allocate);
    if (replay_list_ != nullptr) {
        scene_camera_.hold(replay_list_->held);
        replay_list_->held.push_back(wide);
        if (wide_ms) {
            replay_list_->held.push_back(wide_ms);
        }
    }
    reset_draw_bindings();
}

// The game's bloom (game_bloom_vk.h) at this point of the stream: the scene target pass ends for the passes that copy or read
// the scene, and the composite is drawn in it again.
void RendererVk::game_bloom(std::uint16_t phase, std::uint32_t a, std::uint32_t b) {
    if (!bloom_.ready()) {
        return;
    }
    const ReplayBloom bloom{phase, a, b};
    if (logging()) {
        recording_log_.ops.push_back({ReplayKind::Bloom, static_cast<std::uint32_t>(recording_log_.blooms.size())});
        recording_log_.blooms.push_back(bloom);
    }
    record_game_bloom(bloom);
}

void RendererVk::record_game_bloom(const ReplayBloom& bloom) {
    if (!scene_.color) {
        return;
    }
    const GpuPassScope pass{*this, GpuPass::Bloom};
    end_scene_pass();
    settle_owed_scene();
    vk::GameBloomVk::Inputs in{};
    in.color = scene_.color.get();
    in.scale = static_cast<float>(scene_map_.height / kAuthoredLines);
    in.frame_width = std::min(scene_map_.width, scene_.color->width);
    in.frame_height = std::min(scene_map_.height, scene_.color->height);
    const auto allocate = [this](VkDescriptorSetLayout layout) { return allocate_post_set(layout); };
    end_scene_pass();
    if (bloom.phase == motion::kBloomHold) {
        if (bloom_.ready() && scene_.samples <= 1u && !scene_.wide && !logging() && !replaying_ && ring_mode_ != RingMode::Interp) {
            // The scene and the bloom's image trade places instead of the scene being copied. The game draws the
            // bloom's objects on black into the new target, over the same depth.
            gpu::TextureRef picture = hand_over_scene_color(bloom_.take_kept_image());
            scene_owed_.reset();
            bloom_.keep_picture(std::move(picture));
            clear_scene(0u, true, true, false);
            return;
        }
        static_cast<void>(bloom_.keep(commands_, in));
    } else {
        vk::GameBloomVk::Params p{};
        p.threshold = bloom.a & 0xFFu;
        p.intensity = bloom.a >> 24u;
        if (bloom.phase == motion::kBloomNormal) {
            p.kind = vk::GameBloomVk::Kind::Normal;
            p.tap1 = (bloom.a >> 8u) & 0xFFu;
            p.tap2 = (bloom.a >> 16u) & 0xFFu;
            p.ring_x = std::bit_cast<float>(bloom.b);
        } else {
            p.kind = vk::GameBloomVk::Kind::Simple;
            p.channel = (bloom.a >> 8u) & 0xFFu;
            p.tap1 = (bloom.a >> 16u) & 0xFFu;
            p.ring_y = std::bit_cast<float>(bloom.b);
        }
        if (!(p.ring_x > 0.0f && p.ring_x < 16.0f) || !(p.ring_y > 0.0f && p.ring_y < 16.0f)) {
            p.ring_x = p.ring_y = 1.0f;
        }
        p.strength = static_cast<float>(std::min(get_render_config().bloom, kMaxBloom)) / 100.0f;
        if (bloom_.prepare(commands_, in, p, allocate)) {
            begin_scene_pass();
            bloom_.finish(commands_, draw_layout_.pass_for(scene_variant(), false), scene_samples_log2(), allocate);
        }
    }
    if (replay_list_ != nullptr) {
        bloom_.hold(replay_list_->held);
    }
    reset_draw_bindings();
}

// A texture the GPU may still read: released once the submissions that can name it completed.
void RendererVk::retire(gpu::TextureRef texture) {
    if (!texture) {
        return;
    }
    // Interpolating, a logged frame's sub-frames may still name it until its game-frame segment comes round.
    if (ring_mode_ == RingMode::Interp) {
        segments_[ring_segment_].retired.push_back(std::move(texture));
    } else {
        frames_[frame_slot_].retired.push_back(std::move(texture));
    }
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
        const VkDescriptorPool pool =
            vk_detail::create_pool(device, vk_detail::kPersistentSets, vk_detail::kPersistentPool);
        persistent_overflow_.push_back(pool);
        info.descriptorPool = pool;
        result = vkAllocateDescriptorSets(device, &info, &set);
    }
    vk::check(result, "vkAllocateDescriptorSets");
    return set;
}

void RendererVk::attach_texture_uploads(TextureCache& cache) {
    textures_ = &cache;
    const VkCommandBuffer uploads = frames_[frame_slot_].uploads;
    cache.backend().set_upload_list(uploads);
    cache.backend().prepare_null_view(uploads);
}

gpu::Table RendererVk::texture_table_for(const TextureHandle* handles, unsigned count, const TextureCache& cache) {
    const VkDescriptorSet set = allocate_table_set(draw_layout_.sets[1]);
    std::array<VkDescriptorImageInfo, kMaxTextureMaps> images{};
    for (unsigned m = 0; m < kMaxTextureMaps; ++m) {
        const bool bound = m < count && handles[m].resource != nullptr;
        images[m] = {VK_NULL_HANDLE, bound ? cache.backend().view(handles[m].srv_index) : cache.backend().null_view(),
                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    }
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = set;
    write.dstBinding = 0;
    write.descriptorCount = kMaxTextureMaps;
    write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    write.pImageInfo = images.data();
    vkUpdateDescriptorSets(vk::Context::get().device(), 1, &write, 0, nullptr);
    if (logging()) {
        std::array<VkImageView, kMaxTextureMaps>& views = recording_log_.tables[reinterpret_cast<std::uint64_t>(set)];
        for (unsigned m = 0; m < kMaxTextureMaps; ++m) {
            views[m] = images[m].imageView;
        }
        note_feedback_reads(views);
    }
    return gpu::Table{reinterpret_cast<std::uint64_t>(set)};
}

// The sampler a packed key describes (layout: pack_sampler_key in render_core.cpp; the D3D12 renderer's
// build_sampler_desc_from_key).
VkSampler RendererVk::sampler_for(std::uint32_t key, const RenderConfig& cfg) {
    const std::uint64_t cache_key = (static_cast<std::uint64_t>(key) << 32u) | sampler_config_key(cfg);
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
    const bool mag_linear = ((key >> 4u) & 1u) != 0u;
    const std::uint32_t minf = (key >> 5u) & 7u;
    const float lod_bias = static_cast<float>(static_cast<std::int8_t>((key >> 8u) & 0xffu)) / 32.0f;
    const float min_lod = static_cast<float>((key >> 16u) & 0xffu) / 16.0f;
    const float max_lod = static_cast<float>((key >> 24u) & 0xffu) / 16.0f;
    // The minified filter (texture_sampler.h min_filter_code): 4 linear, plus 1 the nearest level or 2 between levels.
    const bool min_linear = minf >= 4u;
    // 3: linear without anisotropy (pack_sampler_key's arbitrary levels).
    const bool no_anisotropy = (minf & 3u) == 3u;
    const unsigned mip_mode = no_anisotropy ? 2u : minf & 3u;
    VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    info.magFilter = mag_linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    info.minFilter = min_linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    info.addressModeU = wrap(key & 3u);
    info.addressModeV = wrap((key >> 2u) & 3u);
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

gpu::Table RendererVk::sampler_table_for(const std::uint32_t keys[8]) {
    const RenderConfig cfg = get_render_config();
    const SamplerTableKey table_key = make_sampler_table_key(keys, cfg);
    if (const auto it = sampler_tables_.find(table_key); it != sampler_tables_.end()) {
        return gpu::Table{reinterpret_cast<std::uint64_t>(it->second)};
    }
    const VkDescriptorSet set = allocate_persistent_set(draw_layout_.sets[2]);
    std::array<VkDescriptorImageInfo, 8> samplers{};
    for (unsigned i = 0; i < 8u; ++i) {
        samplers[i] = {sampler_for(keys[i], cfg), VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED};
    }
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = set;
    write.dstBinding = 0;
    write.descriptorCount = 8;
    write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    write.pImageInfo = samplers.data();
    vkUpdateDescriptorSets(vk::Context::get().device(), 1, &write, 0, nullptr);
    sampler_tables_.emplace(table_key, set);
    return gpu::Table{reinterpret_cast<std::uint64_t>(set)};
}

// The s0-s7 table of draws without dynamic samplers: wrap, linear, mip point, every level (RendererD3D12's
// default), rebuilt when the sampler settings change.
void RendererVk::refresh_default_sampler_table() {
    const std::uint32_t config = sampler_config_key(get_render_config());
    if (default_sampler_table_.ptr != 0u && config == default_sampler_config_) {
        return;
    }
    std::uint32_t keys[8]{};
    for (std::uint32_t& key : keys) {
        key = 1u | (1u << 2u) | (1u << 4u) | (5u << 5u) | (0xffu << 24u);
    }
    default_sampler_table_ = sampler_table_for(keys);
    default_sampler_config_ = config;
}

VkDescriptorSet RendererVk::constants_set(VkBuffer b0, VkBuffer b1, VkBuffer palette, VkDeviceSize palette_offset,
                                          VkDeviceSize palette_range, VkBuffer earlier) {
    const bool whole = palette_range == VK_WHOLE_SIZE;
    const bool cached = palette_offset == 0u;
    const ConstantsSetKey key{b0, b1, palette, earlier, whole};
    if (cached) {
        if (const auto it = constants_sets_.find(key); it != constants_sets_.end()) {
            return it->second;
        }
    }
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (cached) {
        set = allocate_persistent_set(draw_layout_.sets[0]);
    } else {
        // A replay's evaluated palettes (their base moves with each sequence's layout): a set of the replay list's.
        for (const auto& [buffers, existing] : replay_constant_sets_) {
            if (buffers[0] == b0 && buffers[1] == b1 && buffers[2] == earlier) {
                return existing;
            }
        }
        VkDescriptorSetAllocateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        info.descriptorPool = replay_list_->descriptors;
        info.descriptorSetCount = 1;
        info.pSetLayouts = &draw_layout_.sets[0];
        vk::check(vkAllocateDescriptorSets(vk::Context::get().device(), &info, &set), "replay constants set");
        replay_constant_sets_.push_back({{b0, b1, earlier}, set});
    }
    // The rows (binding 5) are in the palette's buffer from the same offset: the ring's start, or a replay's palettes.
    const VkDescriptorBufferInfo infos[6] = {
        {b0, 0, sizeof(VertexConstants)},
        {b1, 0, 128u * sizeof(float)},  // a native material's constants (MaterialBlock)
        {palette, palette_offset, palette_range},
        {earlier, 0, VK_WHOLE_SIZE},
        {blend_verdicts_->buffer, 0, VK_WHOLE_SIZE},
        {palette, palette_offset, VK_WHOLE_SIZE},
    };
    VkWriteDescriptorSet writes[6]{};
    for (unsigned i = 0; i < 6u; ++i) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = set;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = i < 2u ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC
            : i == 2u                     ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC
                                          : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[i].pBufferInfo = &infos[i];
    }
    vkUpdateDescriptorSets(vk::Context::get().device(), 6, writes, 0, nullptr);
    if (cached) {
        constants_sets_.emplace(key, set);
    }
    return set;
}

bool RendererVk::ensure_curve_sort() {
    if (curve_classify_ != VK_NULL_HANDLE) {
        return true;
    }
    if (curve_sort_failed_) {
        return false;
    }
    curve_sort_failed_ = true;
    vk::Context& context = vk::Context::get();
    const VkDevice device = context.device();
    VkDescriptorSetLayoutBinding bindings[7]{};
    for (std::uint32_t i = 0; i < 7u; ++i) {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layout_info.bindingCount = 7;
    layout_info.pBindings = bindings;
    vk::check(vkCreateDescriptorSetLayout(device, &layout_info, nullptr, &curve_set_layout_), "curve sort set layout");
    VkPipelineLayoutCreateInfo pipeline_layout_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipeline_layout_info.setLayoutCount = 1;
    pipeline_layout_info.pSetLayouts = &curve_set_layout_;
    vk::check(vkCreatePipelineLayout(device, &pipeline_layout_info, nullptr, &curve_layout_), "curve sort layout");
    const VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 7u * kFramesInFlight};
    VkDescriptorPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool_info.maxSets = kFramesInFlight;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes = &size;
    vk::check(vkCreateDescriptorPool(device, &pool_info, nullptr, &curve_pool_), "curve sort descriptor pool");
    for (const auto& [shader, entry, pipeline] :
         {std::tuple{vk::UtilitySpirv::CurveClassifyCs, "classify", &curve_classify_},
          std::tuple{vk::UtilitySpirv::CurveOffsetsCs, "offsets", &curve_offsets_},
          std::tuple{vk::UtilitySpirv::CurveScatterCs, "scatter", &curve_scatter_}}) {
        const std::span<const std::uint32_t> spirv = vk::utility_spirv(shader);
        VkShaderModuleCreateInfo module_info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        module_info.codeSize = spirv.size_bytes();
        module_info.pCode = spirv.data();
        VkShaderModule module = VK_NULL_HANDLE;
        vk::check(vkCreateShaderModule(device, &module_info, nullptr, &module), "curve sort shader module");
        VkComputePipelineCreateInfo info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        info.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        info.stage.module = module;
        info.stage.pName = entry;
        info.layout = curve_layout_;
        const VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, pipeline);
        vkDestroyShaderModule(device, module, nullptr);
        if (result != VK_SUCCESS) {
            std::cerr << "[curve-sort] pipeline " << entry << ": " << static_cast<int>(result) << '\n';
            return false;
        }
    }
    for (CurveSortFrame& frame : curve_sort_) {
        frame.sorted = context.create_buffer(kCurveSortBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                                                  VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                                                                  VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
                                             "curve sort output", vk::BufferPlace::Device);
        VkDescriptorSetAllocateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        info.descriptorPool = curve_pool_;
        info.descriptorSetCount = 1;
        info.pSetLayouts = &curve_set_layout_;
        if (!frame.sorted || vkAllocateDescriptorSets(device, &info, &frame.set) != VK_SUCCESS) {
            return false;
        }
    }
    curve_sort_failed_ = false;
    return true;
}

std::int32_t RendererVk::sort_curved(gpu::Address vertices, gpu::Address indices, std::uint32_t triangles,
                                     gpu::Address constants, gpu::Address palette, bool palette_rows) {
    CurveSortFrame& frame = curve_sort_[frame_slot_];
    // Its output (arguments, two lists), then its blocks' masks and offsets.
    const std::uint64_t blocks = (triangles + 63u) / 64u;
    const std::uint64_t output = 40u + std::uint64_t{24} * triangles;
    const std::uint64_t bytes = output + 12u * blocks;
    if (replaying_ || triangles == 0u || blocks > 1024u || frame.bytes + bytes > kCurveSortBytes ||
        frame.draws.size() >= 65535u || !ensure_curve_sort()) {
        return -1;
    }
    // The buffers bind whole: each within the device's storage range.
    const std::uint64_t range = vk::Context::get().properties().limits.maxStorageBufferRange;
    if (resident_vertex_bytes_ > range || index_ring_.total_bytes() > range || constant_ring_.total_bytes() > range ||
        matrix_ring_.total_bytes() > range) {
        return -1;
    }
    // One buffer of each kind a frame (the resident vertices and the rings), 32-bit offsets into it.
    const vk::Context& context = vk::Context::get();
    const vk::Context::BufferOffset at[4] = {context.resolve(vertices), context.resolve(indices), context.resolve(constants),
                                             palette_rows ? context.resolve(palette) : vk::Context::BufferOffset{}};
    for (unsigned i = 0; i < 4u; ++i) {
        if ((i < 3u || palette_rows) &&
            (at[i].buffer == VK_NULL_HANDLE || at[i].offset > 0xFFFFFFFFu ||
             (frame.sources[i] != VK_NULL_HANDLE && frame.sources[i] != at[i].buffer))) {
            return -1;
        }
    }
    // The tables the dispatches read, grown now: the draws recorded with this sort must find it recorded.
    const auto room = [&](std::shared_ptr<vk::Buffer>& buffer, std::uint64_t needed, const char* name) {
        if (!buffer || buffer->size < needed) {
            std::uint64_t size = 64u << 10u;
            while (size < needed) {
                size *= 2u;
            }
            buffer = vk::Context::get().create_buffer(size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, name);
        }
        return static_cast<bool>(buffer);
    };
    std::uint64_t all_blocks = blocks;
    for (const CurveSortDraw& draw : frame.draws) {
        all_blocks += (draw.triangles + 63u) / 64u;
    }
    if (!room(frame.records, (frame.draws.size() + 1u) * sizeof(CurveSortDraw), "curve sort draws") ||
        !room(frame.blocks, all_blocks * 8u, "curve sort blocks")) {
        return -1;
    }
    for (unsigned i = 0; i < 4u; ++i) {
        if (at[i].buffer != VK_NULL_HANDLE) {
            frame.sources[i] = at[i].buffer;
        }
    }
    frame.draws.push_back({static_cast<std::uint32_t>(at[0].offset), static_cast<std::uint32_t>(at[1].offset), triangles,
                           static_cast<std::uint32_t>(at[2].offset),
                           palette_rows ? static_cast<std::uint32_t>(at[3].offset) : 0u, palette_rows ? 1u : 0u,
                           static_cast<std::uint32_t>(frame.bytes), static_cast<std::uint32_t>(frame.bytes + output)});
    frame.bytes += bytes;
    return static_cast<std::int32_t>(frame.draws.size() - 1u);
}

void RendererVk::record_curve_sort(VkCommandBuffer commands) {
    CurveSortFrame& frame = curve_sort_[frame_slot_];
    const VkDevice device = vk::Context::get().device();
    std::memcpy(frame.records->mapped, frame.draws.data(), frame.draws.size() * sizeof(CurveSortDraw));
    curve_sort_blocks_.clear();
    for (std::uint32_t draw = 0; draw < frame.draws.size(); ++draw) {
        for (std::uint32_t first = 0; first < frame.draws[draw].triangles; first += 64u) {
            curve_sort_blocks_.push_back({draw, first});
        }
    }
    std::memcpy(frame.blocks->mapped, curve_sort_blocks_.data(), curve_sort_blocks_.size() * sizeof(curve_sort_blocks_[0]));
    // A draw without palette rows reads none: the constants stand in.
    const VkBuffer palette = frame.sources[3] != VK_NULL_HANDLE ? frame.sources[3] : frame.sources[2];
    const std::array<VkBuffer, 7> buffers{frame.records->buffer, frame.sources[0], frame.sources[1], frame.sources[2],
                                          palette, frame.blocks->buffer, frame.sorted->buffer};
    if (frame.bound != buffers) {
        VkDescriptorBufferInfo infos[7]{};
        VkWriteDescriptorSet writes[7]{};
        for (unsigned i = 0; i < 7u; ++i) {
            infos[i] = {buffers[i], 0, VK_WHOLE_SIZE};
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = frame.set;
            writes[i].dstBinding = i;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[i].pBufferInfo = &infos[i];
        }
        vkUpdateDescriptorSets(device, 7, writes, 0, nullptr);
        frame.bound = buffers;
    }
    vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_COMPUTE, curve_layout_, 0, 1, &frame.set, 0, nullptr);
    VkMemoryBarrier between{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    between.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    between.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, curve_classify_);
    vkCmdDispatch(commands, static_cast<std::uint32_t>(curve_sort_blocks_.size()), 1u, 1u);
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1,
                         &between, 0, nullptr, 0, nullptr);
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, curve_offsets_);
    vkCmdDispatch(commands, static_cast<std::uint32_t>(frame.draws.size()), 1u, 1u);
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1,
                         &between, 0, nullptr, 0, nullptr);
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, curve_scatter_);
    vkCmdDispatch(commands, static_cast<std::uint32_t>(curve_sort_blocks_.size()), 1u, 1u);
    // The frame's draws read the lists and their arguments.
    VkMemoryBarrier to_draws{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    to_draws.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    to_draws.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_INDEX_READ_BIT;
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0, 1, &to_draws, 0,
                         nullptr, 0, nullptr);
}

void RendererVk::draw(const DrawCall& call) {
    if (logging()) {
        recording_log_.ops.push_back({ReplayKind::Draw, static_cast<std::uint32_t>(recording_log_.draws.size())});
        recording_log_.draws.push_back(call);
    }
    const LogPause pause{*this};
    begin_scene_pass();
    const VkCommandBuffer commands = commands_;
    const vk::Context& context = vk::Context::get();
    const bool split = call.flat_pipeline != nullptr;
    gpu::Pipeline* const first_pipeline = split ? call.flat_pipeline : call.pipeline;
    if (first_pipeline != bound_.pipeline) {
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, first_pipeline);
        bound_.pipeline = first_pipeline;
    }

    // Set 0: b0, b1 and the palette at their draw's offsets. An address of 0 keeps the previous one (as D3D12's
    // root arguments); before any, the palette ring's start stands in (shaders that read it always bind one).
    const auto resolve = [&context](gpu::Address address, VkBuffer& buffer, std::uint32_t& offset) {
        if (address == 0u) {
            return false;
        }
        const vk::Context::BufferOffset resolved = context.resolve(address);
        buffer = resolved.buffer;
        offset = static_cast<std::uint32_t>(resolved.offset);
        return true;
    };
    bool constants_changed = !bound_.constants_valid;
    if (!bound_.constants_valid) {
        bound_.buffers = {constant_ring_.resource(), constant_ring_.resource(), matrix_ring_.resource()};
        bound_.offsets = {0u, 0u, 0u};
    }
    std::array<VkBuffer, 3> buffers = bound_.buffers;
    std::array<std::uint32_t, 3> offsets = bound_.offsets;
    resolve(call.vs_constants, buffers[0], offsets[0]);
    resolve(call.ps_constants, buffers[1], offsets[1]);
    resolve(call.matrix_palette, buffers[2], offsets[2]);
    // Vertex blending (a sub-frame's paired draws): the earlier draw's vertices from their first byte; unblended draws
    // name the vertex ring, which their shaders never read.
    VkBuffer earlier = reinterpret_cast<VkBuffer>(vertex_ring_.resource());
    std::uint32_t earlier_base = 0u;
    if (call.blend_vertices != 0u) {
        const vk::Context::BufferOffset resolved = context.resolve(call.blend_vertices);
        earlier = resolved.buffer;
        earlier_base = static_cast<std::uint32_t>(resolved.offset);
    }
    const std::uint32_t vertices =
        call.vertex_view.StrideInBytes != 0u ? call.vertex_view.SizeInBytes / call.vertex_view.StrideInBytes : 0u;
    const std::array<std::uint32_t, 5> blend{call.blend_vertices != 0u ? 1u : 0u,
                                             replaying_ ? kFrameValuesElement : kZeroValuesElement,
                                             std::min(call.blend_slot, kMaxBlendPairs - 1u),
                                             call.blend_vertices != 0u && vertices != 0u ? vertices - 1u : 0u,
                                             earlier_base};
    if (!bound_.blend_valid || blend != bound_.blend) {
        vkCmdPushConstants(commands, draw_layout_.layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(blend), blend.data());
        bound_.blend = blend;
        bound_.blend_valid = true;
    }
    if (constants_changed || buffers != bound_.buffers || offsets != bound_.offsets || earlier != bound_.earlier) {
        // The palette's range follows its addressing: palettes indexed from the buffer's base (an interpolating or
        // temporal frame's: its blocks count from the ring's start) reach the buffer's end; a replay's
        // evaluated palettes, from their base in the output; otherwise the draw's own palette.
        constexpr VkDeviceSize kPaletteBytes = draw_rows::kRows * 16u;
        std::array<std::uint32_t, 3> dynamic = offsets;
        VkDescriptorSet set = VK_NULL_HANDLE;
        if (replaying_ && interp_output_ && buffers[2] == interp_output_->buffer) {
            // The table at the draw's slot, after the rows' base (call.matrix_rows: the output's palettes).
            VkBuffer rows = buffers[2];
            std::uint32_t rows_offset = offsets[2];
            resolve(call.matrix_rows, rows, rows_offset);
            set = constants_set(buffers[0], buffers[1], buffers[2], rows_offset, VK_WHOLE_SIZE, earlier);
            dynamic[2] = offsets[2] - rows_offset;
        } else {
            set = constants_set(buffers[0], buffers[1], buffers[2], 0u, offsets[2] == 0u ? VK_WHOLE_SIZE : kPaletteBytes,
                                earlier);
        }
        vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, draw_layout_.layout, 0, 1, &set, 3,
                                dynamic.data());
        bound_.buffers = buffers;
        bound_.offsets = offsets;
        bound_.earlier = earlier;
        bound_.constants_valid = true;
    }
    if (call.texture_table.ptr != 0u && call.texture_table.ptr != bound_.texture_table) {
        const auto set = reinterpret_cast<VkDescriptorSet>(call.texture_table.ptr);
        vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, draw_layout_.layout, 1, 1, &set, 0, nullptr);
        bound_.texture_table = call.texture_table.ptr;
    }
    if (call.sampler_table.ptr != 0u && call.sampler_table.ptr != bound_.sampler_table) {
        const auto set = reinterpret_cast<VkDescriptorSet>(call.sampler_table.ptr);
        vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, draw_layout_.layout, 2, 1, &set, 0, nullptr);
        bound_.sampler_table = call.sampler_table.ptr;
    }

    // The game scissor, scaled and clamped; an empty one rasterises nothing (also CullMode::All).
    if (!bound_.scissor_input_valid || call.scissor.left != bound_.scissor_input.left ||
        call.scissor.top != bound_.scissor_input.top || call.scissor.right != bound_.scissor_input.right ||
        call.scissor.bottom != bound_.scissor_input.bottom) {
        const ScaledSceneRect scaled = rect_pixels(call.scissor.left, call.scissor.top, call.scissor.right,
                                                 call.scissor.bottom, scene_map_);
        if (scaled.empty()) {
            return;
        }
        const VkRect2D scissor{{scaled.left, scaled.top},
                               {static_cast<std::uint32_t>(scaled.right - scaled.left),
                                static_cast<std::uint32_t>(scaled.bottom - scaled.top)}};
        if (!bound_.scissor_valid || std::memcmp(&scissor, &bound_.scissor, sizeof(scissor)) != 0) {
            vkCmdSetScissor(commands, 0, 1, &scissor);
            bound_.scissor = scissor;
            bound_.scissor_valid = true;
        }
        bound_.scissor_input = call.scissor;
        bound_.scissor_input_valid = true;
    }
    // The game's viewport with its depth range (reversed, as D3D12: viewport_pixels); y flipped by a
    // negative height so clip space matches D3D's.
    if (call.has_viewport) {
        const ScaledViewport scaled = viewport_pixels(call.viewport, scene_map_);
        if (scaled.valid()) {
            const VkViewport viewport{scaled.x, scaled.y + scaled.height, scaled.width, -scaled.height,
                                      scaled.min_depth, scaled.max_depth};
            if (!bound_.viewport_valid || std::memcmp(&viewport, &bound_.viewport, sizeof(viewport)) != 0) {
                vkCmdSetViewport(commands, 0, 1, &viewport);
                bound_.viewport = viewport;
                bound_.viewport_valid = true;
            }
        }
    }
    if (!bound_.viewport_valid) {
        bind_scene();
    }
    if (call.vertex_view.BufferLocation != bound_.vertex_address) {
        const vk::Context::BufferOffset vertices = context.resolve(call.vertex_view.BufferLocation);
        vkCmdBindVertexBuffers(commands, 0, 1, &vertices.buffer, &vertices.offset);
        bound_.vertex_address = call.vertex_view.BufferLocation;
    }
    if (call.index_count == 0u) {
        return;
    }
    const bool time_draw = draw_queries_ != VK_NULL_HANDLE && pass_recording_ &&
                           commands == frames_[frame_slot_].commands &&
                           draw_marks_[frame_slot_].size() < kGpuDrawSlots / 2u;
    if (time_draw) {
        vkCmdWriteTimestamp(commands, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, draw_queries_,
                            frame_slot_ * kGpuDrawSlots + 2u * static_cast<std::uint32_t>(draw_marks_[frame_slot_].size()));
    }
    if (call.index_view.BufferLocation != 0u) {
        if (call.index_view.BufferLocation != bound_.index_address) {
            const vk::Context::BufferOffset indices = context.resolve(call.index_view.BufferLocation);
            vkCmdBindIndexBuffer(commands, indices.buffer, indices.offset, VK_INDEX_TYPE_UINT16);
            bound_.index_address = call.index_view.BufferLocation;
        }
        CurveSortFrame& sort = curve_sort_[frame_slot_];
        if (split && call.curve_sort >= 0 && call.curve_sort_list == curve_sort_list_ && !replaying_ &&
            static_cast<std::size_t>(call.curve_sort) < sort.draws.size() && sort.sorted) {
            // The flat triangles, then the sort's lists: the whole triangles, then the patches, each drawn by its
            // arguments.
            if (call.flat_index_count != 0u) {
                vkCmdDrawIndexed(commands, call.flat_index_count, 1, 0, 0, 0);
            }
            const CurveSortDraw& sorted = sort.draws[static_cast<std::size_t>(call.curve_sort)];
            const VkBuffer buffer = sort.sorted->buffer;
            vkCmdBindIndexBuffer(commands, buffer, sorted.output + 40u, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexedIndirect(commands, buffer, sorted.output, 1u, 20u);
            vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, call.pipeline);
            bound_.pipeline = call.pipeline;
            vkCmdBindIndexBuffer(commands, buffer, sorted.output + 40u + VkDeviceSize{12} * sorted.triangles,
                                 VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexedIndirect(commands, buffer, sorted.output + 20u, 1u, 20u);
            bound_.index_address = ~gpu::Address{0};
        } else if (split) {
            if (call.flat_index_count != 0u) {
                vkCmdDrawIndexed(commands, call.flat_index_count, 1, 0, 0, 0);
            }
            if (call.index_count > call.flat_index_count) {
                vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, call.pipeline);
                bound_.pipeline = call.pipeline;
                vkCmdDrawIndexed(commands, call.index_count - call.flat_index_count, 1, call.flat_index_count, 0, 0);
            }
        } else {
            vkCmdDrawIndexed(commands, call.index_count, 1, 0, 0, 0);
        }
    } else {
        vkCmdDraw(commands, call.index_count, 1, 0, 0);
    }
    if (time_draw) {
        std::vector<GpuDrawMark>& marks = draw_marks_[frame_slot_];
        vkCmdWriteTimestamp(commands, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, draw_queries_,
                            frame_slot_ * kGpuDrawSlots + 2u * static_cast<std::uint32_t>(marks.size()) + 1u);
        marks.push_back({call.pipeline, call.index_count / 3u});
    }
}

VkFramebuffer RendererVk::framebuffer_for(vk::Texture& texture) {
    if (texture.framebuffer == VK_NULL_HANDLE) {
        VkFramebufferCreateInfo info{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        info.renderPass = color_pass_;
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
        resolve_scene_depth(commands);
        image_barrier(commands, scene_.depth->image, depth_aspects(depth_format_),
                      VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, kDepthSampleLayout,
                      VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
    } else if (color_image == VK_NULL_HANDLE || color_image != scene_shown_image_) {
        image_barrier(commands, color_image != VK_NULL_HANDLE ? color_image : scene_.color->image,
                      VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                      VK_ACCESS_SHADER_READ_BIT);
    }
}

void RendererVk::transition_scene_to_attachment(VkCommandBuffer commands, bool depth, VkImage color_image) {
    if (depth) {
        image_barrier(commands, scene_.depth->image, depth_aspects(depth_format_), kDepthSampleLayout,
                      VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                      VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                      VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
    } else if (color_image == VK_NULL_HANDLE || color_image != scene_shown_image_) {
        image_barrier(commands, color_image != VK_NULL_HANDLE ? color_image : scene_.color->image,
                      VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                      VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    }
}

// The conversion blit (scene_conversion_shader_source): a full-screen triangle into `dest`, sampling the scene target colour
// or depth. The scene target leaves its attachment layout for the read and returns; `dest` ends shader-readable.
void RendererVk::record_conversion_blit(vk::Texture& dest, unsigned dest_w, unsigned dest_h,
                                        std::uint8_t copy_format, float src_x, float src_y, float step_x, float step_y) {
    end_scene_pass();
    settle_owed_scene();
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
    vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, conversion_layout_, 0, 1, &source, 0, nullptr);
    const SceneConversionShaderConstants constants =
        make_scene_conversion_constants(false, copy_format, src_x, src_y, step_x, step_y);
    vkCmdPushConstants(commands, conversion_layout_, kPushStages, 0, sizeof(constants.values),
                       constants.values.data());
    vkCmdDraw(commands, 3, 1, 0, 0);
    vkCmdEndRenderPass(commands);
    dest.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    transition_scene_to_attachment(commands, false);
    reset_draw_bindings();
    bind_scene();
}

void RendererVk::copy_scene_to_display(const FramePassRequest& params, DisplayTexture& dest) {
    const GpuPassScope pass{*this, GpuPass::DisplayCopy};
    if (!dest.texture) {
        throw std::runtime_error("display picture copy has no destination texture");
    }
    const FramePassGeometry sampling = frame_pass_geometry(dest.width, dest.height, scene_map_);
    const bool logged = logging();
    if (logged) {
        ReplayCopy copy{};
        copy.params = params;
        copy.display = true;
        copy.width = dest.width;
        copy.height = dest.height;
        copy.geometry[0] = sampling.src_x;
        copy.geometry[1] = sampling.src_y;
        copy.geometry[2] = sampling.step_x;
        copy.geometry[3] = sampling.step_y;
        recording_log_.ops.push_back({ReplayKind::Copy, static_cast<std::uint32_t>(recording_log_.copies.size())});
        recording_log_.copies.push_back(copy);
        recording_log_.movie = dest.prerecorded_movie;
    }
    {
        const LogPause pause{*this};
        record_conversion_blit(*dest.texture, dest.width, dest.height, kCopyLayoutDisplay, sampling.src_x, sampling.src_y, sampling.step_x, sampling.step_y);
    }
    if (ring_mode_ == RingMode::Interp && !replaying_) {
        // The display copy closes the game frame: its log is complete and the next frame takes a new segment.
        if (logged) {
            close_replay_log(dest.slot);
        } else {
            ring_advance_pending_ = true;
        }
    }
}

// E-10: the display copy hands the frame over (RendererD3D12::can_hand_over_display_copy): the whole 640x456 frame
// one scene target pixel to one picture pixel, on the plain path (one sample, no sub-frames, no 2D layer or target pass, no
// pending carry). The filter, the display's vertical filter or none, is applied when the frame is presented.
bool RendererVk::can_hand_over_display_copy(const FramePassRequest&, const DisplayTexture& dest) const {
    if (!scene_.color || scene_owed_ || scene_carry_color_ ||
        scene_carry_depth_ || scene_carry_clear_ || ring_mode_ == RingMode::Interp || recording_log_valid_ ||
        run_valid_ || replaying_ || output_scene_active_ || target_pass_active_ || scene_.samples > 1u ||
        display_filter_pipeline_ == VK_NULL_HANDLE) {
        return false;
    }
    const unsigned width = scene_map_.width;
    const unsigned height = scene_map_.height;
    return dest.width == width &&
           dest.height == height && scene_.color->width == width && scene_.color->height == height;
}

bool RendererVk::can_take_scene() const noexcept {
    return scene_.color && !scene_carry_color_ && !scene_carry_depth_ && !scene_carry_clear_ && ring_mode_ != RingMode::Interp &&
           !recording_log_valid_ && !run_valid_ && !replaying_ && !output_scene_active_ && !target_pass_active_ &&
           scene_.samples <= 1u && !scene_.wide;
}

bool RendererVk::take_scene(std::uint64_t key, gpu::TextureRef fresh, TextureCache& textures, gpu::TextureRef& released) {
    end_scene_pass();
    settle_owed_scene();
    if (const auto it = pictures_.find(key); it != pictures_.end()) {
        released = it->second.viewed;
    }
    gpu::TextureRef picture = hand_over_scene_color(std::move(fresh));
    scene_owed_ = {};
    const FrameExtent size = frame_pass_extent(scene_map_);
    return publish_picture(key, size.width, size.height, std::move(picture), 0u, 0u, textures);
}

void RendererVk::open_frame_for_pass(std::uint64_t color_key, std::uint8_t channels, std::uint64_t depth_key,
                                     TextureCache& textures) {
    end_scene_pass();
    settle_owed_scene();
    const VkCommandBuffer commands = commands_;
    const FrameExtent size = frame_pass_extent(scene_map_);
    if (color_key != 0u) {
        transition_scene_for_sampling(commands, false);
        (void)publish_picture(color_key, size.width, size.height, scene_.color, channels, 0u, textures);
    }
    if (depth_key != 0u) {
        // Materials read their textures in the shader-read layout (TextureBackend).
        resolve_scene_depth(commands);
        image_barrier(commands, scene_.depth->image, depth_aspects(depth_format_),
                      VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                      VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
        (void)publish_picture(depth_key, size.width, size.height, scene_.depth, 0u, 0u, textures);
    }
    frame_pass_color_key_ = color_key;
    frame_pass_depth_key_ = depth_key;
}

void RendererVk::close_frame_for_pass(TextureCache& textures) {
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
                      VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                      VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                      VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
    }
    frame_pass_color_key_ = 0u;
    frame_pass_depth_key_ = 0u;
    reset_draw_bindings();
}

gpu::TextureRef RendererVk::hand_over_scene_color(gpu::TextureRef fresh) {
    const GpuPassScope pass{*this, GpuPass::DisplayCopy};
    end_scene_pass();
    vk::Texture& picture = *scene_.color;
    if (fresh && (fresh->width != picture.width || fresh->height != picture.height ||
                  fresh->format != picture.format || fresh->target_view == VK_NULL_HANDLE)) {
        retire(std::move(fresh));
    }
    if (!fresh) {
        fresh = vk::Context::get().create_texture(
            picture.width, picture.height, 1u, picture.format,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            true, "scene target color");
        if (!fresh) {
            throw std::runtime_error("scene target colour for a display copy's hand-over unavailable");
        }
    }
    const VkCommandBuffer commands = commands_;
    // The frame becomes a picture: shader-readable, as presentation and the owed copy read it.
    image_barrier(commands, picture.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                  VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT);
    picture.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    // The idle picture becomes the scene target's colour after the reads of its last presents; its contents are owed.
    image_barrier(commands, fresh->image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    fresh->layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    if (!scene_turned_in_frame_) {
        scene_submitted_color_ = scene_.color;
        scene_turned_in_frame_ = true;
    }
    gpu::TextureRef handed = std::move(scene_.color);
    scene_.color = std::move(fresh);
    scene_.turned = true;
    turn_source_ = VK_NULL_HANDLE;
    scene_owed_ = handed;
    ++gpu_window_.handovers;
    reset_draw_bindings();
    bind_scene();
    return handed;
}

void RendererVk::restore_owed_scene(const VkRect2D* kept) {
    const gpu::TextureRef owed = std::move(scene_owed_);
    vk::Texture& color = *scene_.color;
    const auto width = static_cast<std::int32_t>(color.width);
    const auto height = static_cast<std::int32_t>(color.height);
    // The bands around `kept` (all of the picture without it).
    VkImageCopy regions[4]{};
    std::uint32_t count = 0;
    const auto add = [&](std::int32_t left, std::int32_t top, std::int32_t right, std::int32_t bottom) {
        if (right > left && bottom > top) {
            VkImageCopy& region = regions[count++];
            region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            region.dstSubresource = region.srcSubresource;
            region.srcOffset = {left, top, 0};
            region.dstOffset = region.srcOffset;
            region.extent = {static_cast<std::uint32_t>(right - left), static_cast<std::uint32_t>(bottom - top), 1u};
        }
    };
    if (kept != nullptr) {
        const std::int32_t left = std::clamp(kept->offset.x, 0, width);
        const std::int32_t right = std::clamp(kept->offset.x + static_cast<std::int32_t>(kept->extent.width), left,
                                              width);
        const std::int32_t top = std::clamp(kept->offset.y, 0, height);
        const std::int32_t bottom = std::clamp(kept->offset.y + static_cast<std::int32_t>(kept->extent.height), top,
                                               height);
        add(0, 0, width, top);
        add(0, bottom, width, height);
        add(0, top, left, bottom);
        add(right, top, width, bottom);
        if (count == 0u) {
            ++gpu_window_.owed_free;
            return;
        }
    } else {
        add(0, 0, width, height);
    }
    end_scene_pass();
    const VkCommandBuffer commands = commands_;
    image_barrier(commands, owed->image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                  VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
    image_barrier(commands, color.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
    vkCmdCopyImage(commands, owed->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, color.image,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, count, regions);
    image_barrier(commands, color.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT,
                  VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    image_barrier(commands, owed->image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
    if (kept == nullptr) {
        ++gpu_window_.owed_restored;
    } else {
        ++gpu_window_.owed_banded;
    }
}

VkFramebuffer RendererVk::scene_framebuffer() {
    if (!scene_.turned) {
        return scene_.framebuffer;
    }
    vk::Texture& color = *scene_.color;
    if (color.scene_framebuffer != VK_NULL_HANDLE && color.scene_depth_serial == scene_.depth_serial) {
        return color.scene_framebuffer;
    }
    const VkDevice device = vk::Context::get().device();
    if (color.scene_framebuffer != VK_NULL_HANDLE) {
        // Made with an earlier depth target (an internal-resolution or MSAA change since).
        const VkFramebuffer old = color.scene_framebuffer;
        vk::Context::get().defer([device, old] { vkDestroyFramebuffer(device, old, nullptr); });
        color.scene_framebuffer = VK_NULL_HANDLE;
    }
    const VkImageView views[2] = {color.target_view, scene_.depth->target_view};
    VkFramebufferCreateInfo info{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    info.renderPass = draw_layout_.scene_pass;
    info.attachmentCount = 2;
    info.pAttachments = views;
    info.width = color.width;
    info.height = color.height;
    info.layers = 1;
    vk::check(vkCreateFramebuffer(device, &info, nullptr, &color.scene_framebuffer), "scene target framebuffer (handed over)");
    color.scene_depth_serial = scene_.depth_serial;
    return color.scene_framebuffer;
}

namespace {

// A conversion input (conversion_set_layout_): `view` through the linear clamp sampler, as create_scene_set writes.
void write_conversion_input(VkDescriptorSet set, VkImageView view, VkSampler sampler) {
    const VkDescriptorImageInfo image{VK_NULL_HANDLE, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    const VkDescriptorImageInfo filter{sampler, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED};
    VkWriteDescriptorSet writes[2]{};
    for (unsigned i = 0; i < 2u; ++i) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = set;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = i == 0u ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE : VK_DESCRIPTOR_TYPE_SAMPLER;
        writes[i].pImageInfo = i == 0u ? &image : &filter;
    }
    vkUpdateDescriptorSets(vk::Context::get().device(), 2, writes, 0, nullptr);
}

}  // namespace

VkDescriptorSet RendererVk::scene_color_source() {
    if (!scene_.turned) {
        return scene_.color_source;
    }
    if (turn_source_ == VK_NULL_HANDLE || turn_source_image_ != scene_.color.get() || turn_source_frame_ != frame_index_ ||
        frame_ != &frames_[frame_slot_]) {
        turn_source_ = allocate_frame_set(conversion_set_layout_);
        write_conversion_input(turn_source_, scene_.color->view, linear_clamp_);
        turn_source_image_ = scene_.color.get();
        turn_source_frame_ = frame_index_;
    }
    return turn_source_;
}

bool RendererVk::draw_frame_pass(const FramePassRequest& params, TextureCache& textures) {
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
            w, h, 1u, VK_FORMAT_R8G8B8A8_UNORM,
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
    if (logging()) {
        ReplayCopy copy{};
        copy.params = params;
        copy.dest_key = dest_key;
        copy.width = w;
        copy.height = h;
        copy.geometry[0] = sampling.src_x;
        copy.geometry[1] = sampling.src_y;
        copy.geometry[2] = sampling.step_x;
        copy.geometry[3] = sampling.step_y;
        copy.dest_view = texture.view;
        recording_log_.ops.push_back({ReplayKind::Copy, static_cast<std::uint32_t>(recording_log_.copies.size())});
        recording_log_.copies.push_back(copy);
        recording_log_.written.push_back(texture.view);
    }
    const LogPause pause{*this};

    record_conversion_blit(texture, w, h, params.channels, sampling.src_x, sampling.src_y, sampling.step_x, sampling.step_y);
    return publish_frame_pass(params, current.texture, textures);
}

void RendererVk::create_first_use_resources() {
    vk::Context& context = vk::Context::get();
    probe_target_ = context.create_texture(kScreenProbesMax, 1u, 1u, VK_FORMAT_R8G8B8A8_UNORM,
                                           VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                           true, "screen probes");
    bool ready = static_cast<bool>(probe_target_);
    for (Readback& readback : probe_slots_) {
        readback.buffer = context.create_readback_buffer(kScreenProbesMax * 4u, "screen probe readback");
        ready = ready && readback.buffer;
    }
    if (!ready) {
        throw std::runtime_error("screen probe resources unavailable");
    }
}

bool RendererVk::record_screen_probes(std::size_t slot, ScreenProbeKind kind, std::span<const ScreenProbe> probes) {
    Readback& out = probe_slots_[slot];
    if (out.fence != 0u || probes.size() > kScreenProbesMax) {
        return false;
    }
    end_scene_pass();
    const VkCommandBuffer commands = commands_;
    const bool depth = kind == ScreenProbeKind::Depth;
    if (!depth) {
        settle_owed_scene();
    }
    vk::Texture& target = *probe_target_;
    transition_scene_for_sampling(commands, depth);
    VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    begin.renderPass = color_pass_;
    begin.framebuffer = framebuffer_for(target);
    begin.renderArea = {{0, 0}, {kScreenProbesMax, 1u}};
    vkCmdBeginRenderPass(commands, &begin, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, conversion_pipeline_);
    const VkDescriptorSet source = depth ? scene_.depth_source : scene_color_source();
    vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, conversion_layout_, 0, 1, &source, 0, nullptr);
    for (std::size_t i = 0; i < probes.size(); ++i) {
        if (probes[i].x < 0.0f || probes[i].y < 0.0f) {
            continue;
        }
        const VkViewport viewport{static_cast<float>(i), 1.0f, 1.0f, -1.0f, 0.0f, 1.0f};
        const VkRect2D scissor{{static_cast<std::int32_t>(i), 0}, {1u, 1u}};
        vkCmdSetViewport(commands, 0, 1, &viewport);
        vkCmdSetScissor(commands, 0, 1, &scissor);
        const SceneConversionShaderConstants constants = make_screen_probe_constants(probes[i].x, probes[i].y, scene_map_, depth);
        vkCmdPushConstants(commands, conversion_layout_, kPushStages, 0, sizeof(constants.values), constants.values.data());
        vkCmdDraw(commands, 3, 1, 0, 0);
    }
    vkCmdEndRenderPass(commands);
    transition_scene_to_attachment(commands, depth);
    image_barrier(commands, target.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                  VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {kScreenProbesMax, 1u, 1u};
    VkImage copy_source = target.image;
    if (vk::Context::get().quirks().linear_readback) {
        // Through a linear image the size of the target.
        if (!probe_target_linear_) {
            probe_target_linear_ = vk::Context::get().create_linear_image(target.width, target.height, target.format, "screen probes linear");
        }
        if (probe_target_linear_) {
            image_barrier(commands, probe_target_linear_->image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT,
                          VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
            VkImageCopy copy{};
            copy.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copy.dstSubresource = copy.srcSubresource;
            copy.extent = {kScreenProbesMax, 1u, 1u};
            vkCmdCopyImage(commands, target.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, probe_target_linear_->image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
            image_barrier(commands, probe_target_linear_->image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT,
                          VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
            copy_source = probe_target_linear_->image;
        }
    }
    vkCmdCopyImageToBuffer(commands, copy_source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, out.buffer->buffer, 1, &region);
    VkBufferMemoryBarrier host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    host.srcQueueFamilyIndex = host.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    host.buffer = out.buffer->buffer;
    host.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1, &host,
                         0, nullptr);
    target.layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    reset_draw_bindings();
    bind_scene();
    out.fence = kProbesInFrame;  // fenced at end_frame
    return true;
}

const std::uint8_t* RendererVk::screen_probes_finished(std::size_t slot) {
    Readback& out = probe_slots_[slot];
    if (out.fence == 0u || out.fence == kProbesInFrame || completed_fence_value() < out.fence) {
        return nullptr;
    }
    vk::Context::get().invalidate(*out.buffer);
    out.fence = 0u;
    return reinterpret_cast<const std::uint8_t*>(out.buffer->mapped);
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
    const std::uint64_t bytes = static_cast<std::uint64_t>(w) * h * 4u;
    if (!scene_capture_.buffer || scene_capture_.buffer->size < bytes) {
        scene_capture_.buffer = context.create_readback_buffer(bytes, "scene target capture");
        if (!scene_capture_.buffer) {
            return false;
        }
    }
    end_scene_pass();
    settle_owed_scene();
    const VkCommandBuffer commands = commands_;
    image_barrier(commands, scene_.color->image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {w, h, 1};
    vkCmdCopyImageToBuffer(commands, scene_.color->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           scene_capture_.buffer->buffer, 1, &region);
    image_barrier(commands, scene_.color->image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    scene_capture_.fence = 0u;
    scene_capture_pending_path_ = scene_capture_path_;
    scene_capture_width_ = w;
    scene_capture_height_ = h;
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
    const auto* rgba = reinterpret_cast<const std::uint8_t*>(scene_capture_.buffer->mapped);
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

void RendererVk::debug_log_backbuffer_readback() {
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

}  // namespace galaxy::render
