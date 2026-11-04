// SPDX-License-Identifier: GPL-3.0-only
// FSR 3 frame generation on D3D12 (fsr_frame_generation.h).
#include "galaxy/render/fsr_frame_generation.h"

#include "galaxy/render/render_config.h"

#include <iostream>
#include <utility>
#include <vector>

#if defined(GALAXY_WITH_FSR3_FG)
#include <FidelityFX/host/backends/dx12/ffx_dx12.h>
#include <FidelityFX/host/ffx_fsr3.h>

#include "fsr_frame_generation_core.inl"
#include "galaxy/render/temporal_dump.h"
#endif

namespace galaxy::render {

#if defined(GALAXY_WITH_FSR3_FG)

bool fsr_frame_generation_compiled() noexcept { return true; }

#if !defined(NEBULA_RELEASE)
namespace {

D3D12_RESOURCE_STATES d3d_state(FfxResourceStates state) {
    switch (state) {
    case FFX_RESOURCE_STATE_UNORDERED_ACCESS: return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    case FFX_RESOURCE_STATE_COMPUTE_READ: return D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    case FFX_RESOURCE_STATE_PIXEL_READ: return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    case FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ: return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    case FFX_RESOURCE_STATE_COPY_SRC: return D3D12_RESOURCE_STATE_COPY_SOURCE;
    case FFX_RESOURCE_STATE_COPY_DEST: return D3D12_RESOURCE_STATE_COPY_DEST;
    case FFX_RESOURCE_STATE_RENDER_TARGET: return D3D12_RESOURCE_STATE_RENDER_TARGET;
    default: return D3D12_RESOURCE_STATE_COMMON;
    }
}

struct PendingDump {
    Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
    const char* name;
    std::uint64_t index;
    unsigned width;
    unsigned height;
    UINT pitch;
};

void dump_generated_frame(FfxCommandList commands, const FfxResource& real, const FfxResource& made, std::uint64_t index) {
    static const temporal_dump::Plan plan = temporal_dump::read_plan();
    static std::vector<PendingDump> pending;
    static std::uint64_t written_after = 0u;
    if (plan.every == 0u) {
        return;
    }
    // Finished by now: frames in flight are far fewer.
    if (!pending.empty() && index >= written_after) {
        for (PendingDump& p : pending) {
            void* mapped = nullptr;
            if (SUCCEEDED(p.buffer->Map(0u, nullptr, &mapped))) {
                temporal_dump::write(plan, p.name, p.index, p.width, p.height, 4u, mapped, p.pitch);
                p.buffer->Unmap(0u, nullptr);
            }
        }
        pending.clear();
    }
    if (!plan.wanted(index)) {
        return;
    }
    auto* const list = reinterpret_cast<ID3D12GraphicsCommandList*>(commands);
    const auto copy = [&](const FfxResource& picture, const char* name) {
        auto* const resource = static_cast<ID3D12Resource*>(picture.resource);
        Microsoft::WRL::ComPtr<ID3D12Device> device;
        if (resource == nullptr || FAILED(resource->GetDevice(IID_PPV_ARGS(&device)))) {
            return;
        }
        const D3D12_RESOURCE_DESC desc = resource->GetDesc();
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        UINT64 bytes = 0u;
        device->GetCopyableFootprints(&desc, 0u, 1u, 0u, &footprint, nullptr, nullptr, &bytes);
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC buffer_desc{};
        buffer_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        buffer_desc.Width = bytes;
        buffer_desc.Height = 1u;
        buffer_desc.DepthOrArraySize = 1u;
        buffer_desc.MipLevels = 1u;
        buffer_desc.SampleDesc.Count = 1u;
        buffer_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        PendingDump dump{};
        if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer_desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&dump.buffer)))) {
            return;
        }
        const D3D12_RESOURCE_STATES state = d3d_state(picture.state);
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = resource;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = state;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        list->ResourceBarrier(1u, &barrier);
        D3D12_TEXTURE_COPY_LOCATION to{};
        to.pResource = dump.buffer.Get();
        to.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        to.PlacedFootprint = footprint;
        D3D12_TEXTURE_COPY_LOCATION from{};
        from.pResource = resource;
        from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        list->CopyTextureRegion(&to, 0u, 0u, 0u, &from, nullptr);
        std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
        list->ResourceBarrier(1u, &barrier);
        dump.name = name;
        dump.index = index;
        dump.width = static_cast<unsigned>(desc.Width);
        dump.height = desc.Height;
        dump.pitch = footprint.Footprint.RowPitch;
        pending.push_back(std::move(dump));
    };
    copy(real, "real");
    copy(made, "made");
    written_after = index + 8u;
}

}  // namespace
#endif

struct FsrFrameGeneration::Impl {
    FsrGenerationCore core;
    bool unavailable = false;
};

FsrFrameGeneration::FsrFrameGeneration() : impl_(std::make_unique<Impl>()) {}

FsrFrameGeneration::~FsrFrameGeneration() = default;

bool FsrFrameGeneration::replace_swap_chain(ID3D12CommandQueue* queue, Microsoft::WRL::ComPtr<IDXGISwapChain4>& swap_chain) noexcept {
    FfxSwapchain ffx = ffxGetSwapchainDX12(swap_chain.Detach());
    const FfxErrorCode result = ffxReplaceSwapchainForFrameinterpolationDX12(ffxGetCommandQueueDX12(queue), ffx);
    if (result != FFX_OK) {
        std::cerr << "[fsr3-fg] swap chain: " << result << '\n';
        return false;
    }
    swap_chain.Attach(ffxGetDX12SwapchainPtr(ffx));
    return true;
}

bool FsrFrameGeneration::matches(unsigned render_width, unsigned render_height, unsigned display_width, unsigned display_height) const noexcept {
    return impl_->core.matches(render_width, render_height, display_width, display_height);
}

bool FsrFrameGeneration::create(ID3D12Device* device, unsigned render_width, unsigned render_height, unsigned display_width,
                                unsigned display_height, DXGI_FORMAT backbuffer_format) noexcept {
    Impl& s = *impl_;
    if (s.unavailable) {
        return false;
    }
    FsrGenerationCore& core = s.core;
#if !defined(NEBULA_RELEASE)
    core.dump_generated = dump_generated_frame;
#endif
    try {
        const std::size_t size = ffxGetScratchMemorySizeDX12(FFX_FSR3_CONTEXT_COUNT);
        core.shared_scratch.assign(size, 0u);
        core.generation_scratch.assign(size, 0u);
    } catch (...) {
        return false;
    }
    const FfxDevice ffx_device = ffxGetDeviceDX12(device);
    s.unavailable = ffxGetInterfaceDX12(&core.shared, ffx_device, core.shared_scratch.data(), core.shared_scratch.size(), FFX_FSR3_CONTEXT_COUNT) != FFX_OK ||
                    ffxGetInterfaceDX12(&core.generation, ffx_device, core.generation_scratch.data(), core.generation_scratch.size(), FFX_FSR3_CONTEXT_COUNT) != FFX_OK ||
                    !core.create(render_width, render_height, display_width, display_height, ffxGetSurfaceFormatDX12(backbuffer_format),
                                 ffxGetSurfaceFormatDX12(backbuffer_format));
    return !s.unavailable;
}

void FsrFrameGeneration::destroy(IDXGISwapChain4* swap_chain) noexcept {
    FsrGenerationCore& core = impl_->core;
    if (!core.created()) {
        return;
    }
    if (swap_chain != nullptr) {
        core.configure(ffxGetSwapchainDX12(swap_chain), false, FfxResource{}, 0u, 0u, 0u, 0u);
        (void)ffxWaitForPresents(ffxGetSwapchainDX12(swap_chain));
    }
    core.destroy();
}

bool FsrFrameGeneration::prepare(const Prepare& in) noexcept {
    if (in.list == nullptr || in.depth == nullptr || in.motion == nullptr) {
        return false;
    }
    return impl_->core.prepare(ffxGetCommandListDX12(in.list),
                               ffxGetResourceDX12(in.depth, ffxGetResourceDescriptionDX12(in.depth), L"Nebula scene depth", FFX_RESOURCE_STATE_DEPTH_ATTACHEMENT),
                               ffxGetResourceDX12(in.motion, ffxGetResourceDescriptionDX12(in.motion), L"Nebula motion vectors", FFX_RESOURCE_STATE_RENDER_TARGET),
                               in.jitter_x, in.jitter_y, in.render_width, in.render_height, in.frame_ms, in.camera_near, in.camera_far, in.fov_y);
}

void FsrFrameGeneration::configure(IDXGISwapChain4* swap_chain, bool enabled, ID3D12Resource* hudless, unsigned left, unsigned top,
                                   unsigned width, unsigned height) noexcept {
    if (swap_chain == nullptr) {
        return;
    }
    FfxResource picture{};
    if (hudless != nullptr) {
        picture = ffxGetResourceDX12(hudless, ffxGetResourceDescriptionDX12(hudless), L"Nebula HUD-less", FFX_RESOURCE_STATE_RENDER_TARGET);
    }
    impl_->core.configure(ffxGetSwapchainDX12(swap_chain), enabled, picture, left, top, width, height);
}

#else

bool fsr_frame_generation_compiled() noexcept { return false; }

struct FsrFrameGeneration::Impl {};

FsrFrameGeneration::FsrFrameGeneration() = default;
FsrFrameGeneration::~FsrFrameGeneration() = default;
bool FsrFrameGeneration::replace_swap_chain(ID3D12CommandQueue*, Microsoft::WRL::ComPtr<IDXGISwapChain4>&) noexcept { return false; }
bool FsrFrameGeneration::matches(unsigned, unsigned, unsigned, unsigned) const noexcept { return false; }
bool FsrFrameGeneration::create(ID3D12Device*, unsigned, unsigned, unsigned, unsigned, DXGI_FORMAT) noexcept { return false; }
void FsrFrameGeneration::destroy(IDXGISwapChain4*) noexcept {}
bool FsrFrameGeneration::prepare(const Prepare&) noexcept { return false; }
void FsrFrameGeneration::configure(IDXGISwapChain4*, bool, ID3D12Resource*, unsigned, unsigned, unsigned, unsigned) noexcept {}

#endif

}  // namespace galaxy::render
