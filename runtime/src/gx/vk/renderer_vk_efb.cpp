// SPDX-License-Identifier: GPL-3.0-only
// The Vulkan GX renderer's EFB (renderer_vk.h): its targets, draws, clears, copies, peeks and test captures.

#include "renderer_vk_internal.h"
#include "galaxy/gx/vk/renderer_vk.h"

#include "galaxy/content_viewport.h"
#include "galaxy/experimental_ultrawide_aspect.h"
#include "galaxy/gx/host_window.h"
#include "galaxy/gx/render_config.h"
#include "galaxy/gx/shader_gen.h"
#include "galaxy/motion_signals.h"
#include "galaxy/platform_time.h"
#include "galaxy/gx/gx_state.h"
#include "galaxy/gx/texture_cache.h"
#include "galaxy/gx/uber_constants.h"

#include <vk_mem_alloc.h>

#include <algorithm>
#include <bit>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <span>
#include <stdexcept>
#include <thread>

namespace galaxy::gx {

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

// An EFB set at `scale`: targets in their attachment layouts, the framebuffer, and the conversion inputs (allocated
// once per set; rewriting them needs a GPU that no longer reads the old views, which callers ensure).
bool RendererVk::create_efb_set(EfbSet& set, unsigned scale, unsigned samples) {
    vk::Context& context = vk::Context::get();
    const VkDevice device = context.device();
    const std::uint32_t width = kEfbWidth * scale;
    const std::uint32_t height = kEfbHeight * scale;
    gpu::TextureRef color = context.create_texture(
        width, height, 1u, VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
            VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        true, "EFB color");
    gpu::TextureRef depth = context.create_texture(
        width, height, 1u, depth_format_,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        true, "EFB depth");
    if (!color || !depth) {
        std::cerr << "[Renderer] EFB " << width << 'x' << height << " allocation failed\n";
        return false;
    }
    gpu::TextureRef ms_color;
    gpu::TextureRef ms_depth;
    if (samples > 1u) {
        const auto count = static_cast<VkSampleCountFlagBits>(samples);
        ms_color = context.create_texture(width, height, 1u, VK_FORMAT_R8G8B8A8_UNORM,
                                          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, true, "EFB color MSAA", count);
        ms_depth = context.create_texture(width, height, 1u, depth_format_,
                                          VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                          true, "EFB depth MSAA", count);
        if (!ms_color || !ms_depth) {
            std::cerr << "[Renderer] EFB MSAA " << samples << "x allocation failed; single-sample\n";
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
        vk::check(vkCreateFramebuffer(device, &info, nullptr, &framebuffer), "EFB framebuffer");
        return framebuffer;
    };
    const VkImageView single[2] = {color->target_view, depth->target_view};
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    VkFramebuffer resolve_framebuffer = VK_NULL_HANDLE;
    if (samples > 1u) {
        const VkImageView multi[3] = {ms_color->target_view, ms_depth->target_view, color->target_view};
        framebuffer = make_framebuffer(gx_layout_.pass_for(static_cast<unsigned>(std::countr_zero(samples)), false),
                                       multi);
        resolve_framebuffer = make_framebuffer(gx_layout_.efb_pass, single);
    } else {
        framebuffer = make_framebuffer(gx_layout_.efb_pass, single);
    }
    // The new targets enter their attachment layouts now, so every later submission (peeks included) finds them so.
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
// stay allocated for the next create_efb_set).
void RendererVk::destroy_efb_set(EfbSet& set) {
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

bool RendererVk::create_efb_targets(unsigned scale, unsigned samples) {
    // The previous targets are read once more (carry_over_efb_contents), then released after the GPU is done.
    EfbSet made;
    made.color_source = efb_.color_source;
    made.depth_source = efb_.depth_source;
    made.ms_depth_source = efb_.ms_depth_source;
    if (!create_efb_set(made, scale, samples)) {
        return false;
    }
    if (efb_.framebuffer != VK_NULL_HANDLE) {
        // With MSAA on either side the new targets start cleared instead (carry_over_efb_contents).
        const bool carry = efb_.samples == 1u && made.samples == 1u;
        gpu::TextureRef old_color = carry ? efb_.color : nullptr;
        gpu::TextureRef old_depth = carry ? efb_.depth : nullptr;
        efb_carry_clear_ = !carry;
        destroy_efb_set(efb_);
        efb_carry_color_ = std::move(old_color);
        efb_carry_depth_ = std::move(old_depth);
    }
    efb_ = std::move(made);
    efb_scale_ = scale;
    if (efb_.samples > 1u) {
        std::cout << "[Renderer] EFB MSAA " << efb_.samples << "x\n";
    }
    return true;
}

unsigned RendererVk::wanted_msaa_samples() const {
    return std::min(std::bit_floor(std::clamp(get_render_config().msaa_samples, 1u, 8u)), msaa_max_samples_);
}

void RendererVk::resolve_efb_depth(VkCommandBuffer commands) {
    EfbSet& set = efb_;
    if (set.samples <= 1u || !set.depth_dirty || depth_resolve_pipeline_ == VK_NULL_HANDLE) {
        return;
    }
    const GpuPassScope pass{*this, GpuPass::Resolve};
    image_barrier(commands, set.ms_depth->image, depth_aspects(depth_format_),
                  VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, kDepthSampleLayout,
                  VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
    const std::uint32_t width = kEfbWidth * efb_scale_;
    const std::uint32_t height = kEfbHeight * efb_scale_;
    VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    begin.renderPass = gx_layout_.efb_pass;
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
        reset_gx_bindings();
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
// rule): EFB colour and depth (8 bytes per pixel) and, at most, the XFB pair plus one full-picture copy (12 bytes per
// pixel of the 640x456 picture).
bool RendererVk::efb_scale_fits_memory(unsigned scale) const {
    std::uint64_t usage = 0u;
    std::uint64_t budget = 0u;
    if (!vk::Context::get().device_memory_budget(usage, budget)) {
        return true;
    }
    const auto scaled_bytes = [](unsigned s) {
        const double area = static_cast<double>(s) * s;
        return area *
            (static_cast<double>(kEfbWidth) * kEfbHeight * 8.0 + static_cast<double>(kEfbWidth) * 456.0 * 12.0);
    };
    const double needed = scaled_bytes(scale) - scaled_bytes(efb_scale_);
    return static_cast<double>(usage) + needed <= 0.9 * static_cast<double>(budget);
}

// A changed internal resolution, between frames: the GPU finishes with the old targets and every scaled copy.
void RendererVk::apply_pending_efb_scale() {
    unsigned wanted = std::clamp(get_render_config().efb_scale, 1u, kMaxEfbScale);
    wanted = std::min(wanted, host_window::display_efb_scale_cap());
    const unsigned samples = wanted_msaa_samples();
    // Not while the output EFB holds the EFB's place (its scale is the window's).
    if ((wanted == efb_scale_ && samples == efb_.samples) || !efb_.color || output_efb_active_) {
        return;
    }
    if (wanted > efb_scale_ && !efb_scale_fits_memory(wanted)) {
        // Keep the current targets; the setting returns to what is live.
        std::cerr << "[Renderer] internal resolution " << wanted << "x exceeds the video-memory budget; staying at "
                  << efb_scale_ << "x\n";
        RenderConfig render = get_render_config();
        render.efb_scale = efb_scale_;
        set_render_config(render);
        stage_next_start_efb_scale(efb_scale_);
        host_window::efb_scale_rejected.store(wanted, std::memory_order_release);
        if (render.internal_follow_window) {
            follow_window_retry_ms_ = galaxy::steady_ms() + 10'000u;
        }
        return;
    }
    drain_gpu();
    efb_copy_dests_.clear();
    efb_copy_dests_bytes_ = 0u;
    efb_copy_dests_tick_ = 0u;
    for (Frame& frame : frames_) {
        frame.retired.clear();
    }
    const unsigned previous = efb_scale_;
    if (!create_efb_targets(wanted, samples)) {
        RenderConfig render = get_render_config();
        render.efb_scale = previous;
        set_render_config(render);
        stage_next_start_efb_scale(previous);
        host_window::efb_scale_rejected.store(wanted, std::memory_order_release);
        return;
    }
    reset_gx_bindings();
    std::cout << "[Renderer] internal resolution " << previous << "x -> " << wanted << "x (" << kEfbWidth * wanted
              << 'x' << kEfbHeight * wanted << ")\n";
}

// After an internal-resolution change the new EFB starts with the previous targets' contents, resampled: the game
// expects the EFB it left (normally the display copy's clear, whose alpha its shadow and glow passes read).
void RendererVk::carry_over_efb_contents() {
    const VkCommandBuffer commands = commands_;
    if (efb_carry_clear_) {
        efb_carry_clear_ = false;
        const EfbClearValues clear = make_efb_clear_values(0u, 0u, 0x00FFFFFFu, 0u);
        end_efb_pass();
        begin_efb_pass(&clear);
        end_efb_pass();
        reset_gx_bindings();
    }
    if (!efb_carry_color_ || !efb_carry_depth_) {
        efb_carry_color_.reset();
        efb_carry_depth_.reset();
        return;
    }
    vk::Texture& old_color = *efb_carry_color_;
    vk::Texture& old_depth = *efb_carry_depth_;
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
    begin_efb_pass();
    const float w = static_cast<float>(kEfbWidth * efb_scale_);
    const float h = static_cast<float>(kEfbHeight * efb_scale_);
    const VkViewport viewport{0.0f, h, w, -h, 0.0f, 1.0f};
    const VkRect2D scissor{{0, 0}, {kEfbWidth * efb_scale_, kEfbHeight * efb_scale_}};
    vkCmdSetViewport(commands, 0, 1, &viewport);
    vkCmdSetScissor(commands, 0, 1, &scissor);
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, color_carry_pipeline_);
    vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, utility_layout_, 0, 1, &color_set, 0, nullptr);
    vkCmdDraw(commands, 3, 1, 0, 0);
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, depth_carry_pipeline_);
    vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, utility_layout_, 0, 1, &depth_set, 0, nullptr);
    vkCmdDraw(commands, 3, 1, 0, 0);
    end_efb_pass();
    // The old targets are read by this frame: released when its slot comes round.
    retire(std::move(efb_carry_color_));
    retire(std::move(efb_carry_depth_));
    reset_gx_bindings();
}

void RendererVk::begin_efb_pass(const EfbClearValues* clear) {
    if (efb_pass_open_) {
        return;
    }
    const VkCommandBuffer commands = commands_;
    VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    begin.renderPass = gx_layout_.pass_for(efb_samples_log2(), clear != nullptr);
    begin.framebuffer = efb_.framebuffer;
    efb_.depth_dirty = efb_.samples > 1u;
    begin.renderArea = {{0, 0}, {kEfbWidth * efb_scale_, kEfbHeight * efb_scale_}};
    VkClearValue values[2]{};
    if (clear != nullptr) {
        std::memcpy(values[0].color.float32, clear->color.data(), sizeof(float) * 4u);
        values[1].depthStencil = {clear->reversed_depth, 0u};  // reversed-Z, as clear_efb's
        begin.clearValueCount = 2;
        begin.pClearValues = values;
    }
    vkCmdBeginRenderPass(commands, &begin, VK_SUBPASS_CONTENTS_INLINE);
    efb_pass_open_ = true;
    ++gpu_window_.passes;
    // Dynamic state and bindings outlive the pass, but the renderer's own passes in between changed them.
    if (bound_.viewport_valid) {
        vkCmdSetViewport(commands, 0, 1, &bound_.viewport);
    }
    if (bound_.scissor_valid) {
        vkCmdSetScissor(commands, 0, 1, &bound_.scissor);
    }
}

void RendererVk::end_efb_pass() {
    if (!efb_pass_open_) {
        return;
    }
    vkCmdEndRenderPass(commands_);
    efb_pass_open_ = false;
}

void RendererVk::reset_gx_bindings() noexcept {
    bound_ = Bound{};
}

void RendererVk::bind_efb(const float xf_vp[6]) {
    if (logging()) {
        ReplayViewport viewport{};
        viewport.valid = xf_vp != nullptr;
        if (viewport.valid) {
            std::memcpy(viewport.values, xf_vp, sizeof(viewport.values));
        }
        recording_log_.ops.push_back({ReplayKind::BindEfb, static_cast<std::uint32_t>(recording_log_.viewports.size())});
        recording_log_.viewports.push_back(viewport);
    }
    float x = 0.0f, y = 0.0f;
    float w = static_cast<float>(kEfbWidth * efb_scale_);
    float h = static_cast<float>(kEfbHeight * efb_scale_);
    if (xf_vp != nullptr) {
        // GX XF viewport [xs, xo, ys, yo, zs, zo]: centre (xo - 342, yo - 342), half extents |xs|, |ys|.
        const float scale = static_cast<float>(efb_scale_);
        const float hw = std::abs(xf_vp[0]) * scale;
        const float hh = std::abs(xf_vp[2]) * scale;
        x = (xf_vp[1] - 342.0f) * scale - hw;
        y = (xf_vp[3] - 342.0f) * scale - hh;
        w = hw * 2.0f;
        h = hh * 2.0f;
    }
    bound_.viewport = VkViewport{x, y + h, w, -h, 0.0f, 1.0f};
    bound_.viewport_valid = true;
    bound_.scissor = VkRect2D{{0, 0}, {kEfbWidth * efb_scale_, kEfbHeight * efb_scale_}};
    bound_.scissor_valid = true;
    bound_.scissor_input_valid = false;
    if (efb_pass_open_) {
        const VkCommandBuffer commands = commands_;
        vkCmdSetViewport(commands, 0, 1, &bound_.viewport);
        vkCmdSetScissor(commands, 0, 1, &bound_.scissor);
    }
}

void RendererVk::clear_efb(std::uint32_t clear_ar, std::uint32_t clear_gb, std::uint32_t clear_z24,
                           bool color_enable, bool alpha_enable, bool depth_enable, unsigned rect_x, unsigned rect_y,
                           unsigned rect_w, unsigned rect_h, std::uint8_t pixel_format) {
    const ReplayClear logged{clear_ar, clear_gb, clear_z24, color_enable, alpha_enable, depth_enable,
                             rect_x, rect_y, rect_w, rect_h, pixel_format};
    if (logging()) {
        recording_log_.ops.push_back({ReplayKind::Clear, static_cast<std::uint32_t>(recording_log_.clears.size())});
        recording_log_.clears.push_back(logged);
    } else if (ring_mode_ == RingMode::Interp && ring_advance_pending_ && !replaying_) {
        // The display copy's copy-clear, after the log closed: the next log starts with it, so its sub-frames begin
        // from the EFB the frame began from.
        start_clear_ = logged;
        start_clear_valid_ = true;
    }
    const LogPause pause{*this};
    const std::int32_t efb_w = static_cast<std::int32_t>(kEfbWidth * efb_scale_);
    const std::int32_t efb_h = static_cast<std::int32_t>(kEfbHeight * efb_scale_);
    VkRect2D rect{{0, 0}, {static_cast<std::uint32_t>(efb_w), static_cast<std::uint32_t>(efb_h)}};
    if (rect_w != 0u && rect_h != 0u) {
        // Scoped to the copy's source rectangle (scaled), as the hardware's copy-clear.
        const ScaledEfbRect scaled = compute_scaled_efb_rect(
            static_cast<std::int32_t>(rect_x), static_cast<std::int32_t>(rect_y),
            static_cast<std::int32_t>(rect_x + rect_w), static_cast<std::int32_t>(rect_y + rect_h), kEfbWidth,
            kEfbHeight, efb_scale_);
        if (scaled.right <= scaled.left || scaled.bottom <= scaled.top) {
            return;
        }
        rect = VkRect2D{{scaled.left, scaled.top},
                        {static_cast<std::uint32_t>(scaled.right - scaled.left),
                         static_cast<std::uint32_t>(scaled.bottom - scaled.top)}};
    }
    const EfbClearValues clear = make_efb_clear_values(clear_ar, clear_gb, clear_z24, pixel_format);
    // The whole EFB, every channel, while the pass is closed (a copy-clear): the pass begins cleared.
    const bool has_alpha = (pixel_format & ~kEfbClearExact) == 1u;
    if (!efb_pass_open_ && color_enable && (alpha_enable || !has_alpha) && depth_enable && rect.offset.x == 0 &&
        rect.offset.y == 0 && rect.extent.width == static_cast<std::uint32_t>(efb_w) &&
        rect.extent.height == static_cast<std::uint32_t>(efb_h)) {
        begin_efb_pass(&clear);
        return;
    }
    begin_efb_pass();
    const VkCommandBuffer commands = commands_;
    const VkClearRect clear_rect{rect, 0, 1};
    if (color_enable || alpha_enable) {
        if (!has_alpha) {
            alpha_enable = true;
        }
        const VkPipeline rgba = efb_utility_pipeline(EfbUtility::ClearRgba);
        if (color_enable && alpha_enable && rgba != VK_NULL_HANDLE) {
            // Drawn: vkCmdClearAttachments races with draws on this driver (DeviceQuirks::clear_with_draw).
            const VkViewport viewport{0.0f, static_cast<float>(efb_h), static_cast<float>(efb_w),
                                      -static_cast<float>(efb_h), 0.0f, 1.0f};
            vkCmdSetViewport(commands, 0, 1, &viewport);
            vkCmdSetScissor(commands, 0, 1, &rect);
            vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, rgba);
            vkCmdPushConstants(commands, utility_layout_, kPushStages, 0, sizeof(float) * 4u, clear.color.data());
            vkCmdDraw(commands, 3, 1, 0, 0);
            reset_gx_bindings();
            bind_efb(nullptr);
        } else if (color_enable && alpha_enable) {
            VkClearAttachment attachment{};
            attachment.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            attachment.colorAttachment = 0;
            std::memcpy(attachment.clearValue.color.float32, clear.color.data(), sizeof(float) * 4u);
            vkCmdClearAttachments(commands, 1, &attachment, 1, &clear_rect);
        } else {
            // One channel group only: a full-target quad with that write mask, clipped to the rectangle.
            const VkViewport viewport{0.0f, static_cast<float>(efb_h), static_cast<float>(efb_w),
                                      -static_cast<float>(efb_h), 0.0f, 1.0f};
            vkCmdSetViewport(commands, 0, 1, &viewport);
            vkCmdSetScissor(commands, 0, 1, &rect);
            vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              efb_utility_pipeline(color_enable ? EfbUtility::ClearRgb : EfbUtility::ClearAlpha));
            vkCmdPushConstants(commands, utility_layout_, kPushStages, 0, sizeof(float) * 4u, clear.color.data());
            vkCmdDraw(commands, 3, 1, 0, 0);
            reset_gx_bindings();
            bind_efb(nullptr);
        }
    }
    const VkPipeline depth_clear = depth_enable ? efb_utility_pipeline(EfbUtility::ClearDepth) : VK_NULL_HANDLE;
    if (depth_clear != VK_NULL_HANDLE) {
        const VkViewport viewport{0.0f, static_cast<float>(efb_h), static_cast<float>(efb_w),
                                  -static_cast<float>(efb_h), 0.0f, 1.0f};
        const float value[4] = {clear.reversed_depth, 0.0f, 0.0f, 0.0f};
        vkCmdSetViewport(commands, 0, 1, &viewport);
        vkCmdSetScissor(commands, 0, 1, &rect);
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, depth_clear);
        vkCmdPushConstants(commands, utility_layout_, kPushStages, 0, sizeof(value), value);
        vkCmdDraw(commands, 3, 1, 0, 0);
        reset_gx_bindings();
        bind_efb(nullptr);
    } else if (depth_enable) {
        // Reversed-Z: GX's farthest (0xFFFFFF) clears to about 0.
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

// The smallest integer scale whose 640x456 picture covers the window's content rectangle on both axes (the
// launcher's Match Output rule, as RendererD3D12's).
unsigned RendererVk::output_efb_scale_for_window() const {
    const auto aspect = galaxy::experimental_effective_host_aspect();
    const galaxy::ContentViewport content = galaxy::fit_content_viewport(
        static_cast<int>(host_window::backbuffer_width), static_cast<int>(host_window::backbuffer_height),
        aspect ? aspect->content_aspect : galaxy::kWiiContentAspect);
    if (content.width <= 0 || content.height <= 0) {
        return 0u;
    }
    const double needed = std::max(content.width / static_cast<double>(kEfbWidth), content.height / 456.0);
    return std::clamp(static_cast<unsigned>(std::ceil(needed - 1e-6)), 1u, kMaxEfbScale);
}

// The EFB and the output EFB trade places: targets, scale and their copy sources.
void RendererVk::swap_efb_sets() {
    end_efb_pass();
    std::swap(efb_, alt_efb_);
    std::swap(efb_scale_, alt_efb_scale_);
    output_efb_active_ = !output_efb_active_;
    reset_gx_bindings();
}

bool RendererVk::begin_output_efb() {
    if (output_efb_active_) {
        return false;
    }
    // Without hud_output_resolution the game's layouts keep at least the Wii's own density on a window wider than
    // 16:9 or narrower than 4:3, never above the window's.
    const unsigned scale = get_render_config().hud_output_resolution
        ? output_efb_scale_for_window()
        : std::min(output_efb_scale_for_window(),
                   galaxy::layout_density_scale(galaxy::experimental_effective_host_aspect()));
    if (scale <= efb_scale_) {
        return false;
    }
    if (logging()) {
        recording_log_.ops.push_back({ReplayKind::OutputEfbBegin, scale});
    }
    return enter_output_efb(scale);
}

// The whole scene EFB resampled into the output EFB with the display copy's identity settings (no filter, gamma 1.0,
// alpha kept), which then takes the EFB's place with its depth cleared to the far plane.
bool RendererVk::enter_output_efb(unsigned scale) {
    const LogPause pause(*this);
    if (!alt_efb_.color || alt_efb_scale_ != scale) {
        if (alt_efb_.color) {
            destroy_efb_set(alt_efb_);
        }
        if (!create_efb_set(alt_efb_, scale)) {
            alt_efb_scale_ = 0u;
            return false;
        }
        alt_efb_scale_ = scale;
        if (!replaying_) {
            std::cout << "[Renderer] 2D layer at " << scale << "x (" << kEfbWidth * scale << 'x' << kEfbHeight * scale
                      << ")\n";
        }
    }
    EfbCopyParams params{};
    params.src_width = static_cast<std::uint16_t>(kEfbWidth);
    params.src_height = static_cast<std::uint16_t>(kEfbHeight);
    params.y_scale_raw = 256u;
    params.filter0_raw = (21u << 12u) | (22u << 18u);
    params.filter1_raw = 21u;
    const unsigned width = kEfbWidth * scale;
    const unsigned height = kEfbHeight * scale;
    const EfbCopySamplingGeometry sampling = compute_xfb_sampling_geometry(params, width, height, efb_scale_);
    record_conversion_blit(*alt_efb_.color, width, height, params, false, false, true, 15u, sampling.src_x,
                           sampling.src_y, sampling.step_x, sampling.step_y, sampling.filter_row_offset);
    end_efb_pass();
    image_barrier(commands_, alt_efb_.color->image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    alt_efb_.color->layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    swap_efb_sets();
    bind_efb(nullptr);
    clear_efb(0u, 0u, 0x00FFFFFFu, false, false, true);
    return true;
}

void RendererVk::end_output_efb() {
    if (!output_efb_active_) {
        return;
    }
    if (logging()) {
        recording_log_.ops.push_back({ReplayKind::SwapEfbSets, 0u});
    }
    swap_efb_sets();
    bind_efb(nullptr);
}

bool RendererVk::copy_output_efb_to_texture(const EfbCopyParams& params, TextureCache& textures) {
    if (output_efb_active_ || !alt_efb_.color) {
        return copy_efb_to_texture(params, textures, false);
    }
    if (logging()) {
        recording_log_.ops.push_back({ReplayKind::SwapEfbSets, 0u});
    }
    swap_efb_sets();
    bind_efb(nullptr);
    const bool updated = copy_efb_to_texture(params, textures, false);
    if (logging()) {
        recording_log_.ops.push_back({ReplayKind::SwapEfbSets, 0u});
    }
    swap_efb_sets();
    bind_efb(nullptr);
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

// SMAA or FXAA of the 3D scene at its end, before the 2D layer draws: the EFB pass ends for the passes that read
// a copy of the scene, then the result is drawn into it again.
void RendererVk::record_scene_aa() {
    if (!post_.ready() || !efb_.color || post_.settings().aa == PostProcessSettings::Aa::kOff || efb_.samples > 1u) {
        return;
    }
    const GpuPassScope pass{*this, GpuPass::SceneAa};
    if (logging()) {
        recording_log_.ops.push_back({ReplayKind::SceneAa, 0u});
    }
    // The 640x456 picture the display copy takes, against its size on screen, decides AUTO.
    const auto aspect = galaxy::experimental_effective_host_aspect();
    const galaxy::ContentViewport content = galaxy::fit_content_viewport(
        static_cast<int>(host_window::backbuffer_width), static_cast<int>(host_window::backbuffer_height),
        aspect ? aspect->content_aspect : galaxy::kWiiContentAspect);
    const vk::Texture& color = *efb_.color;
    const auto allocate = [this](VkDescriptorSetLayout layout) { return allocate_post_set(layout); };
    end_efb_pass();
    const AaRun ran = post_.prepare_scene_aa(
        commands_, color, color.width, color.height, kEfbWidth * efb_scale_, 456u * efb_scale_,
        static_cast<unsigned>(std::max(content.width, 1)), static_cast<unsigned>(std::max(content.height, 1)),
        allocate);
    begin_efb_pass();
    post_.finish_scene_aa(commands_, ran, color.width, color.height, allocate);
    if (replay_list_ != nullptr && ran != AaRun::kNone) {
        post_.hold_scene_targets(replay_list_->held);
    }
    reset_gx_bindings();
}

// The game's bloom (game_bloom_vk.h) at this point of the stream: the EFB pass ends for the passes that copy or read
// the scene, and the composite is drawn in it again. The game's frame is its 640 x 456 at the internal scale.
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
    if (!efb_.color) {
        return;
    }
    const GpuPassScope pass{*this, GpuPass::Bloom};
    vk::GameBloomVk::Inputs in{};
    in.color = efb_.color.get();
    in.scale = efb_scale_;
    in.frame_width = std::min(kEfbWidth * efb_scale_, efb_.color->width);
    in.frame_height = std::min(kGameFrameHeight * efb_scale_, efb_.color->height);
    const auto allocate = [this](VkDescriptorSetLayout layout) { return allocate_post_set(layout); };
    end_efb_pass();
    if (bloom.phase == motion::kBloomKeep) {
        static_cast<void>(bloom_.keep(commands_, in));
    } else {
        vk::GameBloomVk::Params p{};
        p.threshold = bloom.a & 0xFFu;
        p.intensity = bloom.a >> 24u;
        if (bloom.phase == motion::kBloomRestore) {
            p.kind = vk::GameBloomVk::Kind::Restore;
        } else if (bloom.phase == motion::kBloomNormal) {
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
            begin_efb_pass();
            bloom_.finish(commands_, gx_layout_.pass_for(efb_samples_log2(), false), efb_samples_log2(), allocate);
        }
    }
    if (replay_list_ != nullptr) {
        bloom_.hold(replay_list_->held);
    }
    reset_gx_bindings();
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

gpu::Table RendererVk::texture_table_for(const TextureHandle* handles, unsigned count, const TextureCache& cache,
                                         bool) {
    const VkDescriptorSet set = allocate_table_set(gx_layout_.sets[1]);
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

// The GX sampler a packed key describes (layout: pack_sampler_key in gx_backend.cpp; the D3D12 renderer's
// build_sampler_desc_from_key).
VkSampler RendererVk::sampler_for(std::uint32_t key, const RenderConfig& cfg) {
    const std::uint64_t cache_key = (static_cast<std::uint64_t>(key) << 32u) | sampler_config_key(cfg);
    if (const auto it = samplers_.find(cache_key); it != samplers_.end()) {
        return it->second;
    }
    const vk::Context& context = vk::Context::get();
    const auto wrap = [](std::uint32_t gx) {
        switch (gx) {
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
    // TexMinFilter: 0 near, 1 near-mip-near, 2 near-mip-linear, 4 linear, 5 linear-mip-near, 6 linear-mip-linear.
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
    vk::check(vkCreateSampler(context.device(), &info, nullptr, &sampler), "GX sampler");
    samplers_.emplace(cache_key, sampler);
    return sampler;
}

gpu::Table RendererVk::sampler_table_for(const std::uint32_t keys[8]) {
    const RenderConfig cfg = get_render_config();
    const SamplerTableKey table_key = make_sampler_table_key(keys, cfg);
    if (const auto it = sampler_tables_.find(table_key); it != sampler_tables_.end()) {
        return gpu::Table{reinterpret_cast<std::uint64_t>(it->second)};
    }
    const VkDescriptorSet set = allocate_persistent_set(gx_layout_.sets[2]);
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
        set = allocate_persistent_set(gx_layout_.sets[0]);
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
        info.pSetLayouts = &gx_layout_.sets[0];
        vk::check(vkAllocateDescriptorSets(vk::Context::get().device(), &info, &set), "replay constants set");
        replay_constant_sets_.push_back({{b0, b1, earlier}, set});
    }
    const VkDescriptorBufferInfo infos[5] = {
        {b0, 0, sizeof(GxVsConstants)},
        {b1, 0, sizeof(GxPsConstants)},
        {palette, palette_offset, palette_range},
        {earlier, 0, VK_WHOLE_SIZE},
        {blend_verdicts_->buffer, 0, VK_WHOLE_SIZE},
    };
    VkWriteDescriptorSet writes[5]{};
    for (unsigned i = 0; i < 5u; ++i) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = set;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = i < 2u ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC
            : i == 2u                     ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC
                                          : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[i].pBufferInfo = &infos[i];
    }
    vkUpdateDescriptorSets(vk::Context::get().device(), 5, writes, 0, nullptr);
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
    const std::string source = ShaderGenerator::curve_sort_source();
    for (const auto& [entry, pipeline] : {std::pair{"classify", &curve_classify_}, std::pair{"offsets", &curve_offsets_},
                                          std::pair{"scatter", &curve_scatter_}}) {
        std::string error;
        const std::vector<std::uint32_t> spirv = vk::compile_hlsl(source, vk::ShaderStage::Compute, entry, {}, error);
        if (spirv.empty()) {
            std::cerr << "[curve-sort] " << error << '\n';
            return false;
        }
        VkShaderModuleCreateInfo module_info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        module_info.codeSize = spirv.size() * sizeof(std::uint32_t);
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
    begin_efb_pass();
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
        vkCmdPushConstants(commands, gx_layout_.layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(blend), blend.data());
        bound_.blend = blend;
        bound_.blend_valid = true;
    }
    if (constants_changed || buffers != bound_.buffers || offsets != bound_.offsets || earlier != bound_.earlier) {
        // The palette's range follows its addressing: palettes indexed from the buffer's base (an interpolating or
        // temporal frame's: mtx_palette_base counts from the ring's start) reach the buffer's end; a replay's
        // evaluated palettes, from their base in the output; otherwise the draw's own palette.
        constexpr VkDeviceSize kPaletteBytes = GxState::kMatrixPaletteWords * sizeof(std::uint32_t);
        std::array<std::uint32_t, 3> dynamic = offsets;
        VkDescriptorSet set = VK_NULL_HANDLE;
        if (replaying_ && interp_output_ && buffers[2] == interp_output_->buffer) {
            set = constants_set(buffers[0], buffers[1], buffers[2], offsets[2], VK_WHOLE_SIZE, earlier);
            dynamic[2] = 0u;
        } else {
            set = constants_set(buffers[0], buffers[1], buffers[2], 0u, offsets[2] == 0u ? VK_WHOLE_SIZE : kPaletteBytes,
                                earlier);
        }
        vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, gx_layout_.layout, 0, 1, &set, 3,
                                dynamic.data());
        bound_.buffers = buffers;
        bound_.offsets = offsets;
        bound_.earlier = earlier;
        bound_.constants_valid = true;
    }
    if (call.texture_table.ptr != 0u && call.texture_table.ptr != bound_.texture_table) {
        const auto set = reinterpret_cast<VkDescriptorSet>(call.texture_table.ptr);
        vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, gx_layout_.layout, 1, 1, &set, 0, nullptr);
        bound_.texture_table = call.texture_table.ptr;
    }
    if (call.sampler_table.ptr != 0u && call.sampler_table.ptr != bound_.sampler_table) {
        const auto set = reinterpret_cast<VkDescriptorSet>(call.sampler_table.ptr);
        vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, gx_layout_.layout, 2, 1, &set, 0, nullptr);
        bound_.sampler_table = call.sampler_table.ptr;
    }

    // The GX scissor, scaled and clamped; an empty one rasterises nothing (also CullMode::All).
    if (!bound_.scissor_input_valid || call.scissor.left != bound_.scissor_input.left ||
        call.scissor.top != bound_.scissor_input.top || call.scissor.right != bound_.scissor_input.right ||
        call.scissor.bottom != bound_.scissor_input.bottom) {
        const ScaledEfbRect scaled = compute_scaled_efb_rect(call.scissor.left, call.scissor.top, call.scissor.right,
                                                             call.scissor.bottom, kEfbWidth, kEfbHeight, efb_scale_);
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
    // The XF viewport with the GX depth range (reversed-Z, as D3D12: compute_scaled_gx_viewport); y flipped by a
    // negative height so clip space matches D3D's.
    if (call.has_viewport) {
        const ScaledGxViewport scaled = compute_scaled_gx_viewport(call.xf_viewport, efb_scale_);
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
        bind_efb(nullptr);
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
        info.renderPass = texture.format == VK_FORMAT_R8_UNORM ? grey_pass_ : color_pass_;
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

void RendererVk::transition_efb_for_sampling(VkCommandBuffer commands, bool depth) {
    if (depth) {
        resolve_efb_depth(commands);
        image_barrier(commands, efb_.depth->image, depth_aspects(depth_format_),
                      VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, kDepthSampleLayout,
                      VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
    } else {
        image_barrier(commands, efb_.color->image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                      VK_ACCESS_SHADER_READ_BIT);
    }
}

void RendererVk::transition_efb_to_attachment(VkCommandBuffer commands, bool depth) {
    if (depth) {
        image_barrier(commands, efb_.depth->image, depth_aspects(depth_format_), kDepthSampleLayout,
                      VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                      VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                      VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
    } else {
        image_barrier(commands, efb_.color->image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                      VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    }
}

// The conversion blit (efb_conversion_shader_source): a full-screen triangle into `dest`, sampling the EFB colour
// or depth. The EFB leaves its attachment layout for the read and returns; `dest` ends shader-readable.
void RendererVk::record_conversion_blit(vk::Texture& dest, unsigned dest_w, unsigned dest_h,
                                        const EfbCopyParams& params, bool is_depth_copy, bool intensity,
                                        bool keep_alpha, std::uint8_t copy_format, float src_x, float src_y,
                                        float step_x, float step_y, float filter_row_offset) {
    end_efb_pass();
    const VkCommandBuffer commands = commands_;
    transition_efb_for_sampling(commands, is_depth_copy);
    const bool grey = dest.format == VK_FORMAT_R8_UNORM;
    VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    begin.renderPass = grey ? grey_pass_ : color_pass_;
    begin.framebuffer = framebuffer_for(dest);
    begin.renderArea = {{0, 0}, {dest_w, dest_h}};
    vkCmdBeginRenderPass(commands, &begin, VK_SUBPASS_CONTENTS_INLINE);
    const VkViewport viewport{0.0f, static_cast<float>(dest_h), static_cast<float>(dest_w),
                              -static_cast<float>(dest_h), 0.0f, 1.0f};
    const VkRect2D scissor{{0, 0}, {dest_w, dest_h}};
    vkCmdSetViewport(commands, 0, 1, &viewport);
    vkCmdSetScissor(commands, 0, 1, &scissor);
    // A display copy one EFB pixel to one XFB pixel without gamma: its filter alone (display_filter_shader_source).
    const bool display_filter = copy_format == 15u && keep_alpha && !intensity && !is_depth_copy && !grey &&
                                (params.gamma & 3u) == 0u && step_x == 1.0f && step_y == 1.0f &&
                                display_filter_pipeline_ != VK_NULL_HANDLE;
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      display_filter ? display_filter_pipeline_
                      : grey         ? grey_conversion_pipeline_
                                     : conversion_pipeline_);
    const VkDescriptorSet source = is_depth_copy ? efb_.depth_source : efb_.color_source;
    vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, conversion_layout_, 0, 1, &source, 0, nullptr);
    const EfbConversionShaderConstants constants =
        make_efb_conversion_constants(params, is_depth_copy, intensity, keep_alpha, copy_format, src_x, src_y,
                                      step_x, step_y, filter_row_offset, efb_scale_, kEfbHeight);
    vkCmdPushConstants(commands, conversion_layout_, kPushStages, 0, sizeof(constants.values),
                       constants.values.data());
    vkCmdDraw(commands, 3, 1, 0, 0);
    vkCmdEndRenderPass(commands);
    dest.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    transition_efb_to_attachment(commands, is_depth_copy);
    reset_gx_bindings();
    bind_efb(nullptr);
}

void RendererVk::copy_efb_to_xfb(const EfbCopyParams& params, XfbTexture& dest) {
    const GpuPassScope pass{*this, GpuPass::XfbCopy};
    if (!dest.texture) {
        throw std::runtime_error("XFB copy has no destination texture");
    }
    // Display copies never convert to intensity; gamma and the copy filter apply.
    const EfbCopySamplingGeometry sampling = compute_xfb_sampling_geometry(params, dest.width, dest.height,
                                                                           efb_scale_);
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
        copy.geometry[4] = sampling.filter_row_offset;
        recording_log_.ops.push_back({ReplayKind::Copy, static_cast<std::uint32_t>(recording_log_.copies.size())});
        recording_log_.copies.push_back(copy);
        recording_log_.surround = dest.safety_surround;
        recording_log_.movie = dest.prerecorded_movie;
    }
    {
        const LogPause pause{*this};
        record_conversion_blit(*dest.texture, dest.width, dest.height, params, false, false, true, 15u,
                               sampling.src_x, sampling.src_y, sampling.step_x, sampling.step_y,
                               sampling.filter_row_offset);
    }
    if (ring_mode_ == RingMode::Interp && !replaying_) {
        // The display copy closes the game frame: its log is complete and the next frame takes a new segment.
        if (logged) {
            close_replay_log(dest.guest_addr);
        } else {
            ring_advance_pending_ = true;
        }
    }
}

bool RendererVk::copy_efb_to_texture(const EfbCopyParams& params, TextureCache& textures, bool is_depth_copy) {
    const GpuPassScope pass{*this, GpuPass::EfbCopy};
    ++gpu_window_.copies;
    // The game composites its picture from EFB->texture copies; half_scale halves both dimensions (a 2x2 box).
    const unsigned dw_unscaled = params.half_scale ? std::max(1u, params.src_width / 2u) : params.src_width;
    const unsigned dh_unscaled = params.half_scale ? std::max(1u, params.src_height / 2u) : params.src_height;
    const bool native = efb_scale_ > 1u && is_effect_copy(dw_unscaled, dh_unscaled);
    const ScaledEfbCopyExtent extent = compute_scaled_efb_copy_extent(
        static_cast<std::uint16_t>(dw_unscaled), static_cast<std::uint16_t>(dh_unscaled), native ? 1u : efb_scale_);
    const unsigned w = extent.width;
    const unsigned h = extent.height;
    if (w == 0u || h == 0u) {
        return false;
    }
    // Keyed by address, format and source: one address alternates between formats and copies of each.
    const std::uint64_t dest_key = static_cast<std::uint64_t>(params.dest_addr) |
        (static_cast<std::uint64_t>(params.target_format) << 32u) | (is_depth_copy ? (1ull << 40u) : 0ull) |
        (native ? (1ull << 41u) : 0ull);
    EfbCopyDest& dest = efb_copy_dests_[dest_key];
    dest.last_used = ++efb_copy_dests_tick_;
    if (!dest.texture || dest.width != w || dest.height != h) {
        if (dest.texture) {
            retire(std::move(dest.texture));
            efb_copy_dests_bytes_ -= std::min(efb_copy_dests_bytes_, dest.bytes);
        }
        const bool grey = efb_copy_single_channel(params.target_format, is_depth_copy);
        dest.texture = vk::Context::get().create_texture(
            w, h, 1u, grey ? VK_FORMAT_R8_UNORM : VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            true, nullptr);
        if (dest.texture && grey && !vk::Context::get().grey_view(*dest.texture)) {
            dest.texture.reset();
        }
        if (!dest.texture) {
            efb_copy_dests_.erase(dest_key);
            static std::atomic<unsigned> reported{0u};
            if (reported.fetch_add(1u, std::memory_order_relaxed) < 8u) {
                std::cerr << "[efb-copy] destination allocation failed size=" << w << 'x' << h
                          << "; copy skipped\n";
            }
            return false;
        }
        dest.width = w;
        dest.height = h;
        dest.bytes = dest.texture->allocation_bytes;
        efb_copy_dests_bytes_ += dest.bytes;
    }
    // Over budget: the least recently used destinations go (a destination is a pure function of its key).
    const std::uint64_t budget = efb_copy_dest_budget_bytes();
    while (budget != 0u && efb_copy_dests_bytes_ > budget) {
        auto victim = efb_copy_dests_.end();
        for (auto it = efb_copy_dests_.begin(); it != efb_copy_dests_.end(); ++it) {
            if (it->first != dest_key && (victim == efb_copy_dests_.end() ||
                                          it->second.last_used < victim->second.last_used)) {
                victim = it;
            }
        }
        if (victim == efb_copy_dests_.end()) {
            break;
        }
        if (victim->second.texture) {
            retire(std::move(victim->second.texture));
        }
        efb_copy_dests_bytes_ -= std::min(efb_copy_dests_bytes_, victim->second.bytes);
        efb_copy_dests_.erase(victim);
    }
    EfbCopyDest& current = efb_copy_dests_[dest_key];
    vk::Texture& texture = *current.texture;
    const auto identity = efb_identity_copy_rect(params, is_depth_copy, efb_scale_, w, h);
    const EfbCopySamplingGeometry sampling = compute_efb_copy_sampling_geometry(params, w, h, efb_scale_);
    if (logging()) {
        ReplayCopy copy{};
        copy.params = params;
        copy.depth = is_depth_copy;
        copy.dest_key = dest_key;
        copy.width = w;
        copy.height = h;
        copy.geometry[0] = sampling.src_x;
        copy.geometry[1] = sampling.src_y;
        copy.geometry[2] = sampling.step_x;
        copy.geometry[3] = sampling.step_y;
        copy.geometry[4] = sampling.filter_row_offset;
        copy.exact = identity.has_value();
        if (identity) {
            copy.box_left = identity->left;
            copy.box_top = identity->top;
        }
        copy.dest_view = texture.view;
        recording_log_.ops.push_back({ReplayKind::Copy, static_cast<std::uint32_t>(recording_log_.copies.size())});
        recording_log_.copies.push_back(copy);
        recording_log_.written.push_back(texture.view);
    }
    const LogPause pause{*this};

    if (const auto& box = identity) {
        // An exact colour copy: the EFB's texels as they are.
        end_efb_pass();
        const VkCommandBuffer commands = commands_;
        image_barrier(commands, efb_.color->image, VK_IMAGE_ASPECT_COLOR_BIT,
                      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                      VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
        image_barrier(commands, texture.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                      VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
        VkImageCopy region{};
        region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.srcOffset = {static_cast<std::int32_t>(box->left), static_cast<std::int32_t>(box->top), 0};
        region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.extent = {w, h, 1};
        vkCmdCopyImage(commands, efb_.color->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, texture.image,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        image_barrier(commands, texture.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT,
                      VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                      VK_ACCESS_SHADER_READ_BIT);
        image_barrier(commands, efb_.color->image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                      VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
        texture.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        reset_gx_bindings();
        bind_efb(nullptr);
        return textures.register_efb_copy(params.dest_addr, params, current.texture);
    }
    // Intensity copies to RA4/RA8 (IA4/IA8) keep the source alpha; pure-I formats replicate Y into alpha.
    const bool keep_alpha = params.target_format == 2u || params.target_format == 3u;
    record_conversion_blit(texture, w, h, params, is_depth_copy, params.intensity, keep_alpha, params.target_format,
                           sampling.src_x, sampling.src_y, sampling.step_x, sampling.step_y,
                           sampling.filter_row_offset);
    return textures.register_efb_copy(params.dest_addr, params, current.texture);
}

void RendererVk::create_first_use_resources() {
    vk::Context& context = vk::Context::get();
    const VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    peek_target_ = context.create_texture(1u, 1u, 1u, VK_FORMAT_R8G8B8A8_UNORM, usage, true, "peek target");
    field_target_ = context.create_texture(kEfbWidth, kEfbHeight, 1u, VK_FORMAT_R8G8B8A8_UNORM, usage, true,
                                           "peek field target");
    peek_readback_.buffer = context.create_readback_buffer(4u, "peek readback");
    for (Readback& readback : pointer_depth_ring_) {
        readback.buffer = context.create_readback_buffer(kEfbWidth * kEfbHeight * 4u, "pointer depth readback");
    }
    if (!peek_target_ || !field_target_ || !peek_readback_.buffer) {
        throw std::runtime_error("EFB peek resources unavailable");
    }
}

// The conversion draw into `target` (sampling the EFB as last submitted, in its own command buffer, or mid-frame)
// and the copy of its texels to `readback`.
void RendererVk::record_peek_field(VkCommandBuffer commands, vk::Texture& target, EfbPeekKind kind,
                                  const EfbConversionShaderConstants& constants, unsigned width, unsigned height,
                                  VkBuffer readback) {
    const bool depth = kind == EfbPeekKind::Depth;
    transition_efb_for_sampling(commands, depth);
    VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    begin.renderPass = color_pass_;
    begin.framebuffer = framebuffer_for(target);
    begin.renderArea = {{0, 0}, {width, height}};
    vkCmdBeginRenderPass(commands, &begin, VK_SUBPASS_CONTENTS_INLINE);
    const VkViewport viewport{0.0f, static_cast<float>(height), static_cast<float>(width),
                              -static_cast<float>(height), 0.0f, 1.0f};
    const VkRect2D scissor{{0, 0}, {width, height}};
    vkCmdSetViewport(commands, 0, 1, &viewport);
    vkCmdSetScissor(commands, 0, 1, &scissor);
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, conversion_pipeline_);
    const VkDescriptorSet source = depth ? efb_.depth_source : efb_.color_source;
    vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, conversion_layout_, 0, 1, &source, 0, nullptr);
    vkCmdPushConstants(commands, conversion_layout_, kPushStages, 0, sizeof(constants.values),
                       constants.values.data());
    vkCmdDraw(commands, 3, 1, 0, 0);
    vkCmdEndRenderPass(commands);
    transition_efb_to_attachment(commands, depth);
    image_barrier(commands, target.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                  VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {width, height, 1};
    VkImage copy_source = target.image;
    if (vk::Context::get().quirks().linear_readback) {
        // Through a linear image the size of the target (Dolphin's VKStagingTexture on Qualcomm).
        gpu::TextureRef& twin = linear_twins_[&target];
        if (!twin) {
            twin = vk::Context::get().create_linear_image(target.width, target.height, target.format, "peek linear");
            std::fprintf(stderr, "[vk-compat] %ux%u readbacks through a linear image: %s\n", target.width,
                         target.height, twin ? "yes" : "no linear transfers for the format");
        }
        if (twin) {
            image_barrier(commands, twin->image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT,
                          VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
            VkImageCopy copy{};
            copy.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copy.dstSubresource = copy.srcSubresource;
            copy.extent = {width, height, 1};
            vkCmdCopyImage(commands, target.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, twin->image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
            image_barrier(commands, twin->image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT,
                          VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
            copy_source = twin->image;
        }
    }
    vkCmdCopyImageToBuffer(commands, copy_source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback, 1, &region);
    VkBufferMemoryBarrier host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    host.srcQueueFamilyIndex = host.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    host.buffer = readback;
    host.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1, &host,
                         0, nullptr);
    target.layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
}

std::uint64_t RendererVk::submit_one_time(VkCommandBuffer commands) {
    vk::check(vkEndCommandBuffer(commands), "vkEndCommandBuffer");
    return vk::Context::get().submit(commands);
}

bool RendererVk::peek_efb(std::uint16_t x, std::uint16_t y, EfbPeekKind kind, std::uint32_t& value) {
    value = 0u;
    if (pointer_depth_fence_ != 0u) {
        throw std::logic_error("GXPeek resources still own an unpublished pointer field");
    }
    if (x >= kEfbWidth || y >= kEfbHeight || !peek_target_) {
        return false;
    }
    vk::check(vkResetCommandBuffer(utility_commands_, 0), "vkResetCommandBuffer");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vk::check(vkBeginCommandBuffer(utility_commands_, &begin), "vkBeginCommandBuffer");
    const EfbPeekSamplingGeometry sampling = compute_efb_peek_sampling_geometry(x, y, efb_scale_);
    record_peek_field(utility_commands_, *peek_target_, kind, make_efb_peek_conversion_constants(sampling, kind), 1u,
                      1u, peek_readback_.buffer->buffer);
    const std::uint64_t fence = submit_one_time(utility_commands_);
    wait_for_fence_value(fence);
    vk::Context::get().invalidate(*peek_readback_.buffer);
    const auto* rgba = reinterpret_cast<const std::uint8_t*>(peek_readback_.buffer->mapped);
    const std::uint32_t rgb = (static_cast<std::uint32_t>(rgba[0]) << 16u) |
        (static_cast<std::uint32_t>(rgba[1]) << 8u) | rgba[2];
    value = kind == EfbPeekKind::Depth ? rgb : (static_cast<std::uint32_t>(rgba[3]) << 24u) | rgb;
    return true;
}

bool RendererVk::capture_pointer_depth(std::span<std::uint32_t> pixels) {
    if (pixels.size() != kEfbWidth * kEfbHeight) {
        throw std::invalid_argument("pointer depth field requires 640x528 pixels");
    }
    const std::uint64_t fence = submit_pointer_depth_capture();
    if (fence == 0u) {
        return false;
    }
    wait_for_fence_value(fence);
    const std::uint8_t* rgba = nullptr;
    if (!try_complete_pointer_depth_capture_in_place(rgba) || rgba == nullptr) {
        return false;
    }
    for (std::size_t i = 0; i < pixels.size(); ++i) {
        pixels[i] = (static_cast<std::uint32_t>(rgba[i * 4u]) << 16u) |
            (static_cast<std::uint32_t>(rgba[i * 4u + 1u]) << 8u) | rgba[i * 4u + 2u];
    }
    return true;
}

std::uint64_t RendererVk::submit_pointer_depth_capture() {
    if (pointer_depth_fence_ != 0u) {
        throw std::logic_error("GXPeek resources still own an unpublished pointer field");
    }
    if (pointer_depth_in_frame_ || !field_target_) {
        return 0u;
    }
    Readback& readback = pointer_depth_ring_[pointer_depth_next_];
    pointer_depth_next_ = (pointer_depth_next_ + 1u) % kPointerDepthRing;
    vk::check(vkResetCommandBuffer(utility_commands_, 0), "vkResetCommandBuffer");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vk::check(vkBeginCommandBuffer(utility_commands_, &begin), "vkBeginCommandBuffer");
    EfbConversionShaderConstants constants =
        make_efb_peek_conversion_constants(compute_efb_peek_sampling_geometry(0u, 0u, efb_scale_), EfbPeekKind::Depth);
    constants.values[15] = -1.0f;  // the whole field
    record_peek_field(utility_commands_, *field_target_, EfbPeekKind::Depth, constants, kEfbWidth, kEfbHeight,
                      readback.buffer->buffer);
    pointer_depth_rgba_ = reinterpret_cast<const std::uint8_t*>(readback.buffer->mapped);
    pointer_depth_readback_ = readback.buffer.get();
    pointer_depth_fence_ = submit_one_time(utility_commands_);
    return pointer_depth_fence_;
}

bool RendererVk::record_pointer_depth_capture_in_frame() {
    if (pointer_depth_in_frame_ || pointer_depth_fence_ != 0u || !field_target_) {
        return false;
    }
    Readback& readback = pointer_depth_ring_[pointer_depth_next_];
    pointer_depth_next_ = (pointer_depth_next_ + 1u) % kPointerDepthRing;
    end_efb_pass();
    EfbConversionShaderConstants constants =
        make_efb_peek_conversion_constants(compute_efb_peek_sampling_geometry(0u, 0u, efb_scale_), EfbPeekKind::Depth);
    constants.values[15] = -1.0f;
    record_peek_field(commands_, *field_target_, EfbPeekKind::Depth, constants, kEfbWidth,
                      kEfbHeight, readback.buffer->buffer);
    pointer_depth_rgba_ = reinterpret_cast<const std::uint8_t*>(readback.buffer->mapped);
    pointer_depth_readback_ = readback.buffer.get();
    reset_gx_bindings();
    bind_efb(nullptr);
    pointer_depth_in_frame_ = true;  // fenced at end_frame
    return true;
}

bool RendererVk::try_complete_pointer_depth_capture_in_place(const std::uint8_t*& rgba) {
    rgba = nullptr;
    if (pointer_depth_fence_ == 0u) {
        if (pointer_depth_in_frame_) {
            return false;  // recorded in the open frame
        }
        throw std::logic_error("pointer depth completion has no owned fence");
    }
    if (completed_fence_value() < pointer_depth_fence_) {
        return false;
    }
    if (pointer_depth_readback_ != nullptr) {
        vk::Context::get().invalidate(*pointer_depth_readback_);
    }
    rgba = pointer_depth_rgba_;
    pointer_depth_fence_ = 0u;
    return true;
}

std::uint64_t RendererVk::submit_async_peek_field(std::size_t channel, EfbPeekKind kind, std::size_t slot) {
    AsyncField& field = async_fields_[channel];
    if (field.fence != 0u || slot >= kAsyncPeekFieldSlots) {
        return 0u;
    }
    ensure_async_field(field);
    vk::check(vkResetCommandBuffer(field.commands, 0), "vkResetCommandBuffer");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vk::check(vkBeginCommandBuffer(field.commands, &begin), "vkBeginCommandBuffer");
    EfbConversionShaderConstants constants =
        make_efb_peek_conversion_constants(compute_efb_peek_sampling_geometry(0u, 0u, efb_scale_), kind);
    constants.values[15] = -1.0f;
    record_peek_field(field.commands, *field.target, kind, constants, kEfbWidth, kEfbHeight,
                      field.readbacks[slot].buffer->buffer);
    field.readbacks[slot].fence = submit_one_time(field.commands);
    field.fence = field.readbacks[slot].fence;
    field.pending_slot = slot;
    return field.fence;
}

bool RendererVk::record_async_peek_field_in_frame(std::size_t channel, EfbPeekKind kind, std::size_t slot) {
    AsyncField& field = async_fields_[channel];
    if (field.fence != 0u || slot >= kAsyncPeekFieldSlots) {
        return false;
    }
    ensure_async_field(field);
    end_efb_pass();
    EfbConversionShaderConstants constants =
        make_efb_peek_conversion_constants(compute_efb_peek_sampling_geometry(0u, 0u, efb_scale_), kind);
    constants.values[15] = -1.0f;
    record_peek_field(commands_, *field.target, kind, constants, kEfbWidth, kEfbHeight,
                      field.readbacks[slot].buffer->buffer);
    reset_gx_bindings();
    bind_efb(nullptr);
    field.fence = kAsyncPeekInFrame;  // fenced at end_frame
    field.pending_slot = slot;
    return true;
}

void RendererVk::ensure_async_field(AsyncField& field) {
    vk::Context& context = vk::Context::get();
    const VkDevice device = context.device();
    if (field.pool == VK_NULL_HANDLE) {
        VkCommandPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pool_info.queueFamilyIndex = context.queue_family();
        vk::check(vkCreateCommandPool(device, &pool_info, nullptr, &field.pool), "vkCreateCommandPool");
        VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        alloc.commandPool = field.pool;
        alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc.commandBufferCount = 1;
        vk::check(vkAllocateCommandBuffers(device, &alloc, &field.commands), "vkAllocateCommandBuffers");
        field.target = context.create_texture(
            kEfbWidth, kEfbHeight, 1u, VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, true,
            "async peek field");
        for (Readback& readback : field.readbacks) {
            readback.buffer = context.create_readback_buffer(kEfbWidth * kEfbHeight * 4u, "async peek field readback");
            if (!readback.buffer) {
                throw std::runtime_error("async peek field readback unavailable");
            }
        }
        if (!field.target) {
            throw std::runtime_error("async peek field target unavailable");
        }
    }
}

bool RendererVk::try_complete_async_peek_field(std::size_t channel) {
    AsyncField& field = async_fields_[channel];
    if (field.fence == 0u || completed_fence_value() < field.fence) {
        return false;
    }
    vk::Context::get().invalidate(*field.readbacks[field.pending_slot].buffer);
    field.fence = 0u;
    return true;
}

const std::uint8_t* RendererVk::async_peek_field_rgba(std::size_t channel, std::size_t slot) const noexcept {
    const AsyncField& field = async_fields_[channel];
    return slot < kAsyncPeekFieldSlots && field.readbacks[slot].buffer
        ? reinterpret_cast<const std::uint8_t*>(field.readbacks[slot].buffer->mapped)
        : nullptr;
}

void RendererVk::set_debug_capture_paths(std::string efb_ppm_path, std::string backbuffer_ppm_path) {
    efb_capture_path_ = std::move(efb_ppm_path);
    backbuffer_capture_path_ = std::move(backbuffer_ppm_path);
}

bool RendererVk::debug_copy_efb_to_readback() {
    debug_log_readback();
    if (efb_capture_path_.empty() || !efb_capture_pending_path_.empty()) {
        return false;
    }
    vk::Context& context = vk::Context::get();
    const unsigned w = kEfbWidth * efb_scale_;
    const unsigned h = kEfbHeight * efb_scale_;
    const std::uint64_t bytes = static_cast<std::uint64_t>(w) * h * 4u;
    if (!efb_capture_.buffer || efb_capture_.buffer->size < bytes) {
        efb_capture_.buffer = context.create_readback_buffer(bytes, "EFB capture");
        if (!efb_capture_.buffer) {
            return false;
        }
    }
    end_efb_pass();
    const VkCommandBuffer commands = commands_;
    image_barrier(commands, efb_.color->image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {w, h, 1};
    vkCmdCopyImageToBuffer(commands, efb_.color->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           efb_capture_.buffer->buffer, 1, &region);
    image_barrier(commands, efb_.color->image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    efb_capture_.fence = 0u;
    efb_capture_pending_path_ = efb_capture_path_;
    efb_capture_width_ = w;
    efb_capture_height_ = h;
    reset_gx_bindings();
    bind_efb(nullptr);
    return true;
}

void RendererVk::debug_log_readback() {
    if (efb_capture_pending_path_.empty() || efb_capture_.fence == 0u ||
        completed_fence_value() < efb_capture_.fence) {
        return;
    }
    vk::Context::get().invalidate(*efb_capture_.buffer);
    const auto* rgba = reinterpret_cast<const std::uint8_t*>(efb_capture_.buffer->mapped);
    std::vector<std::uint8_t> rgb(static_cast<std::size_t>(efb_capture_width_) * efb_capture_height_ * 3u);
    for (std::size_t i = 0, n = rgb.size() / 3u; i < n; ++i) {
        rgb[i * 3u] = rgba[i * 4u];
        rgb[i * 3u + 1u] = rgba[i * 4u + 1u];
        rgb[i * 3u + 2u] = rgba[i * 4u + 2u];
    }
    write_ppm_async(std::move(efb_capture_pending_path_), std::move(rgb), efb_capture_width_, efb_capture_height_);
    efb_capture_pending_path_.clear();
    efb_capture_.fence = 0u;
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

}  // namespace galaxy::gx
