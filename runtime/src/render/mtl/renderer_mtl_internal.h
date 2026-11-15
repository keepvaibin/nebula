// SPDX-License-Identifier: GPL-3.0-only
// RendererMtl's helpers shared by its Objective-C++ sources: mtl::Object back to Metal's own types.
#pragma once

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include "galaxy/render/mtl/mtl_context.h"
#include "galaxy/render/mtl/renderer_mtl.h"

namespace galaxy::render::mtl_detail {

[[nodiscard]] inline id<MTLDevice> metal_device() noexcept { return (__bridge id<MTLDevice>)mtl::Context::get().device(); }
[[nodiscard]] inline id<MTLCommandQueue> queue() noexcept { return (__bridge id<MTLCommandQueue>)mtl::Context::get().queue(); }
[[nodiscard]] inline id<MTLCommandBuffer> commands_of(mtl::Object object) noexcept {
    return (__bridge id<MTLCommandBuffer>)object;
}
[[nodiscard]] inline id<MTLRenderCommandEncoder> pass_of(mtl::Object object) noexcept {
    return (__bridge id<MTLRenderCommandEncoder>)object;
}
[[nodiscard]] inline id<MTLTexture> texture_of(mtl::Object object) noexcept { return (__bridge id<MTLTexture>)object; }
[[nodiscard]] inline id<MTLTexture> texture_of(const mtl::Texture& texture) noexcept {
    return (__bridge id<MTLTexture>)texture.texture;
}
[[nodiscard]] inline id<MTLTexture> texture_of(const gpu::TextureRef& texture) noexcept {
    return texture ? (__bridge id<MTLTexture>)texture->texture : nil;
}
[[nodiscard]] inline id<MTLBuffer> buffer_of(mtl::Object object) noexcept { return (__bridge id<MTLBuffer>)object; }
[[nodiscard]] inline id<MTLBuffer> buffer_of(const std::shared_ptr<mtl::Buffer>& buffer) noexcept {
    return buffer ? (__bridge id<MTLBuffer>)buffer->buffer : nil;
}
// A reference of its own for an mtl::Object member.
[[nodiscard]] inline mtl::Object own(id object) noexcept { return (__bridge_retained void*)object; }
inline void drop(mtl::Object& object) noexcept {
    mtl::release(object);
    object = nullptr;
}

inline constexpr NSUInteger kWriteAll = MTLColorWriteMaskRed | MTLColorWriteMaskGreen | MTLColorWriteMaskBlue | MTLColorWriteMaskAlpha;
inline constexpr NSUInteger kWriteRgb = MTLColorWriteMaskRed | MTLColorWriteMaskGreen | MTLColorWriteMaskBlue;

// Sampler slots a frame may add: the sampler array starts again between frames when fewer than this are free.
[[nodiscard]] inline unsigned sampler_frame_room(unsigned slots) noexcept { return slots >= 2048u ? 512u : slots / 4u; }

}  // namespace galaxy::render::mtl_detail
