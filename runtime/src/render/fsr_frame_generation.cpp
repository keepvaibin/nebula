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
#endif

namespace galaxy::render {

#if defined(GALAXY_WITH_FSR3_FG)

bool fsr_frame_generation_compiled() noexcept { return true; }


struct FsrFrameGeneration::Impl {
    FsrGenerationCore core;
    bool unavailable = false;
};

FsrFrameGeneration::FsrFrameGeneration() : impl_(std::make_unique<Impl>()) {}

FsrFrameGeneration::~FsrFrameGeneration() = default;

bool FsrFrameGeneration::replace_swap_chain(ID3D12CommandQueue* queue, Microsoft::WRL::ComPtr<IDXGISwapChain4>& swap_chain) noexcept {
    // The SDK reads the chain's description, window and fullscreen state, and from then on releases the chain on every
    // path (a failed proxy included); before that it keeps it. A chain that cannot give those, or is fullscreen (which the
    // SDK must not release), is refused here and stays with the caller, so it is never leaked half-way.
    DXGI_SWAP_CHAIN_DESC1 desc{};
    DXGI_SWAP_CHAIN_FULLSCREEN_DESC fullscreen{};
    HWND window = nullptr;
    if (FAILED(swap_chain->GetDesc1(&desc)) || FAILED(swap_chain->GetFullscreenDesc(&fullscreen)) ||
        FAILED(swap_chain->GetHwnd(&window)) || fullscreen.Windowed == FALSE) {
        std::cerr << "[fsr3-fg] swap chain: not replaceable\n";
        return false;
    }
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
                               ffxGetResourceDX12(in.depth, ffxGetResourceDescriptionDX12(in.depth), L"Nebula scene depth",
                                                  in.common ? FFX_RESOURCE_STATE_COMMON : FFX_RESOURCE_STATE_DEPTH_ATTACHEMENT),
                               ffxGetResourceDX12(in.motion, ffxGetResourceDescriptionDX12(in.motion), L"Nebula motion vectors",
                                                  in.common ? FFX_RESOURCE_STATE_COMMON : FFX_RESOURCE_STATE_RENDER_TARGET),
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
