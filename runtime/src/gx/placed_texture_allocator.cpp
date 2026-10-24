// PlacedTextureAllocator (placed_texture_allocator.h): first-fit ranges in D3D12 heaps, committed fallback.

#include "galaxy/gx/placed_texture_allocator.h"

#include <atomic>
#include <map>
#include <mutex>
#include <vector>

namespace galaxy::gx {
namespace {

using Microsoft::WRL::ComPtr;

// Private-data key of the range owner attached to each placed resource.
constexpr GUID kRangeOwnerGuid = {
    0x6c3f1b0e, 0x2a47, 0x4d0b, {0x9a, 0x51, 0x3e, 0x7d, 0x18, 0x22, 0x5b, 0x91}};

[[nodiscard]] std::uint64_t align_up(std::uint64_t value, std::uint64_t alignment) noexcept {
    return (value + alignment - 1u) & ~(alignment - 1u);
}

}  // namespace

struct PlacedTextureAllocator::State {
    struct Heap {
        ComPtr<ID3D12Heap> heap;
        // Free ranges, offset -> size, coalesced on release.
        std::map<std::uint64_t, std::uint64_t> free;
    };

    D3D12_HEAP_FLAGS flags = D3D12_HEAP_FLAG_NONE;
    std::uint64_t heap_bytes = 0;
    ComPtr<ID3D12Device> device;
    mutable std::mutex mutex;
    std::vector<Heap> heaps;
    std::uint64_t used = 0;
    std::uint64_t placed = 0;
    std::uint64_t committed = 0;

    bool add_heap() {
        if (!device) return false;
        D3D12_HEAP_DESC desc{};
        desc.SizeInBytes = heap_bytes;
        desc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
        desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
        desc.Flags = flags;
        Heap heap;
        if (FAILED(device->CreateHeap(&desc, IID_PPV_ARGS(&heap.heap)))) return false;
        heap.free.emplace(0u, heap_bytes);
        heaps.push_back(std::move(heap));
        return true;
    }

    // First fit over every heap's free ranges.
    bool allocate(std::uint64_t size, std::uint64_t alignment,
                  std::size_t& heap_index, std::uint64_t& offset) {
        for (std::size_t h = 0; h < heaps.size(); ++h) {
            auto& free = heaps[h].free;
            for (auto it = free.begin(); it != free.end(); ++it) {
                const std::uint64_t start = it->first;
                const std::uint64_t end = start + it->second;
                const std::uint64_t aligned = align_up(start, alignment);
                if (aligned + size > end) continue;
                free.erase(it);
                if (aligned > start) free.emplace(start, aligned - start);
                if (aligned + size < end) free.emplace(aligned + size, end - aligned - size);
                heap_index = h;
                offset = aligned;
                used += size;
                return true;
            }
        }
        return false;
    }

    void release(std::size_t heap_index, std::uint64_t offset, std::uint64_t size) noexcept {
        std::lock_guard lock(mutex);
        if (heap_index >= heaps.size()) return;
        auto& free = heaps[heap_index].free;
        std::uint64_t start = offset;
        std::uint64_t length = size;
        auto next = free.lower_bound(offset);
        if (next != free.begin()) {
            auto previous = std::prev(next);
            if (previous->first + previous->second == start) {
                start = previous->first;
                length += previous->second;
                free.erase(previous);
            }
        }
        if (next != free.end() && offset + size == next->first) {
            length += next->second;
            free.erase(next);
        }
        free.emplace(start, length);
        used -= std::min(used, size);
    }
};

namespace {

// Returns a placed resource's range to its heap when D3D12 destroys the
// resource and releases its private data.
class RangeOwner final : public IUnknown {
public:
    RangeOwner(std::shared_ptr<PlacedTextureAllocator::State> state, std::size_t heap,
               std::uint64_t offset, std::uint64_t size) noexcept
        : state_(std::move(state)), heap_(heap), offset_(offset), size_(size) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override {
        if (object == nullptr) return E_POINTER;
        if (riid == __uuidof(IUnknown)) {
            *object = static_cast<IUnknown*>(this);
            AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        return references_.fetch_add(1u, std::memory_order_relaxed) + 1u;
    }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG remaining = references_.fetch_sub(1u, std::memory_order_acq_rel) - 1u;
        if (remaining == 0u) delete this;
        return remaining;
    }

private:
    ~RangeOwner() { state_->release(heap_, offset_, size_); }

    std::atomic<ULONG> references_{1u};
    std::shared_ptr<PlacedTextureAllocator::State> state_;
    std::size_t heap_;
    std::uint64_t offset_;
    std::uint64_t size_;
};

}  // namespace

PlacedTextureAllocator::PlacedTextureAllocator(D3D12_HEAP_FLAGS heap_flags,
                                               std::uint64_t heap_bytes)
    : state_(std::make_shared<State>()) {
    state_->flags = heap_flags;
    state_->heap_bytes = heap_bytes;
}

PlacedTextureAllocator::~PlacedTextureAllocator() { shutdown(); }

void PlacedTextureAllocator::initialize(ID3D12Device* device) {
    std::lock_guard lock(state_->mutex);
    state_->device = device;
    if (state_->heaps.empty()) (void)state_->add_heap();
}

void PlacedTextureAllocator::shutdown() noexcept {
    // Live placed resources keep the state (and through it their heap) alive.
    std::lock_guard lock(state_->mutex);
    state_->device.Reset();
}

HRESULT PlacedTextureAllocator::create(const D3D12_RESOURCE_DESC& desc,
                                       D3D12_RESOURCE_STATES initial_state,
                                       const D3D12_CLEAR_VALUE* clear_value,
                                       ComPtr<ID3D12Resource>& resource,
                                       std::uint64_t& allocation_bytes,
                                       bool* placed) {
    if (placed != nullptr) *placed = false;
    ComPtr<ID3D12Device> device;
    {
        std::lock_guard lock(state_->mutex);
        device = state_->device;
    }
    if (!device) return E_FAIL;

    // Small sampled textures may sit on 4 KB boundaries; everything else on
    // the 64 KB default.
    D3D12_RESOURCE_DESC placed_desc = desc;
    const bool render_target = (desc.Flags & (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET |
                                              D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)) != 0;
    D3D12_RESOURCE_ALLOCATION_INFO info{};
    if (!render_target && desc.SampleDesc.Count <= 1u) {
        placed_desc.Alignment = D3D12_SMALL_RESOURCE_PLACEMENT_ALIGNMENT;
        info = device->GetResourceAllocationInfo(0u, 1u, &placed_desc);
    }
    if (info.Alignment != D3D12_SMALL_RESOURCE_PLACEMENT_ALIGNMENT) {
        placed_desc.Alignment = 0u;
        info = device->GetResourceAllocationInfo(0u, 1u, &placed_desc);
    }
    if (info.SizeInBytes == 0u || info.SizeInBytes == UINT64_MAX) return E_INVALIDARG;
    allocation_bytes = info.SizeInBytes;

    if (info.SizeInBytes <= state_->heap_bytes / 2u) {
        std::size_t heap_index = 0u;
        std::uint64_t offset = 0u;
        ComPtr<ID3D12Heap> heap;
        {
            std::lock_guard lock(state_->mutex);
            if (state_->allocate(info.SizeInBytes, info.Alignment, heap_index, offset) ||
                (state_->add_heap() &&
                 state_->allocate(info.SizeInBytes, info.Alignment, heap_index, offset))) {
                heap = state_->heaps[heap_index].heap;
            }
        }
        if (heap) {
            auto* owner = new RangeOwner(state_, heap_index, offset, info.SizeInBytes);
            HRESULT hr = device->CreatePlacedResource(heap.Get(), offset, &placed_desc,
                                                      initial_state, clear_value,
                                                      IID_PPV_ARGS(&resource));
            if (SUCCEEDED(hr)) hr = resource->SetPrivateDataInterface(kRangeOwnerGuid, owner);
            if (SUCCEEDED(hr)) {
                owner->Release();  // the resource holds the remaining reference
                std::lock_guard lock(state_->mutex);
                ++state_->placed;
                if (placed != nullptr) *placed = true;
                return S_OK;
            }
            resource.Reset();
            owner->Release();  // returns the range
        }
    }

    D3D12_HEAP_PROPERTIES properties{};
    properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    const HRESULT hr = device->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE, &desc,
                                                       initial_state, clear_value,
                                                       IID_PPV_ARGS(&resource));
    if (SUCCEEDED(hr)) {
        std::lock_guard lock(state_->mutex);
        ++state_->committed;
    }
    return hr;
}

PlacedTextureAllocator::Stats PlacedTextureAllocator::stats() const {
    std::lock_guard lock(state_->mutex);
    Stats stats;
    stats.heaps = state_->heaps.size();
    stats.heap_bytes = state_->heaps.size() * state_->heap_bytes;
    stats.used_bytes = state_->used;
    stats.placed = state_->placed;
    stats.committed_fallbacks = state_->committed;
    return stats;
}

}  // namespace galaxy::gx
