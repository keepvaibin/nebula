#pragma once

// Sub-allocates GPU textures from large D3D12 heaps instead of creating one
// committed resource per texture, which is much slower on some drivers.
//
// A placed resource returns its range when the resource object is destroyed:
// the allocator attaches a small COM object as private data, and D3D12
// releases it with the resource. Callers release a texture only after the GPU
// has finished with it.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>
#include <memory>

namespace galaxy::gx {

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
    // for render targets. Requests larger than half a heap are committed.
    PlacedTextureAllocator(D3D12_HEAP_FLAGS heap_flags, std::uint64_t heap_bytes);
    ~PlacedTextureAllocator();
    PlacedTextureAllocator(const PlacedTextureAllocator&) = delete;
    PlacedTextureAllocator& operator=(const PlacedTextureAllocator&) = delete;

    // Binds the device and creates the first heap (startup, not mid-frame).
    void initialize(ID3D12Device* device);
    // Drops the allocator's heap references; live resources keep theirs.
    void shutdown() noexcept;

    // Creates `desc` in `initial_state`. `allocation_bytes` receives the
    // resource's allocation size for budgeting. A render-target texture is
    // placed over memory with undefined contents: the caller must clear,
    // discard or fully copy it before reading it (D3D12 placed-RT rule).
    HRESULT create(const D3D12_RESOURCE_DESC& desc,
                   D3D12_RESOURCE_STATES initial_state,
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

}  // namespace galaxy::gx
