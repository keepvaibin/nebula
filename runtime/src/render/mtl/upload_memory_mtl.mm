// SPDX-License-Identifier: GPL-3.0-only
// gpu_types.h's upload memory and render textures on Metal.
#include "galaxy/render/gpu_types.h"

#import <Metal/Metal.h>

namespace galaxy::render::gpu {

UploadMemory create_upload_memory(Device*, std::size_t bytes, const char* name, UploadPlace) {
    mtl::Context& context = mtl::Context::get();
    if (!context.ready() || bytes == 0u) {
        return {};
    }
    // One buffer serves any ring: vertices, indices, constants and rows. Shared memory: the GPU reads what the CPU
    // wrote with no copy on every Apple GPU, and across the bus as an upload heap does on an Intel Mac's.
    std::shared_ptr<mtl::Buffer> buffer = context.create_buffer(bytes, mtl::Storage::Shared, name);
    if (!buffer) {
        return {};
    }
    UploadMemory out;
    out.local = context.caps().unified_memory;
    out.cpu = buffer->mapped;
    out.gpu = buffer->address;
    out.buffer = std::move(buffer);
    return out;
}

bool device_removed(Device*) noexcept {
    // Metal says so through a command buffer that ends in an error (mtl::Context::take_failure), never by itself.
    return false;
}

TextureRef create_render_texture(Device*, std::uint32_t width, std::uint32_t height) {
    return mtl::Context::get().create_texture(width, height, 1u, mtl::Format::Rgba8, mtl::kUseTarget | mtl::kUseSample,
                                              mtl::Storage::Private, "display picture");
}

}  // namespace galaxy::render::gpu
