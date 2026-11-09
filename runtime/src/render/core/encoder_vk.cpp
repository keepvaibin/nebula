// SPDX-License-Identifier: GPL-3.0-only
#include "render/core/gpu_encoder.h"

namespace galaxy::render::gpu {

void Encoder::set_stream(unsigned slot, const StreamView& view) noexcept {
    StreamView& bound = streams_[slot];
    if (view.address == bound.address) {
        return;
    }
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceSize offset = 0u;
    layout_.resolve(layout_.renderer, view.address, buffer, offset);
    vkCmdBindVertexBuffers(commands_, slot, 1u, &buffer, &offset);
    bound = view;
}

void Encoder::set_indices(Address address, std::uint32_t bytes, std::uint32_t index_bytes) noexcept {
    if (index_valid_ && address == index_address_ && index_bytes == index_stride_) {
        return;
    }
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceSize offset = 0u;
    layout_.resolve(layout_.renderer, address, buffer, offset);
    vkCmdBindIndexBuffer(commands_, buffer, offset, index_bytes == 2u ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
    index_address_ = address;
    index_size_ = bytes;
    index_stride_ = index_bytes;
    index_valid_ = true;
}

void Encoder::flush_constants() noexcept {
    if (offsets_valid_) {
        return;
    }
    // Set 0 names the buffers; the three constants' offsets are dynamic. A draw whose program reads no material rows
    // leaves those addresses 0: whichever buffer was bound stands in.
    const auto place = [this](Address address, Address& bound, VkBuffer& buffer, std::uint32_t& offset) {
        if (address != 0u && address != bound) {
            VkDeviceSize at = 0u;
            layout_.resolve(layout_.renderer, address, buffer, at);
            offset = static_cast<std::uint32_t>(at);
            bound = address;
        }
    };
    place(view_, bound_view_, view_buffer_, offsets_[0]);
    place(vertex_rows_, bound_vertex_rows_, vertex_buffer_, offsets_[1]);
    place(pixel_rows_, bound_pixel_rows_, pixel_buffer_, offsets_[2]);
    if (vertex_buffer_ == VK_NULL_HANDLE) {
        vertex_buffer_ = view_buffer_;
    }
    if (pixel_buffer_ == VK_NULL_HANDLE) {
        pixel_buffer_ = view_buffer_;
    }
    const std::array<VkBuffer, 3> buffers{view_buffer_, vertex_buffer_, pixel_buffer_};
    if (set0_ == VK_NULL_HANDLE || buffers != set_buffers_ || rows_ != bound_rows_ || instances_ != bound_instances_ ||
        lists_ != bound_lists_) {
        set0_ = layout_.constants_set(layout_.renderer, buffers.data(), rows_, instances_, lists_);
        set_buffers_ = buffers;
        bound_rows_ = rows_;
        bound_instances_ = instances_;
        bound_lists_ = lists_;
    }
    vkCmdBindDescriptorSets(commands_, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_.layout, 0u, 1u, &set0_, 3u, offsets_.data());
    offsets_valid_ = true;
}

}  // namespace galaxy::render::gpu
