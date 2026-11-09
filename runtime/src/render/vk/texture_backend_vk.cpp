// SPDX-License-Identifier: GPL-3.0-only
// TextureStore's Vulkan side (gpu_texture_backend.h).

#include "galaxy/render/gpu_texture_backend.h"

#include "barriers_vk.h"

#include <algorithm>
#include <stdexcept>

namespace galaxy::render {

namespace {

// The texture from its tracked layout to `to`, over all its levels.
void transition(VkCommandBuffer commands, vk::Texture& texture, VkImageLayout to, VkPipelineStageFlags2 src_stage,
                VkAccessFlags2 src_access, VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access) {
    vk::image_barrier(commands, texture.image, VK_IMAGE_ASPECT_COLOR_BIT, texture.layout, to, src_stage, src_access,
                      dst_stage, dst_access, texture.levels);
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
    mirror_ = VK_NULL_HANDLE;
    mirror_count_ = 0;
    mirror_grow_ = nullptr;
    mirror_owner_ = nullptr;
    null_texture_.reset();
    upload_list_ = VK_NULL_HANDLE;
}

void TextureBackend::prepare_null_view(VkCommandBuffer commands) {
    if (!null_texture_ || null_texture_->layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        return;
    }
    transition(commands, *null_texture_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
               VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    const VkClearColorValue black{};
    const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdClearColorImage(commands, null_texture_->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1, &range);
    transition(commands, *null_texture_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_CLEAR_BIT,
               VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
}

void TextureBackend::write_view(std::uint32_t index, vk::Texture* texture, unsigned) {
    ensure_view(index);
    views_[index] = texture->view;
    publish_view(index);
}

std::uint32_t TextureBackend::live_views() const noexcept {
    for (std::size_t i = views_.size(); i > 0; --i) {
        if (views_[i - 1] != VK_NULL_HANDLE && (null_texture_ == nullptr || views_[i - 1] != null_texture_->view)) {
            return static_cast<std::uint32_t>(i);
        }
    }
    return 0u;
}

void TextureBackend::mirror_into(VkDescriptorSet images, std::uint32_t count, GrowFn grow, void* owner) {
    mirror_ = images;
    mirror_count_ = count;
    mirror_grow_ = grow;
    mirror_owner_ = owner;
    // The initial vector's last entry is the null view, whose slot the renderer writes.
    for (std::uint32_t i = 0; i < views_.size(); ++i) {
        if (views_[i] != VK_NULL_HANDLE && views_[i] != null_texture_->view) {
            publish_view(i);
        }
    }
}

void TextureBackend::publish_view(std::uint32_t index) {
    if (mirror_ == VK_NULL_HANDLE || views_[index] == VK_NULL_HANDLE) {
        return;
    }
    if (index >= mirror_count_) {
        // A larger set that already holds the earlier views (move_mirror); this one is written below.
        if (mirror_grow_ != nullptr) {
            mirror_grow_(mirror_owner_, index);
        }
        if (index >= mirror_count_) {
            throw std::runtime_error("[textures] more texture views than the device's image array holds");
        }
    }
    const VkDescriptorImageInfo image{VK_NULL_HANDLE, views_[index], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = mirror_;
    write.dstBinding = 0;
    write.dstArrayElement = index;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    write.pImageInfo = &image;
    vkUpdateDescriptorSets(vk::Context::get().device(), 1, &write, 0, nullptr);
}

void TextureBackend::release_view(std::uint32_t index) {
    if (index >= views_.size()) {
        return;
    }
    views_[index] = VK_NULL_HANDLE;
    if (mirror_ == VK_NULL_HANDLE || index >= mirror_count_ || null_texture_ == nullptr) {
        return;
    }
    const VkDescriptorImageInfo image{VK_NULL_HANDLE, null_texture_->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = mirror_;
    write.dstBinding = 0;
    write.dstArrayElement = index;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    write.pImageInfo = &image;
    vkUpdateDescriptorSets(vk::Context::get().device(), 1, &write, 0, nullptr);
}

TextureBackend::UploadChunk TextureBackend::create_upload_chunk(std::uint64_t size) {
    UploadChunk chunk{};
    chunk.resource = vk::Context::get().create_buffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, "texture upload arena");
    if (!chunk.resource) {
        throw std::runtime_error("[TextureStore] create texture upload arena failed");
    }
    chunk.cpu_base = reinterpret_cast<std::uint8_t*>(chunk.resource->mapped);
    chunk.size = size;
    return chunk;
}

void TextureBackend::release_upload_chunk(UploadChunk& chunk) noexcept {
    chunk.cpu_base = nullptr;
    chunk.resource.reset();
}

bool TextureBackend::supports_bc1() {
    return vk::Context::get().bc_textures();
}

gpu::TextureRef TextureBackend::create_texture(
    std::uint32_t width, std::uint32_t height, unsigned levels, std::uint64_t& allocation_bytes, TexelFormat format) {
    const VkFormat vk_format = format == TexelFormat::R8    ? VK_FORMAT_R8_UNORM
                               : format == TexelFormat::Rg8 ? VK_FORMAT_R8G8_UNORM
                               : format == TexelFormat::Bc1 ? VK_FORMAT_BC1_RGBA_UNORM_BLOCK
                                                            : VK_FORMAT_R8G8B8A8_UNORM;
    auto texture = vk::Context::get().create_texture(
        width, height, levels, vk_format, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, false, nullptr);
    if (!texture) {
        throw std::runtime_error("[TextureStore] create default texture failed");
    }
    if (format == TexelFormat::R8 || format == TexelFormat::Rg8) {
        // The value in every colour channel, alpha the value or the second byte.
        const VkDevice device = vk::Context::get().device();
        VkImageViewCreateInfo view_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view_info.image = texture->image;
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = vk_format;
        view_info.components = {VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_R,
                                format == TexelFormat::R8 ? VK_COMPONENT_SWIZZLE_R : VK_COMPONENT_SWIZZLE_G};
        view_info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, levels, 0, 1};
        VkImageView view = VK_NULL_HANDLE;
        if (vkCreateImageView(device, &view_info, nullptr, &view) != VK_SUCCESS) {
            throw std::runtime_error("[TextureStore] create texture view failed");
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

void TextureBackend::record_upload(vk::Texture* texture, bool reused, const UploadChunk& staging,
                                   std::uint64_t offset, std::uint32_t width, std::uint32_t height, unsigned levels,
                                   const std::uint64_t* offsets, const std::uint32_t* pitches) {
    // A reused pooled texture was last sampled; a new one has no contents to keep.
    if (!reused) {
        texture->layout = VK_IMAGE_LAYOUT_UNDEFINED;
    }
    transition(upload_list_, *texture, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
               reused ? VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
               VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    std::vector<VkBufferImageCopy> regions(levels);
    const unsigned count = levels;
    for (unsigned l = 0; l < count; ++l) {
        VkBufferImageCopy& region = regions[l];
        region = {};
        region.bufferOffset = offset + offsets[l];
        // In texels: a block format's rows hold 4 texels per 8 bytes.
        region.bufferRowLength = texture->format == VK_FORMAT_R8_UNORM          ? pitches[l]
                                 : texture->format == VK_FORMAT_R8G8_UNORM      ? pitches[l] / 2u
                                 : texture->format == VK_FORMAT_BC1_RGBA_UNORM_BLOCK ? pitches[l] / 2u
                                                                                : pitches[l] / 4u;
        region.bufferImageHeight = 0;
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, l, 0, 1};
        region.imageExtent = {std::max<std::uint32_t>(1u, width >> l), std::max<std::uint32_t>(1u, height >> l), 1};
    }
    vkCmdCopyBufferToImage(upload_list_, staging.resource->buffer, texture->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           count, regions.data());
    transition(upload_list_, *texture, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
               VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
}

}  // namespace galaxy::render
