// SPDX-License-Identifier: GPL-3.0-only
// TextureStore's Metal side (gpu_texture_backend.h; Vulkan's is vk/texture_backend_vk.h).
//
// A texture's view is a slot, the store's index for it. Where the device reads every texture from one array
// (mtl::DeviceCaps::texture_arrays) the slots are a buffer of resource ids, texture_array(), which a draw's programs
// index; elsewhere a draw binds its own table, built from texture_at(). A slot no texture has holds the null texture
// (one transparent black texel), never nothing.
//
// Metal wants every texture a pass reaches through that array named to the pass. The textures made here come from
// heaps this class owns, so a pass names the heaps: use_textures(), once for each render command encoder that
// draws with the game's programs. A texture given a slot that is not from a heap (a render target's picture) is
// named one by one in the same call.
//
// Levels are staged in shared buffers (chunks) and copied by a blit encoder the renderer hands over for the frame
// (set_upload_list), on a command buffer committed before the frame's draws.
#pragma once

#include "galaxy/render/gpu_types.h"
#include "galaxy/render/mtl/mtl_context.h"
#include "galaxy/render/texel_format.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace galaxy::render {

class TextureBackend {
public:
    // One mapped buffer of a frame's staging arena.
    struct UploadChunk {
        std::shared_ptr<mtl::Buffer> resource;
        std::uint8_t* cpu_base = nullptr;
        std::uint64_t size = 0;
    };
    // The store's own alignments for staged levels (any would do for a blit; these keep its sizes those of the other
    // renderers).
    static constexpr std::uint64_t kPlacementAlignment = 512u;
    static constexpr std::uint32_t kPitchAlignment = 256u;
    struct HeapStats {
        std::uint64_t heaps = 0;
        std::uint64_t heap_bytes = 0;
        std::uint64_t used_bytes = 0;
        std::uint64_t placed = 0;
        std::uint64_t committed_fallbacks = 0;  // textures too large for a heap of the usual size: each has its own
    };

    bool initialize(gpu::Device* device, std::uint32_t view_capacity);
    void shutdown();
    [[nodiscard]] bool ready() const noexcept { return !views_.empty(); }

    // Slot `index` takes `texture` (what draws sample of it: mtl::Texture::view).
    void write_view(std::uint32_t index, mtl::Texture* texture, unsigned levels);
    // Slot `index` takes a texture that is not one of this class's (a picture): named to each pass by itself.
    void write_view(std::uint32_t index, mtl::Object texture);
    void ensure_view(std::uint32_t index);
    // The slot holds the null texture again.
    void release_view(std::uint32_t index);
    // One past the highest slot that holds a texture.
    [[nodiscard]] std::uint32_t live_views() const noexcept;

    // The array of every slot's resource id (an id<MTLBuffer>, eight bytes a slot), for gpu::Table::ptr; it is replaced
    // by one twice as long when a slot past its end is written, and the one before is kept until the GPU is done with
    // the frames that read it. Null where the device has no arrays.
    [[nodiscard]] mtl::Object texture_array() const noexcept { return array_ ? array_->buffer : nullptr; }
    // The array's length in slots; its last slot is never given out by the store while the array has this length, so
    // it reads the null texture: what a draw names for a map it does not have.
    [[nodiscard]] std::uint32_t array_slots() const noexcept { return array_slots_; }
    // The texture of slot `index` (an id<MTLTexture>, never null: the null texture where the slot has none), for a
    // draw's bound table.
    [[nodiscard]] mtl::Object texture_at(std::uint32_t index) const noexcept {
        return index < views_.size() && views_[index] != nullptr ? views_[index] : null_view_;
    }
    [[nodiscard]] mtl::Object null_texture() const noexcept { return null_view_; }
    // Names every texture a slot can hold to `render_encoder` (an id<MTLRenderCommandEncoder>), for its fragment
    // functions: the heaps, then the textures that are not from one. Render thread, or a thread recording one of the
    // pass's chunks while the render thread makes no texture.
    // `targets`: the textures the pass draws into, which are left out (a target's own picture may hold a slot while
    // the next pass draws into it again).
    void use_textures(mtl::Object render_encoder, const mtl::Object* targets = nullptr, std::size_t target_count = 0u) const;
    // The render pass that is open and draws with the game's programs (null: none): a heap made, and a picture given
    // a slot, while it is open are named to it at once. Render thread.
    void set_open_pass(mtl::Object render_encoder) noexcept { open_pass_ = render_encoder; }
    // How many times the array was replaced by a longer one: its last slot, the null texture's, moved each time.
    [[nodiscard]] std::uint32_t array_generation() const noexcept { return array_generation_; }

    [[nodiscard]] UploadChunk create_upload_chunk(std::uint64_t size);
    static void release_upload_chunk(UploadChunk& chunk) noexcept;
    // A texture for sampling, from a heap. R8 is sampled with its value in every channel and Rg8 with the first byte
    // in the colour channels and the second in alpha, as the other renderers' views give them. `writable` is the
    // store's word that its levels are written once: nothing differs here.
    [[nodiscard]] std::shared_ptr<mtl::Texture> create_texture(
        std::uint32_t width, std::uint32_t height, unsigned levels, std::uint64_t& allocation_bytes,
        TexelFormat format = TexelFormat::Rgba8, bool writable = false);
    [[nodiscard]] bool host_writes() const noexcept { return false; }
    [[nodiscard]] static bool direct_written(const mtl::Texture*) noexcept { return false; }
    [[nodiscard]] static bool supports_bc1();
    [[nodiscard]] static bool supports_astc4x4();
    // Every Metal GPU samples BC1 (a Mac's AMD or Intel) or ASTC (Apple's): never needed.
    [[nodiscard]] static bool supports_rgb565() noexcept { return false; }
    // A texture made by create_texture that the pool may hand out again for the same extent.
    [[nodiscard]] static bool poolable(mtl::Texture* texture, std::uint32_t& width, std::uint32_t& height,
                                       std::uint32_t& levels);
    // Copies every level from the staging chunk (level l at offset + offsets[l], rows of pitches[l] bytes).
    void record_upload(mtl::Texture* texture, bool reused, const UploadChunk& staging, std::uint64_t offset,
                       std::uint32_t width, std::uint32_t height, unsigned levels, const std::uint64_t* offsets,
                       const std::uint32_t* pitches);
    // The frame's blit encoder (an id<MTLBlitCommandEncoder>, the renderer's to end); null between frames.
    void set_upload_list(mtl::Object blit_encoder) noexcept { upload_list_ = blit_encoder; }
    [[nodiscard]] bool has_upload_list() const noexcept { return upload_list_ != nullptr; }
    // Levels were copied since the last call: the frame's upload command buffer has work.
    [[nodiscard]] bool take_uploaded() noexcept {
        const bool uploaded = uploaded_;
        uploaded_ = false;
        return uploaded;
    }

    // One queue: nothing is uploaded beside the frame.
    bool enable_async_uploads(unsigned, unsigned) { return false; }
    [[nodiscard]] bool async_uploads() const noexcept { return false; }
    // Each frame's start: now and then, a heap with nothing left in it is given back (never the first).
    void begin_frame(unsigned frame_slot);
    void note_named(mtl::Texture*) {}
    void forget(mtl::Texture*) {}
    void drain() noexcept {}
    [[nodiscard]] HeapStats heap_stats() const noexcept;

private:
    struct Heap {
        mtl::Object heap = nullptr;  // id<MTLHeap>
        std::uint64_t size = 0;
        bool single = false;         // made for one texture larger than a heap of the usual size
    };
    void publish(std::uint32_t index);
    void grow_array(std::uint32_t index);
    [[nodiscard]] mtl::Object allocate(void* descriptor, mtl::Object& heap_out);

    std::vector<mtl::Object> views_;                  // by slot: what is sampled there (not retained), or null
    std::unordered_map<std::uint32_t, mtl::Object> loose_;  // slots whose texture is not from a heap
    std::shared_ptr<mtl::Texture> null_texture_;
    mtl::Object null_view_ = nullptr;
    std::uint64_t null_id_ = 0u;
    std::shared_ptr<mtl::Buffer> array_;
    std::uint32_t array_slots_ = 0u;
    std::uint32_t array_generation_ = 0u;
    mtl::Object open_pass_ = nullptr;                 // id<MTLRenderCommandEncoder>, not retained
    mutable std::mutex heaps_mutex_;                  // create_texture may run while a chunk's thread names the heaps
    std::vector<Heap> heaps_;
    std::vector<mtl::Object> heap_list_;              // heaps_' objects in a row, for useHeaps
    std::uint64_t placed_ = 0u;
    mtl::Object upload_list_ = nullptr;
    bool uploaded_ = false;
    std::uint32_t frames_since_trim_ = 0u;
};

}  // namespace galaxy::render
