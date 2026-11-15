// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/render/gpu_texture_backend.h"

#include <algorithm>
#include <cstring>
#include <new>
#include <vector>

namespace galaxy::render {

bool TextureBackend::initialize(gpu::Device* device, std::uint32_t view_capacity) {
    device_ = device;
    views_.clear();
    views_.reserve(view_capacity);
    null_texture_ = d3d11::Context::get().create_texture(1u, 1u, 1u, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE,
                                                        "null texture");
    if (null_texture_ == nullptr) {
        return false;
    }
    const std::uint32_t black = 0u;
    d3d11::Context::get().immediate()->UpdateSubresource(null_texture_->texture.Get(), 0u, nullptr, &black, 4u, 4u);
    return true;
}

void TextureBackend::shutdown() {
    views_.clear();
    null_texture_.reset();
    upload_list_ = nullptr;
    device_ = nullptr;
}

void TextureBackend::write_view(std::uint32_t index, d3d11::Texture* texture, unsigned) {
    ensure_view(index);
    views_[index] = texture != nullptr ? texture->view.Get() : nullptr;
}

void TextureBackend::ensure_view(std::uint32_t index) {
    if (index >= views_.size()) {
        views_.resize(static_cast<std::size_t>(index) + 1u, nullptr);
    }
}

void TextureBackend::release_view(std::uint32_t index) {
    if (index < views_.size()) {
        views_[index] = nullptr;
    }
}

TextureBackend::UploadChunk TextureBackend::create_upload_chunk(std::uint64_t size) {
    UploadChunk chunk;
    chunk.resource = std::shared_ptr<std::uint8_t[]>(new (std::nothrow) std::uint8_t[size]);
    if (chunk.resource != nullptr) {
        chunk.cpu_base = chunk.resource.get();
        chunk.size = size;
    }
    return chunk;
}

void TextureBackend::release_upload_chunk(UploadChunk& chunk) noexcept {
    chunk = UploadChunk{};
}

gpu::TextureRef TextureBackend::create_texture(std::uint32_t width, std::uint32_t height, unsigned levels,
                                               std::uint64_t& allocation_bytes, TexelFormat format, bool) {
    const DXGI_FORMAT dxgi = format == TexelFormat::Bc1 ? DXGI_FORMAT_BC1_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM;
    std::shared_ptr<d3d11::Texture> texture =
        d3d11::Context::get().create_texture(width, height, levels, dxgi, D3D11_BIND_SHADER_RESOURCE, "store texture");
    if (texture == nullptr) {
        allocation_bytes = 0u;
        return {};
    }
    texture->widened = format == TexelFormat::R8 ? 1u : format == TexelFormat::Rg8 ? 2u : 0u;
    allocation_bytes = texture->allocation_bytes;
    stats_.placed += 1u;
    return texture;
}

bool TextureBackend::poolable(d3d11::Texture* texture, std::uint32_t& width, std::uint32_t& height, std::uint32_t& levels) {
    if (texture == nullptr || texture->format != DXGI_FORMAT_R8G8B8A8_UNORM || texture->widened != 0u ||
        texture->target != nullptr) {
        return false;
    }
    width = texture->width;
    height = texture->height;
    levels = texture->levels;
    return true;
}

void TextureBackend::record_upload(d3d11::Texture* texture, bool, const UploadChunk& staging, std::uint64_t offset,
                                   std::uint32_t width, std::uint32_t height, unsigned levels, const std::uint64_t* offsets,
                                   const std::uint32_t* pitches) {
    if (texture == nullptr || upload_list_ == nullptr || staging.cpu_base == nullptr) {
        return;
    }
    const bool blocks = texture->format == DXGI_FORMAT_BC1_UNORM;
    std::vector<std::uint32_t> wide;
    for (unsigned level = 0; level < levels; ++level) {
        const std::uint32_t w = std::max(1u, width >> level);
        const std::uint32_t h = std::max(1u, height >> level);
        const std::uint8_t* source = staging.cpu_base + offset + offsets[level];
        std::uint32_t pitch = pitches[level];
        if (texture->widened != 0u) {
            // One or two channels to four, as the other renderers' views read them: rrrr, rrrg.
            wide.resize(static_cast<std::size_t>(w) * h);
            const unsigned step = texture->widened;
            for (std::uint32_t y = 0; y < h; ++y) {
                const std::uint8_t* row = source + static_cast<std::size_t>(y) * pitch;
                std::uint32_t* out = wide.data() + static_cast<std::size_t>(y) * w;
                for (std::uint32_t x = 0; x < w; ++x) {
                    const std::uint32_t r = row[x * step];
                    const std::uint32_t a = step == 2u ? row[x * 2u + 1u] : r;
                    out[x] = r | (r << 8u) | (r << 16u) | (a << 24u);
                }
            }
            source = reinterpret_cast<const std::uint8_t*>(wide.data());
            pitch = w * 4u;
        }
        const std::uint32_t rows = blocks ? (h + 3u) / 4u : h;
        upload_list_->UpdateSubresource(texture->texture.Get(), level, nullptr, source, pitch, pitch * rows);
    }
}

}  // namespace galaxy::render
