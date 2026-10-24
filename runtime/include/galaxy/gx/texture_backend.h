// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// TextureCache's GPU side: shader views by slot, mapped upload staging, textures, and the recorded upload of a
// texture's levels. The cache's logic (keys, maps, aliases, eviction, retirement) is shared; each API implements
// this class (texture_backend_d3d12.cpp, vk/texture_backend_vk.cpp).

#include "galaxy/gx/gpu_types.h"

#include <cstdint>

#if defined(NEBULA_GPU_VULKAN)
#include "galaxy/gx/vk/texture_backend_vk.h"
#else
#include "galaxy/gx/placed_texture_allocator.h"

namespace galaxy::gx {

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

    // `view_capacity` slots, then the permanent null view.
    bool initialize(gpu::Device* device, std::uint32_t view_capacity);
    void shutdown();
    [[nodiscard]] bool ready() const noexcept { return device_ != nullptr && srv_heap_ != nullptr; }

    // A Texture2D view of `levels` mips in slot `index`: RGBA8, or a one-byte texture's value in every channel.
    void write_view(std::uint32_t index, ID3D12Resource* texture, unsigned levels);
    [[nodiscard]] UploadChunk create_upload_chunk(std::uint64_t size);
    static void release_upload_chunk(UploadChunk& chunk) noexcept;
    // A DEFAULT-heap texture in the copy-destination state: RGBA8, or one byte per texel (`grey`, a movie plane).
    [[nodiscard]] gpu::TextureRef create_texture(
        std::uint32_t width, std::uint32_t height, unsigned levels, std::uint64_t& allocation_bytes, bool grey = false);
    // A texture made by create_texture that the pool may hand out again for the same extent.
    [[nodiscard]] static bool poolable(ID3D12Resource* texture, std::uint32_t& width, std::uint32_t& height,
                                       std::uint32_t& levels);
    // EFB copies alias only single-level RGBA8 textures.
    [[nodiscard]] static bool alias_compatible(ID3D12Resource* texture, std::uint32_t& width, std::uint32_t& height);
    // Copies every level from the staging chunk (level l at offset + offsets[l], rows of pitches[l] bytes) and leaves
    // the texture readable by shaders. A reused pooled texture first returns to the copy-destination state.
    void record_upload(ID3D12Resource* texture, bool reused, const UploadChunk& staging, std::uint64_t offset,
                       std::uint32_t width, std::uint32_t height, unsigned levels, const std::uint64_t* offsets,
                       const std::uint32_t* pitches);
    void set_upload_list(ID3D12GraphicsCommandList* list) noexcept { upload_list_ = list; }
    [[nodiscard]] bool has_upload_list() const noexcept { return upload_list_ != nullptr; }
    [[nodiscard]] PlacedTextureAllocator::Stats heap_stats() const { return texture_heap_.stats(); }

    [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE srv_cpu_base() const noexcept { return srv_cpu_base_; }
    [[nodiscard]] UINT srv_descriptor_stride() const noexcept { return srv_stride_; }
    [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE null_srv() const noexcept { return null_srv_; }

private:
    PlacedTextureAllocator texture_heap_{D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES, 64ull << 20};
    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srv_heap_;
    D3D12_CPU_DESCRIPTOR_HANDLE srv_cpu_base_{};
    UINT srv_stride_ = 0u;
    D3D12_CPU_DESCRIPTOR_HANDLE null_srv_{};
    ID3D12GraphicsCommandList* upload_list_ = nullptr;
};

}  // namespace galaxy::gx
#endif
