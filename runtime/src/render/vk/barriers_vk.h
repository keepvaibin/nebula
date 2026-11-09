// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// BarriersVk: the barriers one pass records, issued as one vkCmdPipelineBarrier2 where the device enabled
// synchronization2 (Context::pipeline_barrier2), else as one vkCmdPipelineBarrier with the same scopes (copy, resolve, blit
// and clear stages become TRANSFER, sampled and storage accesses SHADER_READ and SHADER_WRITE). E13-BARRIERS-DESIGN.md
// step (c).
//
// Two ways to name a change of an image, both in synchronization2 vocabulary:
//   image(...)        by use (gpu_use.h): the layout, stages and accesses come from the table below, shared with the Direct3D
//                     backends' vocabulary; Contents::Discard is an UNDEFINED old layout.
//   image_scoped(...) by layouts and scopes, for a pass whose layouts a render pass or a library fixes (a depth image
//                     sampled in SHADER_READ_ONLY_OPTIMAL, a swapchain image) or that names a narrower scope.
// A change of an image already in the batch issues the batch first (one image appears once in a call). Changes of other
// images, memory and buffers travel together; undo() issues the way back for the by-use changes.

#include <volk.h>

#include "galaxy/render/gpu_use.h"
#include "galaxy/render/vk/vk_context.h"

#include <cstdint>

namespace galaxy::render::vk {

struct UseScope {
    VkPipelineStageFlags2 stages;
    VkAccessFlags2 access;
    VkImageLayout layout;
};

// `depth`: a depth image, which is sampled in the read-only depth layout (the depth test may stay on).
[[nodiscard]] constexpr UseScope scope_of(gpu::Use use, bool depth) noexcept {
    const VkImageLayout sampled =
        depth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    switch (use) {
    case gpu::Use::ColorWrite:
        return {VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    case gpu::Use::DepthWrite:
        return {VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    case gpu::Use::DepthRead:
        return {VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT |
                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
    case gpu::Use::PixelRead:
        return {VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, sampled};
    case gpu::Use::ComputeRead:
        return {VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, sampled};
    case gpu::Use::ShaderRead:
        return {VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, sampled};
    case gpu::Use::ComputeWrite:
        return {VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL};
    case gpu::Use::CopySource:
        return {VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL};
    case gpu::Use::CopyDest:
        return {VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL};
    case gpu::Use::ResolveSource:
        return {VK_PIPELINE_STAGE_2_RESOLVE_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL};
    case gpu::Use::ResolveDest:
        return {VK_PIPELINE_STAGE_2_RESOLVE_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL};
    case gpu::Use::Present:
        return {VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR};
    case gpu::Use::Common:
        return {VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED};
    }
    return {VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
            VK_IMAGE_LAYOUT_GENERAL};
}

// Version-1 equivalents of synchronization2 scopes (the stages and accesses it splits finer).
[[nodiscard]] constexpr VkPipelineStageFlags stages_v1(VkPipelineStageFlags2 stages, bool source) noexcept {
    VkPipelineStageFlags out = static_cast<VkPipelineStageFlags>(stages & 0x7FFFFFFFull);
    if ((stages & (VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_RESOLVE_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT |
                   VK_PIPELINE_STAGE_2_CLEAR_BIT)) != 0) {
        out |= VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    if (out == 0) {
        out = source ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    }
    return out;
}

[[nodiscard]] constexpr VkAccessFlags access_v1(VkAccessFlags2 access) noexcept {
    VkAccessFlags out = static_cast<VkAccessFlags>(access & 0x7FFFFFFFull);
    if ((access & (VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT)) != 0) {
        out |= VK_ACCESS_SHADER_READ_BIT;
    }
    if ((access & VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT) != 0) {
        out |= VK_ACCESS_SHADER_WRITE_BIT;
    }
    return out;
}

class BarriersVk {
public:
    BarriersVk(VkCommandBuffer commands, PFN_vkCmdPipelineBarrier2 barrier2) noexcept
        : commands_(commands), barrier2_(barrier2) {}
    // With the device's own choice (Context::pipeline_barrier2).
    explicit BarriersVk(VkCommandBuffer commands) noexcept : BarriersVk(commands, Context::get().pipeline_barrier2()) {}

    // A change of `image` from one use to another; `Discard` drops the old contents (an UNDEFINED old layout, no
    // access to wait on, only the old use's stages). Nothing when the uses are the same and the contents are kept.
    void image(VkImage image, VkImageAspectFlags aspect, gpu::Use before, gpu::Use after,
               gpu::Contents contents = gpu::Contents::Keep, std::uint32_t levels = VK_REMAINING_MIP_LEVELS) noexcept {
        if (image == VK_NULL_HANDLE || (before == after && contents == gpu::Contents::Keep)) {
            return;
        }
        const bool depth = (aspect & VK_IMAGE_ASPECT_DEPTH_BIT) != 0;
        const UseScope from = scope_of(before, depth), to = scope_of(after, depth);
        const bool discard = contents == gpu::Contents::Discard;
        Change& change = add(image, aspect, levels);
        change.use = true;
        change.before = before;
        change.after = after;
        VkImageMemoryBarrier2& b = images_[count_ - 1u];
        b.srcStageMask = from.stages;
        // Only writes need making available: a read before a write needs the execution dependency alone.
        b.srcAccessMask = discard ? VK_ACCESS_2_NONE : (from.access & kWrites);
        b.dstStageMask = to.stages;
        b.dstAccessMask = to.access;
        b.oldLayout = discard ? VK_IMAGE_LAYOUT_UNDEFINED : from.layout;
        b.newLayout = to.layout;
    }

    // A change of `image` between layouts with the scopes the caller names (synchronization2 flags; the version-1 names
    // below 2^31 have the same values). Not undone by undo().
    void image_scoped(VkImage image, VkImageAspectFlags aspect, VkImageLayout from, VkImageLayout to,
                      VkPipelineStageFlags2 src_stages, VkAccessFlags2 src_access, VkPipelineStageFlags2 dst_stages,
                      VkAccessFlags2 dst_access, std::uint32_t levels = VK_REMAINING_MIP_LEVELS) noexcept {
        if (image == VK_NULL_HANDLE) {
            return;
        }
        add(image, aspect, levels);
        VkImageMemoryBarrier2& b = images_[count_ - 1u];
        b.srcStageMask = src_stages;
        b.srcAccessMask = src_access;
        b.dstStageMask = dst_stages;
        b.dstAccessMask = dst_access;
        b.oldLayout = from;
        b.newLayout = to;
    }

    // A queue family ownership transfer of `image` with the layout change: recorded once on the source family's queue (the
    // release: the writes made available, no destination scope) and once, the same, on the destination family's (the
    // acquire: no source scope, the reads that follow).
    void image_transfer(VkImage image, VkImageAspectFlags aspect, VkImageLayout from, VkImageLayout to, std::uint32_t src_family,
                        std::uint32_t dst_family, VkPipelineStageFlags2 src_stages, VkAccessFlags2 src_access,
                        VkPipelineStageFlags2 dst_stages, VkAccessFlags2 dst_access,
                        std::uint32_t levels = VK_REMAINING_MIP_LEVELS) noexcept {
        if (image == VK_NULL_HANDLE) {
            return;
        }
        image_scoped(image, aspect, from, to, src_stages, src_access, dst_stages, dst_access, levels);
        VkImageMemoryBarrier2& b = images_[count_ - 1u];
        b.srcQueueFamilyIndex = src_family;
        b.dstQueueFamilyIndex = dst_family;
    }

    // A memory dependency between all resources (one per call; later ones widen it).
    void memory(VkPipelineStageFlags2 src_stages, VkAccessFlags2 src_access, VkPipelineStageFlags2 dst_stages,
                VkAccessFlags2 dst_access) noexcept {
        if (!memory_used_) {
            memory_ = VkMemoryBarrier2{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
            memory_used_ = true;
        }
        memory_.srcStageMask |= src_stages;
        memory_.srcAccessMask |= src_access;
        memory_.dstStageMask |= dst_stages;
        memory_.dstAccessMask |= dst_access;
    }

    void buffer(VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size, VkPipelineStageFlags2 src_stages,
                VkAccessFlags2 src_access, VkPipelineStageFlags2 dst_stages, VkAccessFlags2 dst_access) noexcept {
        if (buffer == VK_NULL_HANDLE) {
            return;
        }
        if (buffer_count_ == kMaxBuffers) {
            flush();
        }
        VkBufferMemoryBarrier2& b = buffers_[buffer_count_++];
        b = VkBufferMemoryBarrier2{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
        b.srcStageMask = src_stages;
        b.srcAccessMask = src_access;
        b.dstStageMask = dst_stages;
        b.dstAccessMask = dst_access;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.buffer = buffer;
        b.offset = offset;
        b.size = size;
    }

    [[nodiscard]] bool empty() const noexcept { return count_ == 0u && buffer_count_ == 0u && !memory_used_; }

    // Issues the recorded changes (they stay recorded for undo()).
    void issue() const noexcept {
        if (empty()) {
            return;
        }
        if (barrier2_ != nullptr) {
            VkDependencyInfo info{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            info.memoryBarrierCount = memory_used_ ? 1u : 0u;
            info.pMemoryBarriers = &memory_;
            info.bufferMemoryBarrierCount = buffer_count_;
            info.pBufferMemoryBarriers = buffers_;
            info.imageMemoryBarrierCount = count_;
            info.pImageMemoryBarriers = images_;
            barrier2_(commands_, &info);
            return;
        }
        VkMemoryBarrier memory{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        VkBufferMemoryBarrier buffers[kMaxBuffers];
        VkImageMemoryBarrier images[kMax];
        VkPipelineStageFlags2 src = 0, dst = 0;
        if (memory_used_) {
            memory.srcAccessMask = access_v1(memory_.srcAccessMask);
            memory.dstAccessMask = access_v1(memory_.dstAccessMask);
            src |= memory_.srcStageMask;
            dst |= memory_.dstStageMask;
        }
        for (std::uint32_t i = 0; i < buffer_count_; ++i) {
            const VkBufferMemoryBarrier2& b = buffers_[i];
            buffers[i] = VkBufferMemoryBarrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
            buffers[i].srcAccessMask = access_v1(b.srcAccessMask);
            buffers[i].dstAccessMask = access_v1(b.dstAccessMask);
            buffers[i].srcQueueFamilyIndex = buffers[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            buffers[i].buffer = b.buffer;
            buffers[i].offset = b.offset;
            buffers[i].size = b.size;
            src |= b.srcStageMask;
            dst |= b.dstStageMask;
        }
        for (std::uint32_t i = 0; i < count_; ++i) {
            const VkImageMemoryBarrier2& b = images_[i];
            images[i] = VkImageMemoryBarrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            images[i].srcAccessMask = access_v1(b.srcAccessMask);
            images[i].dstAccessMask = access_v1(b.dstAccessMask);
            images[i].oldLayout = b.oldLayout;
            images[i].newLayout = b.newLayout;
            images[i].srcQueueFamilyIndex = b.srcQueueFamilyIndex;
            images[i].dstQueueFamilyIndex = b.dstQueueFamilyIndex;
            images[i].image = b.image;
            images[i].subresourceRange = b.subresourceRange;
            src |= b.srcStageMask;
            dst |= b.dstStageMask;
        }
        vkCmdPipelineBarrier(commands_, stages_v1(src, true), stages_v1(dst, false), 0, memory_used_ ? 1u : 0u, &memory,
                             buffer_count_, buffers, count_, images);
    }

    // Issues the way back (every by-use change of the last batch reversed, contents kept) and forgets everything.
    void undo() noexcept {
        Change back[kMax];
        const std::uint32_t n = count_;
        for (std::uint32_t i = 0; i < n; ++i) {
            back[i] = changes_[i];
        }
        clear();
        for (std::uint32_t i = 0; i < n; ++i) {
            if (back[i].use) {
                image(back[i].target, back[i].aspect, back[i].after, back[i].before, gpu::Contents::Keep, back[i].levels);
            }
        }
        issue();
        clear();
    }

    // Issues what is recorded and starts a new batch.
    void flush() noexcept {
        issue();
        clear();
    }
    void clear() noexcept {
        count_ = 0u;
        buffer_count_ = 0u;
        memory_used_ = false;
    }

private:
    static constexpr VkAccessFlags2 kWrites =
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    static constexpr std::uint32_t kMax = 8u;
    static constexpr std::uint32_t kMaxBuffers = 4u;
    struct Change {
        VkImage target = VK_NULL_HANDLE;
        VkImageAspectFlags aspect = 0;
        std::uint32_t levels = 0;
        gpu::Use before = gpu::Use::ColorWrite, after = gpu::Use::ColorWrite;
        bool use = false;
    };

    // A new image barrier at index count_ - 1 (the batch issued first when full or when the image is in it already).
    Change& add(VkImage image, VkImageAspectFlags aspect, std::uint32_t levels) noexcept {
        bool again = count_ == kMax;
        for (std::uint32_t i = 0; i < count_ && !again; ++i) {
            again = images_[i].image == image;
        }
        if (again) {
            flush();
        }
        Change& change = changes_[count_];
        change = Change{image, aspect, levels};
        VkImageMemoryBarrier2& b = images_[count_++];
        b = VkImageMemoryBarrier2{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = image;
        b.subresourceRange = {aspect, 0u, levels, 0u, 1u};
        return change;
    }

    VkCommandBuffer commands_;
    PFN_vkCmdPipelineBarrier2 barrier2_;
    VkImageMemoryBarrier2 images_[kMax];
    Change changes_[kMax];
    VkBufferMemoryBarrier2 buffers_[kMaxBuffers];
    VkMemoryBarrier2 memory_{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    std::uint32_t count_ = 0u;
    std::uint32_t buffer_count_ = 0u;
    bool memory_used_ = false;
};

// One image's change, issued at once: by layouts and scopes (image_scoped), or by use (image).
inline void image_barrier(VkCommandBuffer commands, VkImage image, VkImageAspectFlags aspect, VkImageLayout from,
                          VkImageLayout to, VkPipelineStageFlags2 src_stages, VkAccessFlags2 src_access,
                          VkPipelineStageFlags2 dst_stages, VkAccessFlags2 dst_access,
                          std::uint32_t levels = VK_REMAINING_MIP_LEVELS) noexcept {
    BarriersVk barriers(commands);
    barriers.image_scoped(image, aspect, from, to, src_stages, src_access, dst_stages, dst_access, levels);
    barriers.issue();
}

inline void image_barrier(VkCommandBuffer commands, VkImage image, VkImageAspectFlags aspect, gpu::Use before,
                          gpu::Use after, gpu::Contents contents = gpu::Contents::Keep,
                          std::uint32_t levels = VK_REMAINING_MIP_LEVELS) noexcept {
    BarriersVk barriers(commands);
    barriers.image(image, aspect, before, after, contents, levels);
    barriers.issue();
}

// A pass's shader writes before the next pass's shader reads.
inline void shader_written(VkCommandBuffer commands, VkPipelineStageFlags2 reader_stages) noexcept {
    BarriersVk barriers(commands);
    barriers.memory(VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, reader_stages,
                    VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    barriers.issue();
}

}  // namespace galaxy::render::vk
