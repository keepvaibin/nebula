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
    // A DEFAULT-heap texture in the copy-destination state. `writable`: a texture whose levels are uploaded once and never
    // rewritten while the GPU may read them; with direct_writes() it is made in CPU-visible memory instead (born in the
    // shader-resource state), and record_upload writes it from the CPU with no command.
    [[nodiscard]] gpu::TextureRef create_texture(
        std::uint32_t width, std::uint32_t height, unsigned levels, std::uint64_t& allocation_bytes,
        TexelFormat format = TexelFormat::Rgba8, bool writable = false);
    // True on an adapter with one memory shared by the CPU and the GPU and cache coherence between them
    // (D3D12_FEATURE_DATA_ARCHITECTURE UMA and CacheCoherentUMA): textures are written from the CPU (WriteToSubresource) into
    // custom write-back heap memory, and staging chunks are write-back memory the CPU also reads. GALAXY_D3D12_TEST_NO_FEATURE
    // names uma_writes to force the copy path.
    [[nodiscard]] bool direct_writes() const noexcept { return direct_; }
    // A texture create_texture made in CPU-visible memory: it is never uploaded again in place.
    [[nodiscard]] static bool direct_written(ID3D12Resource* texture) noexcept;
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
    [[nodiscard]] PlacedTextureAllocator::Stats heap_stats() const {
        PlacedTextureAllocator::Stats stats = texture_heap_.stats();
        if (direct_) {
            const PlacedTextureAllocator::Stats direct = direct_heap_.stats();
            stats.heaps += direct.heaps;
            stats.heap_bytes += direct.heap_bytes;
            stats.used_bytes += direct.used_bytes;
            stats.placed += direct.placed;
            stats.committed_fallbacks += direct.committed_fallbacks;
        }
        return stats;
    }

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
    // The CPU-visible texture heaps of direct_writes(): system memory, write-back (valid with CacheCoherentUMA).
    static constexpr D3D12_HEAP_PROPERTIES kDirectProperties{D3D12_HEAP_TYPE_CUSTOM, D3D12_CPU_PAGE_PROPERTY_WRITE_BACK,
                                                             D3D12_MEMORY_POOL_L0, 0u, 0u};
    PlacedTextureAllocator direct_heap_{D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES, 64ull << 20, &kDirectProperties};
    bool direct_ = false;
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
