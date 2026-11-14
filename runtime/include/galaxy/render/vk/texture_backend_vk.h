// SPDX-License-Identifier: GPL-3.0-only
// TextureStore's Vulkan side (gpu_texture_backend.h): image views by srv_index, mapped staging buffers, RGBA8 images and
// the recorded upload of their levels.
#pragma once

#include "galaxy/render/texel_format.h"

#include "galaxy/render/vk/vk_context.h"

#include <cstdint>
#include <algorithm>
#include <memory>
#include <unordered_map>
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
    // RGBA8, or one byte per texel whose view repeats it in every channel (`grey`, a movie plane). `writable`: the levels are
    // uploaded once and never rewritten while the GPU may read them; with host image copy (host_writes()) and a format the
    // device copies to at full speed, the image is made for host copies and record_upload writes it with no command.
    [[nodiscard]] gpu::TextureRef create_texture(
        std::uint32_t width, std::uint32_t height, unsigned levels, std::uint64_t& allocation_bytes,
        TexelFormat format = TexelFormat::Rgba8, bool writable = false);
    // VK_EXT_host_image_copy is in use for some formats.
    [[nodiscard]] bool host_writes() const noexcept { return host_formats_ != 0u; }
    // A texture create_texture made for host copies: it is never uploaded again in place.
    [[nodiscard]] static bool direct_written(const vk::Texture* texture) noexcept {
        return texture != nullptr && (texture->usage & VK_IMAGE_USAGE_HOST_TRANSFER_BIT_EXT) != 0u;
    }
    [[nodiscard]] static bool supports_bc1();
    [[nodiscard]] static bool supports_astc4x4();
    // R5G6B5 sampled with linear filtering and written by copies (Vulkan 1.0's mandatory formats hold it; asked anyway).
    [[nodiscard]] static bool supports_rgb565();
    [[nodiscard]] static bool poolable(vk::Texture* texture, std::uint32_t& width, std::uint32_t& height,
                                       std::uint32_t& levels);
    void record_upload(vk::Texture* texture, bool reused, const UploadChunk& staging, std::uint64_t offset,
                       std::uint32_t width, std::uint32_t height, unsigned levels, const std::uint64_t* offsets,
                       const std::uint32_t* pitches);
    // Clears the null view's texel and makes it sampleable; once, before the first draw.
    void prepare_null_view(VkCommandBuffer commands);
    void set_upload_list(VkCommandBuffer commands) noexcept { upload_list_ = commands; }
    [[nodiscard]] bool has_upload_list() const noexcept { return upload_list_ != VK_NULL_HANDLE; }

    // Uploads on a transfer-only queue (Context::transfer_queue): a new image is written by a command buffer of that queue's
    // family (record_upload) and handed to the graphics family with a queue family ownership transfer; the release is
    // recorded there, the acquire in the frame's upload list by submit_uploads, before the first frame whose draws name the
    // image (note_named, called wherever a handle is given out); that frame's submission waits for the transfer timeline
    // semaphore. An image already acquired is rewritten on the graphics queue as before. Not for images host copies write.
    // False: the same queue, as before.
    bool enable_async_uploads(unsigned frames_in_flight, unsigned frame_slot);
    [[nodiscard]] bool async_uploads() const noexcept { return !async_slots_.empty(); }
    // At the start of a frame in `frame_slot`, after the renderer has waited for the slot's fence: waits until the transfer
    // queue has finished what that slot's frame sent it (its staging buffers and retired images go free next).
    void begin_frame(unsigned frame_slot);
    // The frame names `texture` in a draw. Free when no upload is outstanding.
    void note_named(vk::Texture* texture) {
        if (!async_.empty()) {
            note_named_slow(texture);
        }
    }
    // The texture is given up (retired): no acquire will name it.
    void forget(vk::Texture* texture) {
        if (!async_.empty()) {
            forget_slow(texture);
        }
    }
    // Ends the frame's uploads: the transfer command buffer is submitted, the images this frame names are acquired in the
    // upload list, and the returned transfer value is what the frame's submission must wait for (0: nothing).
    [[nodiscard]] std::uint64_t submit_uploads();
    // Waits until the transfer queue has finished everything sent to it.
    void drain() noexcept;
    [[nodiscard]] HeapStats heap_stats() const noexcept { return {}; }

    // The renderer writes these views into its frame descriptor sets.
    [[nodiscard]] VkImageView view(std::uint32_t index) const noexcept { return views_[index]; }
    [[nodiscard]] VkImageView null_view() const noexcept { return null_texture_->view; }

private:
    // An image made for the transfer queue: the transfer value that carries its latest upload (0: not uploaded yet), and
    // whether a draw named it since.
    struct Async {
        std::uint64_t value = 0u;
        bool named = false;
    };
    struct AsyncSlot {
        VkCommandPool pool = VK_NULL_HANDLE;
        VkCommandBuffer commands = VK_NULL_HANDLE;
        std::uint64_t value = 0u;  // the transfer value after this slot's last frame
    };
    void note_named_slow(vk::Texture* texture);
    void forget_slow(vk::Texture* texture);
    void record_async(vk::Texture* texture, Async& state, const UploadChunk& staging, std::uint64_t offset, std::uint32_t width,
                      std::uint32_t height, unsigned levels, const std::uint64_t* offsets, const std::uint32_t* pitches);
    // Records the acquire of `texture` in the frame's upload list now and returns its transfer value.
    std::uint64_t acquire_now(vk::Texture* texture);
    void destroy_async() noexcept;
    std::vector<VkImageView> views_;
    std::shared_ptr<vk::Texture> null_texture_;
    std::uint32_t host_formats_ = 0u;  // bit per TexelFormat the device host-copies to at full speed
    VkDescriptorSet mirror_ = VK_NULL_HANDLE;
    std::uint32_t mirror_count_ = 0;
    GrowFn mirror_grow_ = nullptr;
    void* mirror_owner_ = nullptr;
    VkCommandBuffer upload_list_ = VK_NULL_HANDLE;
    std::vector<AsyncSlot> async_slots_;
    unsigned async_slot_ = 0u;
    bool transfer_open_ = false;
    std::unordered_map<vk::Texture*, Async> async_;
    std::vector<vk::Texture*> named_;  // async_ entries a draw named since the last submit_uploads
    std::uint64_t forced_wait_ = 0u;   // the transfer value a frame must wait for apart from the named images' (acquire_now)
};

}  // namespace galaxy::render
