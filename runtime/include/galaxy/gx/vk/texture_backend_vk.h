// SPDX-License-Identifier: GPL-3.0-only
// TextureCache's Vulkan side (texture_backend.h): image views by srv_index, mapped staging buffers, RGBA8 images and
// the recorded upload of their levels.
#pragma once

#include "galaxy/gx/vk/vk_context.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace galaxy::gx {

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
    [[nodiscard]] UploadChunk create_upload_chunk(std::uint64_t size);
    static void release_upload_chunk(UploadChunk& chunk) noexcept;
    // RGBA8, or one byte per texel whose view repeats it in every channel (`grey`, a movie plane).
    [[nodiscard]] gpu::TextureRef create_texture(
        std::uint32_t width, std::uint32_t height, unsigned levels, std::uint64_t& allocation_bytes, bool grey = false);
    [[nodiscard]] static bool poolable(vk::Texture* texture, std::uint32_t& width, std::uint32_t& height,
                                       std::uint32_t& levels);
    [[nodiscard]] static bool alias_compatible(vk::Texture* texture, std::uint32_t& width, std::uint32_t& height);
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
    [[nodiscard]] VkImageView null_view() const noexcept { return views_.back(); }

private:
    std::vector<VkImageView> views_;  // view_capacity slots, then the null view
    std::shared_ptr<vk::Texture> null_texture_;
    VkCommandBuffer upload_list_ = VK_NULL_HANDLE;
};

}  // namespace galaxy::gx
