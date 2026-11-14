// SPDX-License-Identifier: GPL-3.0-only
// TextureStore's Direct3D 11 side (gpu_texture_backend.h; the other back ends' are gpu_texture_backend_d3d12.cpp,
// vk/texture_backend_vk.h, mtl/texture_backend_mtl.h).
//
// A texture's view is a slot, the store's index for it; a draw binds its own table of views (no arrays on Direct3D 11),
// built from view_at(). A slot no texture has reads the null texture (one transparent black texel), never nothing.
//
// Levels are staged in system memory (chunks) and written by UpdateSubresource on the immediate context, in the frame
// that names them: Direct3D 11 orders that copy before every later draw, and keeps the bytes it needs, so a chunk is
// free again once the call returns.
#pragma once

#include "galaxy/render/d3d11/d3d11_context.h"
#include "galaxy/render/gpu_types.h"
#include "galaxy/render/texel_format.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace galaxy::render {

class TextureBackend {
public:
    // One block of a frame's staging arena: plain system memory.
    struct UploadChunk {
        std::shared_ptr<std::uint8_t[]> resource;
        std::uint8_t* cpu_base = nullptr;
        std::uint64_t size = 0;
    };
    // The store's alignments for staged levels (UpdateSubresource takes any; these keep its sizes those of the other
    // renderers).
    static constexpr std::uint64_t kPlacementAlignment = 512u;
    static constexpr std::uint32_t kPitchAlignment = 256u;
    struct HeapStats {
        std::uint64_t heaps = 0;
        std::uint64_t heap_bytes = 0;
        std::uint64_t used_bytes = 0;
        std::uint64_t placed = 0;
        std::uint64_t committed_fallbacks = 0;
    };

    bool initialize(gpu::Device* device, std::uint32_t view_capacity);
    void shutdown();
    [[nodiscard]] bool ready() const noexcept { return null_texture_ != nullptr; }

    // Slot `index` takes `texture`'s view of `levels` mips.
    void write_view(std::uint32_t index, d3d11::Texture* texture, unsigned levels);
    void ensure_view(std::uint32_t index);
    // The slot holds the null texture again.
    void release_view(std::uint32_t index);
    // What slot `index` samples (never null: the null texture's view where the slot has none), for a draw's table.
    [[nodiscard]] ID3D11ShaderResourceView* view_at(std::uint32_t index) const noexcept {
        return index < views_.size() && views_[index] != nullptr ? views_[index] : null_view();
    }
    [[nodiscard]] ID3D11ShaderResourceView* null_view() const noexcept {
        return null_texture_ != nullptr ? null_texture_->view.Get() : nullptr;
    }
    // One past the highest slot written so far.
    [[nodiscard]] std::uint32_t views_made() const noexcept { return static_cast<std::uint32_t>(views_.size()); }

    [[nodiscard]] UploadChunk create_upload_chunk(std::uint64_t size);
    static void release_upload_chunk(UploadChunk& chunk) noexcept;
    // A texture for sampling. R8 and Rg8 are sampled as the other renderers' views give them (the programs read their
    // channels where the formats put them: R8's value in red, Rg8's in red and green).
    [[nodiscard]] gpu::TextureRef create_texture(std::uint32_t width, std::uint32_t height, unsigned levels,
                                                 std::uint64_t& allocation_bytes, TexelFormat format = TexelFormat::Rgba8,
                                                 bool writable = false);
    [[nodiscard]] static bool direct_written(const d3d11::Texture*) noexcept { return false; }
    // Feature level 11_0 samples BC1 always, so the store's other BLOCK4 forms are never taken here.
    [[nodiscard]] static bool supports_bc1() noexcept { return true; }
    [[nodiscard]] static bool supports_astc4x4() noexcept { return false; }
    [[nodiscard]] static bool supports_rgb565() noexcept { return false; }
    [[nodiscard]] static bool poolable(d3d11::Texture* texture, std::uint32_t& width, std::uint32_t& height,
                                       std::uint32_t& levels);
    // Writes every level from the staging chunk (level l at offset + offsets[l], rows of pitches[l] bytes).
    void record_upload(d3d11::Texture* texture, bool reused, const UploadChunk& staging, std::uint64_t offset,
                       std::uint32_t width, std::uint32_t height, unsigned levels, const std::uint64_t* offsets,
                       const std::uint32_t* pitches);
    // The immediate context while a frame is being recorded (the render thread's); null between frames.
    void set_upload_list(ID3D11DeviceContext1* context) noexcept { upload_list_ = context; }
    [[nodiscard]] bool has_upload_list() const noexcept { return upload_list_ != nullptr; }

    // One context: nothing is uploaded beside the frame.
    bool enable_async_uploads(unsigned, unsigned) { return false; }
    [[nodiscard]] bool async_uploads() const noexcept { return false; }
    void begin_frame(unsigned) {}
    void note_named(d3d11::Texture*) {}
    void forget(d3d11::Texture*) {}
    void drain() noexcept {}
    [[nodiscard]] HeapStats heap_stats() const noexcept { return stats_; }

private:
    ID3D11Device1* device_ = nullptr;
    std::vector<ID3D11ShaderResourceView*> views_;  // by slot (not owned: the texture keeps its view), or null
    std::shared_ptr<d3d11::Texture> null_texture_;
    ID3D11DeviceContext1* upload_list_ = nullptr;
    HeapStats stats_{};
};

}  // namespace galaxy::render
