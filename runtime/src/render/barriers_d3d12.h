#pragma once

// BarriersD3D12: the changes of use (gpu_use.h) one pass makes to its resources, issued together; undo() issues the
// way back. Enhanced barriers (ID3D12GraphicsCommandList7::Barrier: sync, access and layout) when the device has them
// (D3D12_OPTIONS12; BarrierPath), legacy state transitions otherwise. Every texture the renderer makes goes through
// create_texture, so it starts in a layout an enhanced barrier can name; the SDKs (FSR, XeSS, Streamline) are handed
// their textures in render-target and depth-write states, whose layouts are the same in both models.
// E13-BARRIERS-DESIGN.md.

#include "galaxy/windows_api.h"
#include "galaxy/render/gpu_use.h"

#include <d3d12.h>

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

namespace galaxy::render {

// The device's barrier model, chosen once when the device is made (renderer_d3d12.cpp): enhanced when `device` is set.
struct BarrierPath {
    static inline ID3D12Device10* device = nullptr;
    [[nodiscard]] static bool enhanced() noexcept { return device != nullptr; }
};

[[nodiscard]] constexpr D3D12_RESOURCE_STATES legacy_state(gpu::Use use) noexcept {
    switch (use) {
    case gpu::Use::ColorWrite: return D3D12_RESOURCE_STATE_RENDER_TARGET;
    case gpu::Use::DepthWrite: return D3D12_RESOURCE_STATE_DEPTH_WRITE;
    case gpu::Use::DepthRead: return D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    case gpu::Use::PixelRead: return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    case gpu::Use::ComputeRead: return D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    case gpu::Use::ShaderRead:
        return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    case gpu::Use::ComputeWrite: return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    case gpu::Use::CopySource: return D3D12_RESOURCE_STATE_COPY_SOURCE;
    case gpu::Use::CopyDest: return D3D12_RESOURCE_STATE_COPY_DEST;
    case gpu::Use::ResolveSource: return D3D12_RESOURCE_STATE_RESOLVE_SOURCE;
    case gpu::Use::ResolveDest: return D3D12_RESOURCE_STATE_RESOLVE_DEST;
    case gpu::Use::Present: return D3D12_RESOURCE_STATE_PRESENT;
    case gpu::Use::Common: return D3D12_RESOURCE_STATE_COMMON;
    }
    return D3D12_RESOURCE_STATE_COMMON;
}

// A use as an enhanced barrier names it: the work that touches the resource, how, and (textures) the layout.
struct EnhancedUse {
    D3D12_BARRIER_SYNC sync;
    D3D12_BARRIER_ACCESS access;
    D3D12_BARRIER_LAYOUT layout;
};

[[nodiscard]] constexpr EnhancedUse enhanced_use(gpu::Use use) noexcept {
    switch (use) {
    case gpu::Use::ColorWrite:
        return {D3D12_BARRIER_SYNC_RENDER_TARGET, D3D12_BARRIER_ACCESS_RENDER_TARGET, D3D12_BARRIER_LAYOUT_RENDER_TARGET};
    case gpu::Use::DepthWrite:
        return {D3D12_BARRIER_SYNC_DEPTH_STENCIL, D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE,
                D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE};
    case gpu::Use::DepthRead:
        return {D3D12_BARRIER_SYNC_DEPTH_STENCIL | D3D12_BARRIER_SYNC_PIXEL_SHADING,
                D3D12_BARRIER_ACCESS_DEPTH_STENCIL_READ | D3D12_BARRIER_ACCESS_SHADER_RESOURCE,
                D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_READ};
    case gpu::Use::PixelRead:
        return {D3D12_BARRIER_SYNC_PIXEL_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE,
                D3D12_BARRIER_LAYOUT_SHADER_RESOURCE};
    case gpu::Use::ComputeRead:
        return {D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE,
                D3D12_BARRIER_LAYOUT_SHADER_RESOURCE};
    case gpu::Use::ShaderRead:
        return {D3D12_BARRIER_SYNC_ALL_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE,
                D3D12_BARRIER_LAYOUT_SHADER_RESOURCE};
    case gpu::Use::ComputeWrite:
        return {D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_UNORDERED_ACCESS,
                D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS};
    case gpu::Use::CopySource:
        return {D3D12_BARRIER_SYNC_COPY, D3D12_BARRIER_ACCESS_COPY_SOURCE, D3D12_BARRIER_LAYOUT_COPY_SOURCE};
    case gpu::Use::CopyDest:
        return {D3D12_BARRIER_SYNC_COPY, D3D12_BARRIER_ACCESS_COPY_DEST, D3D12_BARRIER_LAYOUT_COPY_DEST};
    case gpu::Use::ResolveSource:
        return {D3D12_BARRIER_SYNC_RESOLVE, D3D12_BARRIER_ACCESS_RESOLVE_SOURCE, D3D12_BARRIER_LAYOUT_RESOLVE_SOURCE};
    case gpu::Use::ResolveDest:
        return {D3D12_BARRIER_SYNC_RESOLVE, D3D12_BARRIER_ACCESS_RESOLVE_DEST, D3D12_BARRIER_LAYOUT_RESOLVE_DEST};
    case gpu::Use::Present:
        return {D3D12_BARRIER_SYNC_NONE, D3D12_BARRIER_ACCESS_NO_ACCESS, D3D12_BARRIER_LAYOUT_PRESENT};
    case gpu::Use::Common:
        // The legacy COMMON state's equivalent: an SDK given a texture here records its own legacy barriers on it,
        // which the enhanced model refuses after a SyncAfter of NONE (debug layer id 1417).
        return {D3D12_BARRIER_SYNC_ALL, D3D12_BARRIER_ACCESS_COMMON, D3D12_BARRIER_LAYOUT_COMMON};
    }
    return {D3D12_BARRIER_SYNC_NONE, D3D12_BARRIER_ACCESS_NO_ACCESS, D3D12_BARRIER_LAYOUT_COMMON};
}

// A committed texture starting in `initial`: with its layout on the enhanced path (CreateCommittedResource3), with the
// legacy state otherwise. Buffers keep CreateCommittedResource (they have no layout).
[[nodiscard]] inline HRESULT create_texture(ID3D12Device* device, const D3D12_HEAP_PROPERTIES* heap, D3D12_HEAP_FLAGS flags,
                                            const D3D12_RESOURCE_DESC* desc, gpu::Use initial,
                                            const D3D12_CLEAR_VALUE* clear, REFIID riid, void** out) noexcept {
    if (BarrierPath::device != nullptr) {
        D3D12_RESOURCE_DESC1 desc1{};
        desc1.Dimension = desc->Dimension;
        desc1.Alignment = desc->Alignment;
        desc1.Width = desc->Width;
        desc1.Height = desc->Height;
        desc1.DepthOrArraySize = desc->DepthOrArraySize;
        desc1.MipLevels = desc->MipLevels;
        desc1.Format = desc->Format;
        desc1.SampleDesc = desc->SampleDesc;
        desc1.Layout = desc->Layout;
        desc1.Flags = desc->Flags;
        return BarrierPath::device->CreateCommittedResource3(heap, flags, &desc1, enhanced_use(initial).layout, clear,
                                                             nullptr, 0u, nullptr, riid, out);
    }
    return device->CreateCommittedResource(heap, flags, desc, legacy_state(initial), clear, riid, out);
}

// The same for a texture placed in a heap (CreatePlacedResource2 on the enhanced path).
[[nodiscard]] inline HRESULT place_texture(ID3D12Device* device, ID3D12Heap* heap, UINT64 offset,
                                           const D3D12_RESOURCE_DESC* desc, gpu::Use initial,
                                           const D3D12_CLEAR_VALUE* clear, REFIID riid, void** out) noexcept {
    if (BarrierPath::device != nullptr) {
        D3D12_RESOURCE_DESC1 desc1{};
        desc1.Dimension = desc->Dimension;
        desc1.Alignment = desc->Alignment;
        desc1.Width = desc->Width;
        desc1.Height = desc->Height;
        desc1.DepthOrArraySize = desc->DepthOrArraySize;
        desc1.MipLevels = desc->MipLevels;
        desc1.Format = desc->Format;
        desc1.SampleDesc = desc->SampleDesc;
        desc1.Layout = desc->Layout;
        desc1.Flags = desc->Flags;
        return BarrierPath::device->CreatePlacedResource2(heap, offset, &desc1, enhanced_use(initial).layout, clear,
                                                          0u, nullptr, riid, out);
    }
    return device->CreatePlacedResource(heap, offset, desc, legacy_state(initial), clear, riid, out);
}

class BarriersD3D12 {
public:
    explicit BarriersD3D12(ID3D12GraphicsCommandList* list) noexcept : list_(list) {}
    BarriersD3D12(const BarriersD3D12&) = delete;
    BarriersD3D12& operator=(const BarriersD3D12&) = delete;
    ~BarriersD3D12() {
        if (list7_ != nullptr) {
            list7_->Release();
        }
    }
    // A change of a texture from one use to another; nothing when the two are the same.
    void texture(ID3D12Resource* resource, gpu::Use before, gpu::Use after) { add(resource, before, after, false); }
    // A buffer's change (a buffer has no layout: on the enhanced path it is a buffer barrier).
    void buffer(ID3D12Resource* resource, gpu::Use before, gpu::Use after) { add(resource, before, after, true); }
    [[nodiscard]] bool empty() const noexcept { return count_ == 0u; }
    // Issues the changes in the order they were recorded; they stay recorded for undo().
    void issue() {
        if (count_ == 0u) {
            return;
        }
        if (BarrierPath::enhanced() && list7_ == nullptr) {
            (void)list_->QueryInterface(IID_PPV_ARGS(&list7_));
        }
        if (list7_ != nullptr) {
            issue_enhanced();
        } else {
            issue_legacy();
        }
    }
    // Issues the way back (every change reversed, in reverse order) and forgets them.
    void undo() {
        for (std::uint32_t i = 0; i < count_; ++i) {
            Change& c = at(i);
            std::swap(c.before, c.after);
        }
        for (std::uint32_t i = 0, j = count_ - 1u; count_ != 0u && i < j; ++i, --j) {
            std::swap(at(i), at(j));
        }
        issue();
        count_ = 0u;
    }
    void clear() noexcept { count_ = 0u; }

private:
    struct Change {
        ID3D12Resource* resource;
        gpu::Use before;
        gpu::Use after;
        bool buffer;
    };
    static constexpr std::uint32_t kInline = 16u;

    void add(ID3D12Resource* resource, gpu::Use before, gpu::Use after, bool buffer) {
        if (resource == nullptr || before == after) {
            return;
        }
        if (count_ < kInline) {
            inline_[count_] = {resource, before, after, buffer};
        } else {
            more_.resize(count_ - kInline + 1u);
            more_[count_ - kInline] = {resource, before, after, buffer};
        }
        ++count_;
    }
    [[nodiscard]] Change& at(std::uint32_t i) noexcept { return i < kInline ? inline_[i] : more_[i - kInline]; }

    void issue_legacy() {
        std::array<D3D12_RESOURCE_BARRIER, kInline> out{};
        UINT n = 0u;
        for (std::uint32_t i = 0; i < count_; ++i) {
            const Change& c = at(i);
            const D3D12_RESOURCE_STATES from = legacy_state(c.before), to = legacy_state(c.after);
            if (from == to) {
                continue;
            }
            D3D12_RESOURCE_BARRIER& b = out[n++];
            b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            b.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
            b.Transition.pResource = c.resource;
            b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            b.Transition.StateBefore = from;
            b.Transition.StateAfter = to;
            if (n == kInline) {
                list_->ResourceBarrier(n, out.data());
                n = 0u;
            }
        }
        if (n != 0u) {
            list_->ResourceBarrier(n, out.data());
        }
    }

    void issue_enhanced() {
        std::array<D3D12_TEXTURE_BARRIER, kInline> textures{};
        std::array<D3D12_BUFFER_BARRIER, kInline> buffers{};
        UINT t = 0u, b = 0u;
        const auto flush = [&] {
            std::array<D3D12_BARRIER_GROUP, 2> groups{};
            UINT g = 0u;
            if (b != 0u) {
                groups[g].Type = D3D12_BARRIER_TYPE_BUFFER;
                groups[g].NumBarriers = b;
                groups[g].pBufferBarriers = buffers.data();
                ++g;
            }
            if (t != 0u) {
                groups[g].Type = D3D12_BARRIER_TYPE_TEXTURE;
                groups[g].NumBarriers = t;
                groups[g].pTextureBarriers = textures.data();
                ++g;
            }
            if (g != 0u) {
                list7_->Barrier(g, groups.data());
            }
            t = 0u;
            b = 0u;
        };
        for (std::uint32_t i = 0; i < count_; ++i) {
            const Change& c = at(i);
            const EnhancedUse from = enhanced_use(c.before), to = enhanced_use(c.after);
            if (c.buffer) {
                D3D12_BUFFER_BARRIER& out = buffers[b++];
                out.SyncBefore = from.sync;
                out.SyncAfter = to.sync;
                out.AccessBefore = from.access;
                out.AccessAfter = to.access;
                out.pResource = c.resource;
                out.Offset = 0u;
                out.Size = UINT64_MAX;
            } else {
                D3D12_TEXTURE_BARRIER& out = textures[t++];
                out.SyncBefore = from.sync;
                out.SyncAfter = to.sync;
                out.AccessBefore = from.access;
                out.AccessAfter = to.access;
                out.LayoutBefore = from.layout;
                out.LayoutAfter = to.layout;
                out.pResource = c.resource;
                out.Subresources = {0xffffffffu, 0u, 0u, 0u, 0u, 0u};
                out.Flags = D3D12_TEXTURE_BARRIER_FLAG_NONE;
            }
            if (t == kInline || b == kInline) {
                flush();
            }
        }
        flush();
    }

    ID3D12GraphicsCommandList* list_;
    ID3D12GraphicsCommandList7* list7_ = nullptr;
    std::array<Change, kInline> inline_{};
    std::vector<Change> more_;
    std::uint32_t count_ = 0u;
};

}  // namespace galaxy::render
