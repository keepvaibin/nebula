// PlacedTextureAllocator (gpu_placed_textures.h): first-fit ranges in D3D12 heaps, committed fallback.

#include "galaxy/render/gpu_placed_textures.h"
#include "barriers_d3d12.h"

#include <algorithm>
#include <atomic>
#include <map>
#include <mutex>
#include <vector>

namespace galaxy::render {
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
        // The bytes in `free` (a heap's fullness, for allocate).
        std::uint64_t free_bytes = 0;
        // The heap's size.
        std::uint64_t bytes = 0;

        // The smallest free range that holds `size` at `alignment`, or free.end().
        [[nodiscard]] std::map<std::uint64_t, std::uint64_t>::iterator best_fit(std::uint64_t size, std::uint64_t alignment) {
            auto best = free.end();
            for (auto it = free.begin(); it != free.end(); ++it) {
                const std::uint64_t start = it->first;
                if (align_up(start, alignment) + size > start + it->second) continue;
                if (best == free.end() || it->second < best->second) best = it;
            }
            return best;
        }
    };

    D3D12_HEAP_FLAGS flags = D3D12_HEAP_FLAG_NONE;
    // The largest heap. The first is an eighth of it; each new one twice the largest held, so a device holds about what
    // it places, not a whole large heap from the start.
    std::uint64_t heap_bytes = 0;
    D3D12_HEAP_PROPERTIES properties{D3D12_HEAP_TYPE_DEFAULT, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 0u, 0u};
    ComPtr<ID3D12Device> device;
    mutable std::mutex mutex;
    std::vector<Heap> heaps;
    std::uint64_t used = 0;
    std::uint64_t placed = 0;
    std::uint64_t committed = 0;

    // A new heap for a range of `need` bytes (at most heap_bytes / 2): twice the largest heap held (an eighth of
    // heap_bytes when none is), at least twice `need`, at most heap_bytes.
    bool add_heap(std::uint64_t need) {
        if (!device) return false;
        std::uint64_t size = std::max<std::uint64_t>(heap_bytes / 8u, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);
        for (const Heap& held : heaps) {
            if (held.heap) size = std::max(size, held.bytes * 2u);
        }
        while (size < need * 2u) size *= 2u;
        size = std::min(size, heap_bytes);
        D3D12_HEAP_DESC desc{};
        desc.SizeInBytes = size;
        desc.Properties = properties;
        desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
        desc.Flags = flags;
        Heap heap;
        if (FAILED(device->CreateHeap(&desc, IID_PPV_ARGS(&heap.heap)))) return false;
        heap.free.emplace(0u, size);
        heap.free_bytes = size;
        heap.bytes = size;
        // A heap given back (release) leaves its place for the next one: the owners name heaps by index.
        for (Heap& slot : heaps) {
            if (!slot.heap) {
                slot = std::move(heap);
                return true;
            }
        }
        heaps.push_back(std::move(heap));
        return true;
    }

    // The heaps holding memory now.
    [[nodiscard]] std::uint64_t live_heaps() const noexcept {
        std::uint64_t live = 0u;
        for (const Heap& heap : heaps) {
            if (heap.heap) ++live;
        }
        return live;
    }

    // Packed: the fullest heap with room takes the range (the lowest index on a tie), and inside it the smallest free range
    // that fits. Textures come and go with each scene; filling the fullest heap first lets the lightly used ones drain until a
    // heap is empty and release() gives its memory back, where first fit in index order scattered the long-lived textures
    // over every heap and kept all of them (64 MB each) for the rest of the run.
    bool allocate(std::uint64_t size, std::uint64_t alignment,
                  std::size_t& heap_index, std::uint64_t& offset) {
        std::size_t chosen = heaps.size();
        for (std::size_t h = 0; h < heaps.size(); ++h) {
            Heap& heap = heaps[h];
            if (!heap.heap || heap.free_bytes < size) continue;
            if (chosen != heaps.size() && heap.free_bytes >= heaps[chosen].free_bytes) continue;
            if (heap.best_fit(size, alignment) != heap.free.end()) chosen = h;
        }
        if (chosen == heaps.size()) return false;
        Heap& heap = heaps[chosen];
        const auto it = heap.best_fit(size, alignment);
        const std::uint64_t start = it->first;
        const std::uint64_t end = start + it->second;
        const std::uint64_t aligned = align_up(start, alignment);
        heap.free.erase(it);
        if (aligned > start) heap.free.emplace(start, aligned - start);
        if (aligned + size < end) heap.free.emplace(aligned + size, end - aligned - size);
        heap.free_bytes -= size;  // the head left in front of an aligned range stays free
        heap_index = chosen;
        offset = aligned;
        used += size;
        return true;
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
        heaps[heap_index].free_bytes += size;
        used -= std::min(used, size);
        // A heap with nothing placed in it gives its memory back to the adapter (the first stays: textures come back at once).
        // Every range was returned by a resource that is gone, which only happens once the GPU is past the frames that read it.
        if (heap_index != 0u && length == heaps[heap_index].bytes && free.size() == 1u && heaps[heap_index].heap) {
            heaps[heap_index].heap.Reset();
            free.clear();
            heaps[heap_index].free_bytes = 0u;
        }
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
                                               std::uint64_t heap_bytes,
                                               const D3D12_HEAP_PROPERTIES* properties)
    : state_(std::make_shared<State>()) {
    state_->flags = heap_flags;
    state_->heap_bytes = heap_bytes;
    if (properties != nullptr) {
        state_->properties = *properties;
    }
}

PlacedTextureAllocator::~PlacedTextureAllocator() { shutdown(); }

void PlacedTextureAllocator::initialize(ID3D12Device* device) {
    std::lock_guard lock(state_->mutex);
    state_->device = device;
    if (state_->heaps.empty()) (void)state_->add_heap(0u);
}

void PlacedTextureAllocator::shutdown() noexcept {
    // Live placed resources keep the state (and through it their heap) alive.
    std::lock_guard lock(state_->mutex);
    state_->device.Reset();
    state_->heaps.clear();
}

HRESULT PlacedTextureAllocator::create(const D3D12_RESOURCE_DESC& desc,
                                       gpu::Use initial,
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
                (state_->add_heap(info.SizeInBytes) &&
                 state_->allocate(info.SizeInBytes, info.Alignment, heap_index, offset))) {
                heap = state_->heaps[heap_index].heap;
            }
        }
        if (heap) {
            auto* owner = new RangeOwner(state_, heap_index, offset, info.SizeInBytes);
            HRESULT hr = place_texture(device.Get(), heap.Get(), offset, &placed_desc, initial, clear_value,
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

    const D3D12_HEAP_PROPERTIES properties = state_->properties;
    const HRESULT hr = create_texture(device.Get(), &properties, D3D12_HEAP_FLAG_NONE, &desc, initial, clear_value,
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
    stats.heaps = state_->live_heaps();
    for (const State::Heap& heap : state_->heaps) {
        if (heap.heap) stats.heap_bytes += heap.bytes;
    }
    stats.used_bytes = state_->used;
    stats.placed = state_->placed;
    stats.committed_fallbacks = state_->committed;
    return stats;
}

}  // namespace galaxy::render
