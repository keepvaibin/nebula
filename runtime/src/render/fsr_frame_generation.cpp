// SPDX-License-Identifier: GPL-3.0-only
// FSR 3 frame generation on D3D12 (fsr_frame_generation.h).
#include "galaxy/render/fsr_frame_generation.h"

#include "galaxy/render/render_config.h"

#include <iostream>
#include <vector>

#if defined(GALAXY_WITH_FSR3_FG)
#include <FidelityFX/host/backends/dx12/ffx_dx12.h>
#include <FidelityFX/host/ffx_fsr3.h>
#endif

namespace galaxy::render {

#if defined(GALAXY_WITH_FSR3_FG)

bool fsr_frame_generation_compiled() noexcept { return true; }

struct FsrFrameGeneration::Impl {
    FfxFsr3Context context{};
    FfxInterface shared{};
    FfxInterface generation{};
    std::vector<unsigned char> shared_scratch;
    std::vector<unsigned char> generation_scratch;
    bool created = false;
    bool enabled = false;
    bool prepared = false;
    bool reported = false;
    std::uint64_t frame_id = 0u;
    unsigned render_width = 0u;
    unsigned render_height = 0u;
    unsigned display_width = 0u;
    unsigned display_height = 0u;
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
    return impl_->created && impl_->render_width == render_width && impl_->render_height == render_height &&
           impl_->display_width == display_width && impl_->display_height == display_height;
}

bool FsrFrameGeneration::create(ID3D12Device* device, unsigned render_width, unsigned render_height, unsigned display_width,
                                unsigned display_height, DXGI_FORMAT backbuffer_format) noexcept {
    Impl& s = *impl_;
    try {
        const std::size_t size = ffxGetScratchMemorySizeDX12(FFX_FSR3_CONTEXT_COUNT);
        s.shared_scratch.assign(size, 0u);
        s.generation_scratch.assign(size, 0u);
    } catch (...) {
        return false;
    }
    const FfxDevice ffx_device = ffxGetDeviceDX12(device);
    if (ffxGetInterfaceDX12(&s.shared, ffx_device, s.shared_scratch.data(), s.shared_scratch.size(), FFX_FSR3_CONTEXT_COUNT) != FFX_OK ||
        ffxGetInterfaceDX12(&s.generation, ffx_device, s.generation_scratch.data(), s.generation_scratch.size(), FFX_FSR3_CONTEXT_COUNT) != FFX_OK) {
        return false;
    }
    FfxFsr3ContextDescription description{};
    // Any temporal upscaler's scene: its own context is apart. Reversed depth.
    description.flags = FFX_FSR3_ENABLE_INTERPOLATION_ONLY | FFX_FSR3_ENABLE_DEPTH_INVERTED;
    description.maxRenderSize = {render_width, render_height};
    description.maxUpscaleSize = {display_width, display_height};
    description.displaySize = {display_width, display_height};
    description.backendInterfaceSharedResources = s.shared;
    description.backendInterfaceFrameInterpolation = s.generation;
    description.backBufferFormat = ffxGetSurfaceFormatDX12(backbuffer_format);
    const FfxErrorCode result = ffxFsr3ContextCreate(&s.context, &description);
    if (result != FFX_OK) {
        std::cerr << "[fsr3-fg] context: " << result << '\n';
        return false;
    }
    s.created = true;
    s.render_width = render_width;
    s.render_height = render_height;
    s.display_width = display_width;
    s.display_height = display_height;
    std::cerr << "[fsr3-fg] " << render_width << 'x' << render_height << " -> " << display_width << 'x' << display_height << '\n';
    return true;
}

void FsrFrameGeneration::destroy(IDXGISwapChain4* swap_chain) noexcept {
    Impl& s = *impl_;
    if (!s.created) {
        return;
    }
    if (swap_chain != nullptr) {
        configure(swap_chain, false, nullptr, 0u, 0u, 0u, 0u);
        (void)ffxWaitForPresents(ffxGetSwapchainDX12(swap_chain));
    }
    (void)ffxFsr3ContextDestroy(&s.context);
    s.created = false;
    s.enabled = false;
    s.prepared = false;
}

bool FsrFrameGeneration::prepare(const Prepare& in) noexcept {
    Impl& s = *impl_;
    if (!s.created || in.list == nullptr || in.depth == nullptr || in.motion == nullptr) {
        return false;
    }
    ++s.frame_id;
    FfxFsr3DispatchFrameGenerationPrepareDescription d{};
    d.commandList = ffxGetCommandListDX12(in.list);
    d.depth = ffxGetResourceDX12(in.depth, ffxGetResourceDescriptionDX12(in.depth), L"Nebula scene depth", FFX_RESOURCE_STATE_DEPTH_ATTACHEMENT);
    d.motionVectors = ffxGetResourceDX12(in.motion, ffxGetResourceDescriptionDX12(in.motion), L"Nebula motion vectors", FFX_RESOURCE_STATE_RENDER_TARGET);
    d.jitterOffset = {in.jitter_x, in.jitter_y};
    d.motionVectorScale = {1.0f, 1.0f};
    d.renderSize = {in.render_width, in.render_height};
    d.frameTimeDelta = in.frame_ms;
    d.cameraNear = in.camera_near;
    d.cameraFar = in.camera_far;
    d.viewSpaceToMetersFactor = 1.0f;
    d.cameraFovAngleVertical = in.fov_y;
    d.frameID = s.frame_id;
    const FfxErrorCode result = ffxFsr3ContextDispatchFrameGenerationPrepare(&s.context, &d);
    if (result != FFX_OK) {
        if (!s.reported) {
            s.reported = true;
            std::cerr << "[fsr3-fg] prepare: " << result << '\n';
        }
        return false;
    }
    s.prepared = true;
    return true;
}

void FsrFrameGeneration::configure(IDXGISwapChain4* swap_chain, bool enabled, ID3D12Resource* hudless, unsigned left, unsigned top,
                                   unsigned width, unsigned height) noexcept {
    Impl& s = *impl_;
    if (!s.created || swap_chain == nullptr) {
        return;
    }
    const bool on = enabled && s.prepared;
    s.prepared = false;
    FfxFrameGenerationConfig config{};
    config.swapChain = ffxGetSwapchainDX12(swap_chain);
    config.frameGenerationEnabled = on;
    config.frameGenerationCallback = ffxFsr3DispatchFrameGeneration;
    config.frameGenerationCallbackContext = &s.context;
    if (on && hudless != nullptr) {
        config.HUDLessColor = ffxGetResourceDX12(hudless, ffxGetResourceDescriptionDX12(hudless), L"Nebula HUD-less", FFX_RESOURCE_STATE_RENDER_TARGET);
    }
    config.interpolationRect = {static_cast<int32_t>(left), static_cast<int32_t>(top), static_cast<int32_t>(width), static_cast<int32_t>(height)};
    config.frameID = s.frame_id;
    const FfxErrorCode result = ffxFsr3ConfigureFrameGeneration(&s.context, &config);
    if (result != FFX_OK && !s.reported) {
        s.reported = true;
        std::cerr << "[fsr3-fg] configure: " << result << '\n';
    }
    if (on != s.enabled) {
        s.enabled = on;
        std::cerr << "[fsr3-fg] " << (on ? "on" : "off") << '\n';
    }
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
