// SPDX-License-Identifier: GPL-3.0-only
// TextureStore's Vulkan side (gpu_texture_backend.h): image views by srv_index, mapped staging buffers, RGBA8 images and
// the recorded upload of their levels.
#pragma once

#include "galaxy/render/texel_format.h"

#include "galaxy/render/vk/vk_context.h"

#include <cstdint>
#include <algorithm>
#include <memory>
#include <vector>

namespace galaxy::render {

class TextureBackend {
public:
    struct UploadChunk {
        std::shared_ptr<vk::Buffer> resource;
        std::uint8_t* cpu_base = nullptr;
        std::uint64_t size = 0;
    };
    // D3D12's footprint rules, kept so the shared staging layout is the same: Vulkan needs only texel alignment.
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
    [[nodiscard]] bool ready() const noexcept { return !views_.empty(); }

    void write_view(std::uint32_t index, vk::Texture* texture, unsigned levels);
    void ensure_view(std::uint32_t index) { if (index >= views_.size()) { views_.resize(std::max<std::size_t>(std::size_t{index} + 1u, views_.size() * 2u), VK_NULL_HANDLE); } }
    void write_view(std::uint32_t index, VkImageView view) {
        ensure_view(index);
        views_[index] = view;
        publish_view(index);
    }
    // The bindless image array (RendererVk): `images` the descriptor set whose first `count` image slots hold every view
    // at its index. The views made so far are written now, each later one when it is written (write_view). A view whose
    // index reaches `count` calls `grow(owner, index)`, which must move the mirror to a larger set (move_mirror).
    using GrowFn = void (*)(void* owner, std::uint32_t index);
    void mirror_into(VkDescriptorSet images, std::uint32_t count, GrowFn grow, void* owner);
    // The later views go to `images` (its first `count` slots), which already holds the earlier ones.
    void move_mirror(VkDescriptorSet images, std::uint32_t count) noexcept {
        mirror_ = images;
        mirror_count_ = count;
    }
    // One past the highest index that holds a view of a texture (the null view excluded).
    [[nodiscard]] std::uint32_t live_views() const noexcept;
    void publish_view(std::uint32_t index);
    // A view index the store gave up (its texture is gone and the GPU is past the frames that read it): the slot of the
    // image array holds the null view again, so a copy of the array never moves a destroyed view.
    void release_view(std::uint32_t index);
    [[nodiscard]] UploadChunk create_upload_chunk(std::uint64_t size);
    static void release_upload_chunk(UploadChunk& chunk) noexcept;
    // RGBA8, or one byte per texel whose view repeats it in every channel (`grey`, a movie plane).
    [[nodiscard]] gpu::TextureRef create_texture(
        std::uint32_t width, std::uint32_t height, unsigned levels, std::uint64_t& allocation_bytes,
        TexelFormat format = TexelFormat::Rgba8);
    [[nodiscard]] static bool supports_bc1();
    [[nodiscard]] static bool poolable(vk::Texture* texture, std::uint32_t& width, std::uint32_t& height,
                                       std::uint32_t& levels);
    void record_upload(vk::Texture* texture, bool reused, const UploadChunk& staging, std::uint64_t offset,
                       std::uint32_t width, std::uint32_t height, unsigned levels, const std::uint64_t* offsets,
                       const std::uint32_t* pitches);
    // Clears the null view's texel and makes it sampleable; once, before the first draw.
    void prepare_null_view(VkCommandBuffer commands);
    void set_upload_list(VkCommandBuffer commands) noexcept { upload_list_ = commands; }
    [[nodiscard]] bool has_upload_list() const noexcept { return upload_list_ != VK_NULL_HANDLE; }
    [[nodiscard]] HeapStats heap_stats() const noexcept { return {}; }

    // The renderer writes these views into its frame descriptor sets.
    [[nodiscard]] VkImageView view(std::uint32_t index) const noexcept { return views_[index]; }
    [[nodiscard]] VkImageView null_view() const noexcept { return null_texture_->view; }

private:
    std::vector<VkImageView> views_;
    std::shared_ptr<vk::Texture> null_texture_;
    VkDescriptorSet mirror_ = VK_NULL_HANDLE;
    std::uint32_t mirror_count_ = 0;
    GrowFn mirror_grow_ = nullptr;
    void* mirror_owner_ = nullptr;
    VkCommandBuffer upload_list_ = VK_NULL_HANDLE;
};

}  // namespace galaxy::render
