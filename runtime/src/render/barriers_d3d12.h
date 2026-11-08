#pragma once

// BarriersD3D12: the changes of use (gpu_use.h) one pass makes to its textures, issued as one ResourceBarrier call;
// undo() issues the way back for a pass that returns its textures to their uses. Legacy states: the enhanced path
// replaces all textures' barriers at once (E13-BARRIERS-DESIGN.md, the spec's legacy layout rule).

#include "galaxy/windows_api.h"
#include "galaxy/render/gpu_use.h"

#include <d3d12.h>

#include <utility>

namespace galaxy::render {

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
    }
    return D3D12_RESOURCE_STATE_COMMON;
}

class BarriersD3D12 {
public:
    explicit BarriersD3D12(ID3D12GraphicsCommandList* list) noexcept : list_(list) {}
    // A change of `resource` from one use to another; nothing when the two are the same state. A full batch is issued
    // first (the order of changes is kept).
    void texture(ID3D12Resource* resource, gpu::Use before, gpu::Use after) noexcept {
        const D3D12_RESOURCE_STATES from = legacy_state(before), to = legacy_state(after);
        if (resource == nullptr || from == to) {
            return;
        }
        if (count_ == kMax) {
            issue();
            count_ = 0u;
        }
        D3D12_RESOURCE_BARRIER& b = barriers_[count_++];
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        b.Transition.pResource = resource;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        b.Transition.StateBefore = from;
        b.Transition.StateAfter = to;
    }
    // A buffer's change (a buffer has no layout: on the enhanced path it is a buffer barrier).
    void buffer(ID3D12Resource* resource, gpu::Use before, gpu::Use after) noexcept { texture(resource, before, after); }
    [[nodiscard]] bool empty() const noexcept { return count_ == 0u; }
    // Issues the changes; they stay recorded for undo().
    void issue() const noexcept {
        if (count_ != 0u) {
            list_->ResourceBarrier(count_, barriers_);
        }
    }
    // Issues the way back (every change reversed) and forgets them.
    void undo() noexcept {
        for (UINT i = 0; i < count_; ++i) {
            std::swap(barriers_[i].Transition.StateBefore, barriers_[i].Transition.StateAfter);
        }
        issue();
        count_ = 0u;
    }
    void clear() noexcept { count_ = 0u; }

private:
    static constexpr UINT kMax = 8u;
    ID3D12GraphicsCommandList* list_;
    D3D12_RESOURCE_BARRIER barriers_[kMax]{};
    UINT count_ = 0u;
};

}  // namespace galaxy::render
