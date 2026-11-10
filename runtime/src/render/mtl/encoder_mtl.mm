// SPDX-License-Identifier: GPL-3.0-only
// gpu::Encoder on Metal (core/gpu_encoder.h).
#include "render/core/gpu_encoder.h"

#include "galaxy/render/mtl/shader_msl.h"

#import <Metal/Metal.h>

#include <algorithm>

namespace galaxy::render::gpu {

namespace {

id<MTLRenderCommandEncoder> metal(mtl::Object encoder) noexcept {
    return (__bridge id<MTLRenderCommandEncoder>)encoder;
}

id<MTLBuffer> metal_buffer(mtl::Object buffer) noexcept {
    return (__bridge id<MTLBuffer>)buffer;
}

}  // namespace

void Encoder::bind_pipeline(Pipeline* pipeline) noexcept {
    id<MTLRenderCommandEncoder> const encoder = metal(encoder_);
    const Pipeline* const before = pipeline_;
    [encoder setRenderPipelineState:(__bridge id<MTLRenderPipelineState>)pipeline->state];
    if (before == nullptr || before->depth_stencil != pipeline->depth_stencil) {
        [encoder setDepthStencilState:(__bridge id<MTLDepthStencilState>)pipeline->depth_stencil];
    }
    if (before == nullptr || before->cull != pipeline->cull) {
        [encoder setCullMode:static_cast<MTLCullMode>(pipeline->cull)];
    }
    if (before == nullptr || before->winding != pipeline->winding) {
        [encoder setFrontFacingWinding:static_cast<MTLWinding>(pipeline->winding)];
    }
    if (before == nullptr || before->depth_clip != pipeline->depth_clip) {
        [encoder setDepthClipMode:static_cast<MTLDepthClipMode>(pipeline->depth_clip)];
    }
    if (before == nullptr || before->fill != pipeline->fill) {
        [encoder setTriangleFillMode:static_cast<MTLTriangleFillMode>(pipeline->fill)];
    }
    if (before == nullptr || before->stencil_reference != pipeline->stencil_reference) {
        [encoder setStencilReferenceValue:pipeline->stencil_reference];
    }
    if (before == nullptr || before->depth_bias != pipeline->depth_bias || before->depth_slope != pipeline->depth_slope ||
        before->depth_bias_clamp != pipeline->depth_bias_clamp) {
        [encoder setDepthBias:pipeline->depth_bias slopeScale:pipeline->depth_slope clamp:pipeline->depth_bias_clamp];
    }
}

void Encoder::flush_constants() noexcept {
    if (constants_valid_) {
        return;
    }
    id<MTLRenderCommandEncoder> const encoder = metal(encoder_);
    // Which functions read each buffer, and at which index (shader_msl.h).
    static constexpr struct {
        std::uint32_t index;
        bool vertex, fragment;
    } kPlaces[kSlotCount] = {
        // A material's rows are one block each for the vertex and the pixel program, and a program of either stage
        // may read the other's (Vulkan binds the set to both).
        {mtl::kBufferView, true, true},         {mtl::kBufferVertexRows, true, true},  {mtl::kBufferPixelRows, true, true},
        {mtl::kBufferRows, true, true},         {mtl::kBufferInstances, true, true},   {mtl::kBufferTextureLists, false, true},
    };
    for (unsigned at = 0; at < kSlotCount; ++at) {
        Slot& slot = slots_[at];
        if (slot.wanted == 0u || slot.wanted == slot.bound) {
            continue;
        }
        const mtl::Context::BufferOffset place = mtl::Context::get().resolve(slot.wanted);
        if (place.buffer == nullptr) {
            continue;
        }
        if (place.buffer == slot.buffer) {
            // The same buffer further on: only the offset moves.
            if (kPlaces[at].vertex) [encoder setVertexBufferOffset:place.offset atIndex:kPlaces[at].index];
            if (kPlaces[at].fragment) [encoder setFragmentBufferOffset:place.offset atIndex:kPlaces[at].index];
        } else {
            id<MTLBuffer> const buffer = metal_buffer(place.buffer);
            if (kPlaces[at].vertex) [encoder setVertexBuffer:buffer offset:place.offset atIndex:kPlaces[at].index];
            if (kPlaces[at].fragment) [encoder setFragmentBuffer:buffer offset:place.offset atIndex:kPlaces[at].index];
            slot.buffer = place.buffer;
        }
        slot.bound = slot.wanted;
    }
    constants_valid_ = true;
}

void Encoder::push_root() noexcept {
    id<MTLRenderCommandEncoder> const encoder = metal(encoder_);
    [encoder setVertexBytes:&root_ length:sizeof(root_) atIndex:mtl::kBufferDraw];
    [encoder setFragmentBytes:&root_ length:sizeof(root_) atIndex:mtl::kBufferDraw];
}

void Encoder::bind_tables(std::uint64_t textures, std::uint64_t samplers) noexcept {
    id<MTLRenderCommandEncoder> const encoder = metal(encoder_);
    if (textures != 0u) {
        const auto* const table = reinterpret_cast<const mtl::BoundTextures*>(static_cast<std::uintptr_t>(textures));
        [encoder setFragmentTextures:(__unsafe_unretained id<MTLTexture> const*)(void*)table->textures.data()
                           withRange:NSMakeRange(0, mtl::kBoundTableSlots)];
    }
    if (samplers != 0u) {
        const auto* const table = reinterpret_cast<const mtl::BoundSamplers*>(static_cast<std::uintptr_t>(samplers));
        [encoder setFragmentSamplerStates:(__unsafe_unretained id<MTLSamplerState> const*)(void*)table->samplers.data()
                                withRange:NSMakeRange(0, mtl::kBoundTableSlots)];
    }
}

void Encoder::bind_arrays(std::uint64_t textures, std::uint64_t samplers) noexcept {
    id<MTLRenderCommandEncoder> const encoder = metal(encoder_);
    if (textures != 0u) {
        [encoder setFragmentBuffer:metal_buffer(reinterpret_cast<mtl::Object>(static_cast<std::uintptr_t>(textures)))
                            offset:0
                           atIndex:mtl::kBufferTextureArray];
    }
    if (samplers != 0u) {
        [encoder setFragmentBuffer:metal_buffer(reinterpret_cast<mtl::Object>(static_cast<std::uintptr_t>(samplers)))
                            offset:0
                           atIndex:mtl::kBufferSamplerArray];
    }
}

void Encoder::apply_scissor() noexcept {
    // Cut to the target: Metal takes no rectangle that leaves it, and none without an area.
    const std::int64_t left = std::clamp<std::int64_t>(scissor_.left, 0, target_width_);
    const std::int64_t top = std::clamp<std::int64_t>(scissor_.top, 0, target_height_);
    const std::int64_t right = std::clamp<std::int64_t>(scissor_.right, left, target_width_);
    const std::int64_t bottom = std::clamp<std::int64_t>(scissor_.bottom, top, target_height_);
    scissor_empty_ = right <= left || bottom <= top;
    if (scissor_empty_) {
        return;
    }
    const MTLScissorRect rect{static_cast<NSUInteger>(left), static_cast<NSUInteger>(top), static_cast<NSUInteger>(right - left),
                              static_cast<NSUInteger>(bottom - top)};
    [metal(encoder_) setScissorRect:rect];
}

void Encoder::apply_viewport() noexcept {
    // The programs' clip space is Direct3D's, which is Metal's: nothing is turned.
    const MTLViewport viewport{viewport_.x, viewport_.y, viewport_.width, viewport_.height, viewport_.min_depth, viewport_.max_depth};
    [metal(encoder_) setViewport:viewport];
}

void Encoder::bind_stream(unsigned slot, const StreamView& view) noexcept {
    const mtl::Context::BufferOffset place = mtl::Context::get().resolve(view.address);
    if (place.buffer == nullptr) {
        return;
    }
    id<MTLRenderCommandEncoder> const encoder = metal(encoder_);
    const NSUInteger index = mtl::kBufferStream0 + slot;
    if (place.buffer == stream_buffers_[slot]) {
        [encoder setVertexBufferOffset:place.offset atIndex:index];
    } else {
        [encoder setVertexBuffer:metal_buffer(place.buffer) offset:place.offset atIndex:index];
        stream_buffers_[slot] = place.buffer;
    }
}

void Encoder::set_indices(Address address, std::uint32_t bytes, std::uint32_t index_bytes) noexcept {
    if (index_valid_ && address == index_address_ && index_bytes == index_stride_) {
        return;
    }
    const mtl::Context::BufferOffset place = mtl::Context::get().resolve(address);
    index_buffer_ = place.buffer;
    index_offset_ = place.offset;
    index_address_ = address;
    index_size_ = bytes;
    index_stride_ = index_bytes;
    index_valid_ = place.buffer != nullptr;
}

void Encoder::draw_indexed(std::uint32_t count, std::uint32_t instances, std::uint32_t first, std::int32_t base_vertex) noexcept {
    if (scissor_empty_ || pipeline_ == nullptr || !index_valid_ || count == 0u || instances == 0u) {
        return;
    }
    flush_constants();
    [metal(encoder_) drawIndexedPrimitives:static_cast<MTLPrimitiveType>(pipeline_->primitive)
                                indexCount:count
                                 indexType:index_stride_ == 2u ? MTLIndexTypeUInt16 : MTLIndexTypeUInt32
                               indexBuffer:metal_buffer(index_buffer_)
                         indexBufferOffset:index_offset_ + std::uint64_t{first} * index_stride_
                             instanceCount:instances
                                baseVertex:base_vertex
                              baseInstance:0];
}

void Encoder::draw(std::uint32_t count, std::uint32_t instances, std::uint32_t first) noexcept {
    if (scissor_empty_ || pipeline_ == nullptr || count == 0u || instances == 0u) {
        return;
    }
    flush_constants();
    [metal(encoder_) drawPrimitives:static_cast<MTLPrimitiveType>(pipeline_->primitive)
                        vertexStart:first
                        vertexCount:count
                      instanceCount:instances];
}

}  // namespace galaxy::render::gpu
