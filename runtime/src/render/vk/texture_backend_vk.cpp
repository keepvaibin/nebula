// SPDX-License-Identifier: GPL-3.0-only
// TextureStore's Vulkan side (gpu_texture_backend.h).

#include "galaxy/render/gpu_texture_backend.h"

#include "barriers_vk.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <stdexcept>
#include <utility>

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

namespace {

VkFormat vulkan_format_of(TexelFormat format) noexcept {
    return format == TexelFormat::R8    ? VK_FORMAT_R8_UNORM
           : format == TexelFormat::Rg8 ? VK_FORMAT_R8G8_UNORM
           : format == TexelFormat::Bc1 ? VK_FORMAT_BC1_RGBA_UNORM_BLOCK
           : format == TexelFormat::Astc4x4 ? VK_FORMAT_ASTC_4x4_UNORM_BLOCK
           : format == TexelFormat::Rgb565 ? VK_FORMAT_R5G6B5_UNORM_PACK16
                                        : VK_FORMAT_R8G8B8A8_UNORM;
}

// Whether images of `format` can be made for host copies, and the device reads them as fast as its own layout (the extension's
// device performance query): both asked of the device, not assumed.
bool host_copy_format(VkFormat format) {
    VkHostImageCopyDevicePerformanceQueryEXT performance{VK_STRUCTURE_TYPE_HOST_IMAGE_COPY_DEVICE_PERFORMANCE_QUERY_EXT};
    VkImageFormatProperties2 properties{VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2};
    properties.pNext = &performance;
    VkPhysicalDeviceImageFormatInfo2 info{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2};
    info.format = format;
    info.type = VK_IMAGE_TYPE_2D;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_HOST_TRANSFER_BIT_EXT;
    return vkGetPhysicalDeviceImageFormatProperties2(vk::Context::get().physical_device(), &info, &properties) == VK_SUCCESS &&
           performance.optimalDeviceAccess == VK_TRUE;
}

// The copy regions of a texture's levels from a staging buffer (rows in texels: a block format's hold 4 texels per 8 bytes).
std::vector<VkBufferImageCopy> copy_regions(const vk::Texture& texture, std::uint64_t offset, std::uint32_t width,
                                            std::uint32_t height, unsigned levels, const std::uint64_t* offsets,
                                            const std::uint32_t* pitches) {
    std::vector<VkBufferImageCopy> regions(levels);
    for (unsigned l = 0; l < levels; ++l) {
        VkBufferImageCopy& region = regions[l];
        region = {};
        region.bufferOffset = offset + offsets[l];
        region.bufferRowLength = texture.format == VK_FORMAT_R8_UNORM          ? pitches[l]
                                 : texture.format == VK_FORMAT_R8G8_UNORM || texture.format == VK_FORMAT_R5G6B5_UNORM_PACK16
                                     ? pitches[l] / 2u
                                 : texture.format == VK_FORMAT_BC1_RGBA_UNORM_BLOCK ? pitches[l] / 2u
                                                                                : pitches[l] / 4u;
        region.bufferImageHeight = 0;
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, l, 0, 1};
        region.imageExtent = {std::max<std::uint32_t>(1u, width >> l), std::max<std::uint32_t>(1u, height >> l), 1};
    }
    return regions;
}

// What a draw does with a texture the transfer queue wrote (fragment shading, or a copy into it again).
constexpr VkPipelineStageFlags2 kAcquireStages = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COPY_BIT;
constexpr VkAccessFlags2 kAcquireAccess = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT;

}  // namespace

bool TextureBackend::initialize(gpu::Device*, std::uint32_t view_capacity) {
    host_formats_ = 0u;
    if (vk::Context::get().host_image_copy()) {
        for (const TexelFormat format : {TexelFormat::Rgba8, TexelFormat::R8, TexelFormat::Rg8, TexelFormat::Bc1, TexelFormat::Astc4x4,
                                         TexelFormat::Rgb565}) {
            if ((format != TexelFormat::Bc1 || supports_bc1()) && (format != TexelFormat::Astc4x4 || supports_astc4x4()) &&
                (format != TexelFormat::Rgb565 || supports_rgb565()) &&
                host_copy_format(vulkan_format_of(format))) {
                host_formats_ |= 1u << static_cast<unsigned>(format);
            }
        }
    }
    {
        // The path, once (again only if a new device chooses another).
        static std::atomic<int> logged{-1};
        const int path = host_formats_ != 0u ? 1 : 0;
        if (logged.exchange(path) != path) {
            std::fprintf(stderr, "[textures] uploads: %s\n",
                         path != 0 ? "direct host copies (VK_EXT_host_image_copy, no copy command or staging buffer read by the GPU)"
                                   : "staged copies");
        }
    }
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
    drain();
    destroy_async();
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

bool TextureBackend::supports_astc4x4() {
    return vk::Context::get().astc_textures();
}

bool TextureBackend::supports_rgb565() {
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(vk::Context::get().physical_device(), VK_FORMAT_R5G6B5_UNORM_PACK16, &properties);
    constexpr VkFormatFeatureFlags kNeeded = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                                             VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    return (properties.optimalTilingFeatures & kNeeded) == kNeeded;
}

gpu::TextureRef TextureBackend::create_texture(
    std::uint32_t width, std::uint32_t height, unsigned levels, std::uint64_t& allocation_bytes, TexelFormat format,
    bool writable) {
    const VkFormat vk_format = vulkan_format_of(format);
    const bool host = writable && (host_formats_ & (1u << static_cast<unsigned>(format))) != 0u;
    auto texture = vk::Context::get().create_texture(
        width, height, levels, vk_format,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | (host ? VK_IMAGE_USAGE_HOST_TRANSFER_BIT_EXT : 0u), false,
        nullptr);
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
    if (!async_slots_.empty() && !host) {
        async_.insert_or_assign(texture.get(), Async{});  // a stale entry of a freed image at this address goes
    }
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
    if (direct_written(texture)) {
        // Host copies: the layout change and the levels are done by the host, in the staged rows; nothing is recorded. A new
        // image no frame reads yet.
        const VkDevice device = vk::Context::get().device();
        VkHostImageLayoutTransitionInfoEXT transition{VK_STRUCTURE_TYPE_HOST_IMAGE_LAYOUT_TRANSITION_INFO_EXT};
        transition.image = texture->image;
        transition.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        transition.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        transition.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, texture->levels, 0, 1};
        vk::check(vkTransitionImageLayoutEXT(device, 1, &transition), "host image layout transition");
        std::vector<VkMemoryToImageCopyEXT> regions(levels);
        for (unsigned l = 0; l < levels; ++l) {
            VkMemoryToImageCopyEXT& region = regions[l];
            region = {VK_STRUCTURE_TYPE_MEMORY_TO_IMAGE_COPY_EXT};
            region.pHostPointer = staging.cpu_base + offset + offsets[l];
            // In texels, as the copy path's bufferRowLength.
            region.memoryRowLength = texture->format == VK_FORMAT_R8_UNORM          ? pitches[l]
                                     : texture->format == VK_FORMAT_R8G8_UNORM || texture->format == VK_FORMAT_R5G6B5_UNORM_PACK16
                                         ? pitches[l] / 2u
                                     : texture->format == VK_FORMAT_BC1_RGBA_UNORM_BLOCK ? pitches[l] / 2u
                                                                                    : pitches[l] / 4u;
            region.memoryImageHeight = 0;
            region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, l, 0, 1};
            region.imageExtent = {std::max<std::uint32_t>(1u, width >> l), std::max<std::uint32_t>(1u, height >> l), 1};
        }
        VkCopyMemoryToImageInfoEXT copy{VK_STRUCTURE_TYPE_COPY_MEMORY_TO_IMAGE_INFO_EXT};
        copy.dstImage = texture->image;
        copy.dstImageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        copy.regionCount = levels;
        copy.pRegions = regions.data();
        vk::check(vkCopyMemoryToImageEXT(device, &copy), "host image copy");
        texture->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        return;
    }
    if (!async_slots_.empty()) {
        // A texture the graphics queue has not acquired yet is written by the transfer queue (new, or rewritten before any
        // frame named it); one rewritten after a frame wrote it for the first time is acquired in this frame's upload list
        // first and then copied into here, on the graphics queue, as an image that rests sampled.
        if (const auto found = async_.find(texture); found != async_.end()) {
            if (!reused || found->second.value == 0u) {
                record_async(texture, found->second, staging, offset, width, height, levels, offsets, pitches);
                return;
            }
            acquire_now(texture);
        }
    }
    transition(upload_list_, *texture, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
               reused ? VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
               VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    const std::vector<VkBufferImageCopy> regions = copy_regions(*texture, offset, width, height, levels, offsets, pitches);
    const unsigned count = levels;
    vkCmdCopyBufferToImage(upload_list_, staging.resource->buffer, texture->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           count, regions.data());
    transition(upload_list_, *texture, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
               VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
}

bool TextureBackend::enable_async_uploads(unsigned frames_in_flight, unsigned frame_slot) {
    destroy_async();
    vk::Context& context = vk::Context::get();
    if (frames_in_flight == 0u || context.transfer_queue() == VK_NULL_HANDLE) {
        return false;
    }
    const VkDevice device = context.device();
    async_slots_.assign(frames_in_flight, AsyncSlot{});
    for (AsyncSlot& slot : async_slots_) {
        VkCommandPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        pool_info.queueFamilyIndex = context.transfer_family();
        VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocate.commandBufferCount = 1u;
        if (vkCreateCommandPool(device, &pool_info, nullptr, &slot.pool) != VK_SUCCESS) {
            destroy_async();
            return false;
        }
        allocate.commandPool = slot.pool;
        if (vkAllocateCommandBuffers(device, &allocate, &slot.commands) != VK_SUCCESS) {
            destroy_async();
            return false;
        }
    }
    async_slot_ = frame_slot < frames_in_flight ? frame_slot : 0u;
    std::fprintf(stderr, "[textures] uploads: transfer queue (family %u; images are written beside the frame and acquired by its first use)\n",
                 context.transfer_family());
    return true;
}

void TextureBackend::destroy_async() noexcept {
    const VkDevice device = vk::Context::get().device();
    if (device != VK_NULL_HANDLE) {
        for (AsyncSlot& slot : async_slots_) {
            if (slot.pool != VK_NULL_HANDLE) {
                vkDestroyCommandPool(device, slot.pool, nullptr);  // frees its buffer
            }
        }
    }
    async_slots_.clear();
    async_.clear();
    named_.clear();
    transfer_open_ = false;
    forced_wait_ = 0u;
}

void TextureBackend::drain() noexcept {
    if (async_slots_.empty()) {
        return;
    }
    vk::Context& context = vk::Context::get();
    if (context.device() != VK_NULL_HANDLE) {
        static_cast<void>(context.wait_transfer(context.transfer_submitted(), 10'000'000'000ull));
    }
}

void TextureBackend::begin_frame(unsigned frame_slot) {
    if (async_slots_.empty() || frame_slot >= async_slots_.size()) {
        return;
    }
    async_slot_ = frame_slot;
    AsyncSlot& slot = async_slots_[frame_slot];
    vk::Context& context = vk::Context::get();
    // What this slot's last frame sent the transfer queue is done before its staging is written again.
    if (!context.wait_transfer(slot.value, 10'000'000'000ull)) {
        throw std::runtime_error("[TextureStore] the transfer queue never finished an upload: the device was lost or it timed out");
    }
    vk::check(vkResetCommandPool(context.device(), slot.pool, 0), "vkResetCommandPool (transfer)");
    transfer_open_ = false;
}

void TextureBackend::note_named_slow(vk::Texture* texture) {
    const auto found = async_.find(texture);
    if (found != async_.end() && !found->second.named) {
        found->second.named = true;
        named_.push_back(texture);
    }
}

void TextureBackend::forget_slow(vk::Texture* texture) {
    if (async_.erase(texture) != 0u) {
        const auto at = std::find(named_.begin(), named_.end(), texture);
        if (at != named_.end()) {
            *at = named_.back();
            named_.pop_back();
        }
    }
}

void TextureBackend::record_async(vk::Texture* texture, Async& state, const UploadChunk& staging, std::uint64_t offset,
                                  std::uint32_t width, std::uint32_t height, unsigned levels, const std::uint64_t* offsets,
                                  const std::uint32_t* pitches) {
    vk::Context& context = vk::Context::get();
    const VkCommandBuffer commands = async_slots_[async_slot_].commands;
    if (!transfer_open_) {
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vk::check(vkBeginCommandBuffer(commands, &begin), "vkBeginCommandBuffer (transfer)");
        transfer_open_ = true;
    }
    // Every level is overwritten: the old contents (a rewrite before any frame acquired the image) are dropped.
    vk::image_barrier(commands, texture->image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
                      VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, texture->levels);
    const std::vector<VkBufferImageCopy> regions = copy_regions(*texture, offset, width, height, levels, offsets, pitches);
    vkCmdCopyBufferToImage(commands, staging.resource->buffer, texture->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, levels,
                           regions.data());
    // The release: the writes made available and the image given to the graphics family, in the layout it is sampled in; the
    // acquire (submit_uploads, acquire_now) repeats the same barrier on that queue.
    vk::BarriersVk release(commands);
    release.image_transfer(texture->image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, context.transfer_family(), context.queue_family(),
                           VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_NONE,
                           VK_ACCESS_2_NONE, texture->levels);
    release.issue();
    texture->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    // Carried by the command buffer that submit_uploads submits.
    state.value = context.transfer_submitted() + 1u;
}

std::uint64_t TextureBackend::acquire_now(vk::Texture* texture) {
    const auto found = async_.find(texture);
    if (found == async_.end()) {
        return 0u;
    }
    const std::uint64_t value = found->second.value;
    vk::Context& context = vk::Context::get();
    vk::BarriersVk acquire(upload_list_);
    acquire.image_transfer(texture->image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, context.transfer_family(), context.queue_family(),
                           kAcquireStages, VK_ACCESS_2_NONE, kAcquireStages, kAcquireAccess, texture->levels);
    acquire.issue();
    forced_wait_ = std::max(forced_wait_, value);
    forget_slow(texture);
    return value;
}

std::uint64_t TextureBackend::submit_uploads() {
    if (async_slots_.empty()) {
        return 0u;
    }
    vk::Context& context = vk::Context::get();
    AsyncSlot& slot = async_slots_[async_slot_];
    if (transfer_open_) {
        vk::check(vkEndCommandBuffer(slot.commands), "vkEndCommandBuffer (transfer)");
        slot.value = context.submit_transfer(slot.commands);
        transfer_open_ = false;
    } else {
        slot.value = std::max(slot.value, context.transfer_submitted());
    }
    std::uint64_t wait = std::exchange(forced_wait_, 0u);
    if (!named_.empty() && upload_list_ != VK_NULL_HANDLE) {
        vk::BarriersVk acquire(upload_list_);
        for (vk::Texture* const texture : named_) {
            const auto found = async_.find(texture);
            if (found == async_.end()) {
                continue;
            }
            if (found->second.value == 0u) {
                found->second.named = false;  // nothing written yet: named again when it is
                continue;
            }
            wait = std::max(wait, found->second.value);
            acquire.image_transfer(texture->image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, context.transfer_family(), context.queue_family(),
                                   kAcquireStages, VK_ACCESS_2_NONE, kAcquireStages, kAcquireAccess, texture->levels);
            async_.erase(found);
        }
        named_.clear();
        acquire.issue();
    }
    return wait;
}

}  // namespace galaxy::render
