// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// TextureStore's GPU side: shader views by slot, mapped upload staging, textures, and the recorded upload of a
// texture's levels. The cache's logic (keys, maps, aliases, eviction, retirement) is shared; each API implements
// this class (gpu_texture_backend_d3d12.cpp, vk/texture_backend_vk.cpp).

#include "galaxy/render/gpu_types.h"
#include "galaxy/render/texel_format.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

#if defined(NEBULA_GPU_VULKAN)
#include "galaxy/render/vk/texture_backend_vk.h"
#elif defined(NEBULA_GPU_METAL)
#include "galaxy/render/mtl/texture_backend_mtl.h"
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
    // custom write-back heap memory, and staging chunks are write-back memory the CPU also reads.
    [[nodiscard]] bool direct_writes() const noexcept { return direct_; }
    // A texture create_texture made in CPU-visible memory: it is never uploaded again in place.
    [[nodiscard]] static bool direct_written(ID3D12Resource* texture) noexcept;
    [[nodiscard]] static bool supports_bc1() noexcept { return true; }
    [[nodiscard]] static bool supports_astc4x4() noexcept { return false; }
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

    // Uploads on the device's copy queue: a new texture is made in the common state, its levels are copied on that queue
    // (record_upload, a list of its own), and the graphics queue acquires it (common -> shader resource, after a wait on the
    // copy queue's fence) in submit_uploads, before the first frame whose draws name it (note_named, called wherever a handle
    // is given out). A texture already acquired is rewritten on the graphics list as before. Not with direct_writes() (no
    // copy to overlap). `frames_in_flight` sizes the per-slot lists, `frame_slot` is the frame now open. False: the same
    // queue, as before.
    bool enable_async_uploads(unsigned frames_in_flight, unsigned frame_slot);
    [[nodiscard]] bool async_uploads() const noexcept { return copy_queue_ != nullptr; }
    // At the start of a frame in `frame_slot`, after the renderer has waited for the slot's fence: waits until the copy queue
    // has finished what that slot's frame submitted (its staging chunks and retired textures go free next), and starts the
    // slot's lists again.
    void begin_frame(unsigned frame_slot);
    // The frame names `texture` in a draw. Free when no upload is outstanding.
    void note_named(ID3D12Resource* texture) {
        if (!async_.empty()) {
            note_named_slow(texture);
        }
    }
    // The texture is given up (retired): no acquire will name it.
    void forget(ID3D12Resource* texture) {
        if (!async_.empty()) {
            forget_slow(texture);
        }
    }
    // Ends the frame's uploads: the copy list goes to the copy queue (signalling the fence), and when this frame names a
    // texture that queue wrote, `graphics` waits on the fence and the returned list (to execute before the frame's list)
    // acquires those textures. Null when there is nothing to acquire.
    [[nodiscard]] ID3D12CommandList* submit_uploads(ID3D12CommandQueue* graphics);
    // Waits until the copy queue has finished everything sent to it (before textures and staging are released).
    void drain() noexcept;
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
    // A texture made for the copy queue: the copy fence value that carries its latest upload (0: not uploaded yet), and
    // whether a draw named it since.
    struct Async {
        std::uint64_t value = 0u;
        bool named = false;
    };
    void note_named_slow(ID3D12Resource* texture);
    void forget_slow(ID3D12Resource* texture);
    ID3D12GraphicsCommandList* open_copy_list();
    void wait_copy(std::uint64_t value);
    void record_async(ID3D12Resource* texture, Async& state, const UploadChunk& staging, std::uint64_t offset,
                      std::uint32_t width, std::uint32_t height, unsigned levels, const std::uint64_t* offsets,
                      const std::uint32_t* pitches);
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

    // The copy queue (enable_async_uploads): its fence counts the lists it ran; slot_value_[slot] is the value after the
    // slot's last frame, which that slot's staging and retired textures wait for.
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> copy_queue_;
    Microsoft::WRL::ComPtr<ID3D12Fence> copy_fence_;
    HANDLE copy_event_ = nullptr;
    std::uint64_t copy_value_ = 0u;
    std::vector<Microsoft::WRL::ComPtr<ID3D12CommandAllocator>> copy_allocators_;
    std::vector<Microsoft::WRL::ComPtr<ID3D12CommandAllocator>> acquire_allocators_;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> copy_list_;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> acquire_list_;
    std::vector<std::uint64_t> slot_value_;
    unsigned slot_ = 0u;
    bool copy_open_ = false;
    std::unordered_map<ID3D12Resource*, Async> async_;
    std::vector<ID3D12Resource*> named_;  // async_ entries a draw named since the last submit_uploads
};

}  // namespace galaxy::render
#endif
