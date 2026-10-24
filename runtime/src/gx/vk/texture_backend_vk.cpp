// SPDX-License-Identifier: GPL-3.0-only
// TextureCache's Vulkan side (texture_backend.h).

#include "galaxy/gx/texture_backend.h"

#include <algorithm>
#include <stdexcept>

namespace galaxy::gx {

namespace {

void transition(VkCommandBuffer commands, vk::Texture& texture, VkImageLayout to, VkPipelineStageFlags src_stage,
                VkAccessFlags src_access, VkPipelineStageFlags dst_stage, VkAccessFlags dst_access) {
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.srcAccessMask = src_access;
    barrier.dstAccessMask = dst_access;
    barrier.oldLayout = texture.layout;
    barrier.newLayout = to;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = texture.image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, texture.levels, 0, 1};
    vkCmdPipelineBarrier(commands, src_stage, dst_stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    texture.layout = to;
}

}  // namespace

bool TextureBackend::initialize(gpu::Device*, std::uint32_t view_capacity) {
    // The null view: one transparent black texel, sampled where a draw binds no map.
    null_texture_ = vk::Context::get().create_texture(
        1u, 1u, 1u, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, false,
        "null texture");
    if (!null_texture_) {
        return false;
    }
    views_.assign(view_capacity + 1u, VK_NULL_HANDLE);
    views_.back() = null_texture_->view;
    return true;
}

void TextureBackend::shutdown() {
    views_.clear();
    null_texture_.reset();
    upload_list_ = VK_NULL_HANDLE;
}

void TextureBackend::prepare_null_view(VkCommandBuffer commands) {
    if (!null_texture_ || null_texture_->layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        return;
    }
    transition(commands, *null_texture_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
               VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
    const VkClearColorValue black{};
    const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdClearColorImage(commands, null_texture_->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1, &range);
    transition(commands, *null_texture_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT,
               VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
}

void TextureBackend::write_view(std::uint32_t index, vk::Texture* texture, unsigned) {
    views_[index] = texture->view;
}

TextureBackend::UploadChunk TextureBackend::create_upload_chunk(std::uint64_t size) {
    UploadChunk chunk{};
    chunk.resource = vk::Context::get().create_buffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, "texture upload arena");
    if (!chunk.resource) {
        throw std::runtime_error("[TextureCache] create texture upload arena failed");
    }
    chunk.cpu_base = reinterpret_cast<std::uint8_t*>(chunk.resource->mapped);
    chunk.size = size;
    return chunk;
}

void TextureBackend::release_upload_chunk(UploadChunk& chunk) noexcept {
    chunk.cpu_base = nullptr;
    chunk.resource.reset();
}

gpu::TextureRef TextureBackend::create_texture(
    std::uint32_t width, std::uint32_t height, unsigned levels, std::uint64_t& allocation_bytes, bool grey) {
    auto texture = vk::Context::get().create_texture(
        width, height, levels, grey ? VK_FORMAT_R8_UNORM : VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, false, nullptr);
    if (!texture) {
        throw std::runtime_error("[TextureCache] create default texture failed");
    }
    if (grey) {
        // The value in every channel, as GX expands an I8 texture.
        const VkDevice device = vk::Context::get().device();
        VkImageViewCreateInfo view_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view_info.image = texture->image;
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = VK_FORMAT_R8_UNORM;
        view_info.components = {VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_R,
                                VK_COMPONENT_SWIZZLE_R};
        view_info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, levels, 0, 1};
        VkImageView view = VK_NULL_HANDLE;
        if (vkCreateImageView(device, &view_info, nullptr, &view) != VK_SUCCESS) {
            throw std::runtime_error("[TextureCache] create grey texture view failed");
        }
        vkDestroyImageView(device, texture->view, nullptr);
        texture->view = view;
    }
    allocation_bytes = texture->allocation_bytes;
    return texture;
}

bool TextureBackend::poolable(vk::Texture* texture, std::uint32_t& width, std::uint32_t& height,
                              std::uint32_t& levels) {
    if (texture->format != VK_FORMAT_R8G8B8A8_UNORM || texture->target_view != VK_NULL_HANDLE) {
        return false;
    }
    width = texture->width;
    height = texture->height;
    levels = texture->levels;
    return true;
}

bool TextureBackend::alias_compatible(vk::Texture* texture, std::uint32_t& width, std::uint32_t& height) {
    if (texture->format != VK_FORMAT_R8G8B8A8_UNORM || texture->width == 0u || texture->width > UINT16_MAX ||
        texture->height == 0u || texture->height > UINT16_MAX) {
        return false;
    }
    width = texture->width;
    height = texture->height;
    return true;
}

void TextureBackend::record_upload(vk::Texture* texture, bool reused, const UploadChunk& staging,
                                   std::uint64_t offset, std::uint32_t width, std::uint32_t height, unsigned levels,
                                   const std::uint64_t* offsets, const std::uint32_t* pitches) {
    // A reused pooled texture was last sampled; a new one has no contents to keep.
    if (!reused) {
        texture->layout = VK_IMAGE_LAYOUT_UNDEFINED;
    }
    transition(upload_list_, *texture, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
               reused ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
               VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
    VkBufferImageCopy regions[16];
    const unsigned count = std::min(levels, 16u);
    for (unsigned l = 0; l < count; ++l) {
        VkBufferImageCopy& region = regions[l];
        region = {};
        region.bufferOffset = offset + offsets[l];
        region.bufferRowLength = pitches[l] / (texture->format == VK_FORMAT_R8_UNORM ? 1u : 4u);  // in texels
        region.bufferImageHeight = 0;
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, l, 0, 1};
        region.imageExtent = {std::max<std::uint32_t>(1u, width >> l), std::max<std::uint32_t>(1u, height >> l), 1};
    }
    vkCmdCopyBufferToImage(upload_list_, staging.resource->buffer, texture->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           count, regions);
    transition(upload_list_, *texture, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT,
               VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
}

}  // namespace galaxy::gx
