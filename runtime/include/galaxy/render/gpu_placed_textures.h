#pragma once

// Sub-allocates GPU textures from large D3D12 heaps instead of creating one
// committed resource per texture, which is much slower on some drivers.
//
// A placed resource returns its range when the resource object is destroyed:
// the allocator attaches a small COM object as private data, and D3D12
// releases it with the resource. Callers release a texture only after the GPU
// has finished with it.

#include "galaxy/render/gpu_use.h"
#include "galaxy/windows_api.h"
#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>
#include <memory>

namespace galaxy::render {

class PlacedTextureAllocator {
public:
    struct Stats {
        std::uint64_t heaps = 0;
        std::uint64_t heap_bytes = 0;
        std::uint64_t used_bytes = 0;
        std::uint64_t placed = 0;
        std::uint64_t committed_fallbacks = 0;
    };

    // `heap_flags` selects the resource class: D3D12_HEAP_FLAG_ALLOW_ONLY_
    // NON_RT_DS_TEXTURES for sampled textures, ..._ALLOW_ONLY_RT_DS_TEXTURES
    // for render targets. `heap_bytes` is the largest heap: the first is an eighth of it, each further one twice the
    // largest held. Requests larger than half of `heap_bytes` are committed. `properties`: the heaps' (and the committed
    // fallback's) properties; none means the DEFAULT heap type. A custom CPU-visible heap is for WriteToSubresource.
    PlacedTextureAllocator(D3D12_HEAP_FLAGS heap_flags, std::uint64_t heap_bytes,
                           const D3D12_HEAP_PROPERTIES* properties = nullptr);
    ~PlacedTextureAllocator();
    PlacedTextureAllocator(const PlacedTextureAllocator&) = delete;
    PlacedTextureAllocator& operator=(const PlacedTextureAllocator&) = delete;

    // Binds the device and creates the first heap (startup, not mid-frame).
    void initialize(ID3D12Device* device);
    // Drops the allocator's heap references; live resources keep theirs.
    void shutdown() noexcept;

    // Creates `desc` in `initial` (its layout or legacy state: barriers_d3d12.h). `allocation_bytes` receives the
    // resource's allocation size for budgeting. A render-target texture is
    // placed over memory with undefined contents: the caller must clear,
    // discard or fully copy it before reading it (D3D12 placed-RT rule).
    HRESULT create(const D3D12_RESOURCE_DESC& desc,
                   gpu::Use initial,
                   const D3D12_CLEAR_VALUE* clear_value,
                   Microsoft::WRL::ComPtr<ID3D12Resource>& resource,
                   std::uint64_t& allocation_bytes,
                   bool* placed = nullptr);

    [[nodiscard]] Stats stats() const;

    // Shared with each placed resource's range owner, so heaps outlive the
    // resources placed in them.
    struct State;

private:
    std::shared_ptr<State> state_;
};

}  // namespace galaxy::render
