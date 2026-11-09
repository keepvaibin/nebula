// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// TextureStore's GPU side: shader views by slot, mapped upload staging, textures, and the recorded upload of a
// texture's levels. The cache's logic (keys, maps, aliases, eviction, retirement) is shared; each API implements
// this class (gpu_texture_backend_d3d12.cpp, vk/texture_backend_vk.cpp).

#include "galaxy/render/gpu_types.h"
#include "galaxy/render/texel_format.h"

#include <cstdint>
#include <vector>

#if defined(NEBULA_GPU_VULKAN)
#include "galaxy/render/vk/texture_backend_vk.h"
#else
#include "galaxy/render/gpu_placed_textures.h"

namespace galaxy::render {

class TextureBackend {
public:
    // One mapped UPLOAD buffer of a frame's staging arena.
    struct UploadChunk {
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        std::uint8_t* cpu_base = nullptr;
        std::uint64_t size = 0;
    };
    static constexpr std::uint64_t kPlacementAlignment = D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT;
    static constexpr std::uint32_t kPitchAlignment = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;

    // Initial view-page size; later pages grow independently of live descriptors.
    bool initialize(gpu::Device* device, std::uint32_t view_capacity);
    void shutdown();
    [[nodiscard]] bool ready() const noexcept { return device_ != nullptr && srv_heap_ != nullptr; }

    // A Texture2D view of `levels` mips in slot `index`, every channel as the texture's format gives it.
    void write_view(std::uint32_t index, ID3D12Resource* texture, unsigned levels);
    [[nodiscard]] UploadChunk create_upload_chunk(std::uint64_t size);
    static void release_upload_chunk(UploadChunk& chunk) noexcept;
    // A DEFAULT-heap texture in the copy-destination state.
    [[nodiscard]] gpu::TextureRef create_texture(
        std::uint32_t width, std::uint32_t height, unsigned levels, std::uint64_t& allocation_bytes,
        TexelFormat format = TexelFormat::Rgba8);
    [[nodiscard]] static bool supports_bc1() noexcept { return true; }
    // A texture made by create_texture that the pool may hand out again for the same extent.
    [[nodiscard]] static bool poolable(ID3D12Resource* texture, std::uint32_t& width, std::uint32_t& height,
                                       std::uint32_t& levels);
    // Copies every level from the staging chunk (level l at offset + offsets[l], rows of pitches[l] bytes) and leaves
    // the texture readable by shaders. A reused pooled texture first returns to the copy-destination state.
    void record_upload(ID3D12Resource* texture, bool reused, const UploadChunk& staging, std::uint64_t offset,
                       std::uint32_t width, std::uint32_t height, unsigned levels, const std::uint64_t* offsets,
                       const std::uint32_t* pitches);
    void set_upload_list(ID3D12GraphicsCommandList* list) noexcept { upload_list_ = list; }
    [[nodiscard]] bool has_upload_list() const noexcept { return upload_list_ != nullptr; }
    [[nodiscard]] PlacedTextureAllocator::Stats heap_stats() const { return texture_heap_.stats(); }

    void ensure_view(std::uint32_t index);
    // A view index the store gave up (its texture is gone and the GPU is past the frames that read it): the slot holds the
    // null view again, in the CPU page and in the shader-visible copy.
    void release_view(std::uint32_t index);
    [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE srv_cpu(std::uint32_t index) const noexcept;
    [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE null_srv() const noexcept { return null_srv_; }
    // The shader-visible slots every view is copied into, a view's slot its index (the texture region of the renderer's
    // descriptor heap): the views made so far are copied now, each later one when it is written (write_view,
    // publish_view). A view whose index reaches `count` calls `grow(owner, index)`, which makes a larger heap and calls
    // mirror_into again for it (the earlier heap keeps the views it has for the draws already recorded).
    using GrowFn = void (*)(void* owner, std::uint32_t index);
    void mirror_into(D3D12_CPU_DESCRIPTOR_HANDLE slots, std::uint32_t count, GrowFn grow, void* owner);
    void publish_view(std::uint32_t index);
    // One past the highest view index made so far.
    [[nodiscard]] std::uint32_t views_made() const noexcept { return views_made_; }

private:
    // `count` descriptors from `first` take the null view (a copy, then doubling copies).
    void fill_null(D3D12_CPU_DESCRIPTOR_HANDLE first, std::uint32_t count);
    PlacedTextureAllocator texture_heap_{D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES, 64ull << 20};
    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srv_heap_;
    std::vector<Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>> srv_pages_;
    std::uint32_t views_per_page_ = 0;
    D3D12_CPU_DESCRIPTOR_HANDLE srv_cpu_base_{};
    UINT srv_stride_ = 0u;
    D3D12_CPU_DESCRIPTOR_HANDLE null_srv_{};
    D3D12_CPU_DESCRIPTOR_HANDLE mirror_{};
    std::uint32_t mirror_count_ = 0u;
    GrowFn mirror_grow_ = nullptr;
    void* mirror_owner_ = nullptr;
    std::uint32_t views_made_ = 0u;  // one past the highest view index ensured
    ID3D12GraphicsCommandList* upload_list_ = nullptr;
};

}  // namespace galaxy::render
#endif
