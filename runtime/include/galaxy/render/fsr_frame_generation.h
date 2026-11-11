// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// AMD FSR 3 frame generation on D3D12: one frame made between each two the game shows, with AMD's frame-pacing swap chain in
// place of the window's. It reads the scene's depth and motion after the upscale, and the frame without its 2D layer at the
// present. Render thread only.

#include "galaxy/windows_api.h"
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <cstdint>
#include <memory>

namespace galaxy::render {

class FsrFrameGeneration {
public:
    FsrFrameGeneration();
    ~FsrFrameGeneration();

    // The swap chain given is released and `swap_chain` becomes AMD's for the same window; on failure it is left null, or as it was when refused before the SDK took it (a chain it cannot describe, or a fullscreen one).
    [[nodiscard]] static bool replace_swap_chain(ID3D12CommandQueue* queue, Microsoft::WRL::ComPtr<IDXGISwapChain4>& swap_chain) noexcept;

    // Its context for these sizes (made at the first frame; the GPU must be idle when they change, and at destroy).
    [[nodiscard]] bool matches(unsigned render_width, unsigned render_height, unsigned display_width, unsigned display_height) const noexcept;
    bool create(ID3D12Device* device, unsigned render_width, unsigned render_height, unsigned display_width, unsigned display_height,
                DXGI_FORMAT backbuffer_format) noexcept;
    void destroy(IDXGISwapChain4* swap_chain) noexcept;

    struct Prepare {
        ID3D12GraphicsCommandList* list = nullptr;
        ID3D12Resource* depth = nullptr;   // depth-write
        ID3D12Resource* motion = nullptr;  // render target, in pixels
        float jitter_x = 0.0f;
        float jitter_y = 0.0f;
        unsigned render_width = 0;
        unsigned render_height = 0;
        float frame_ms = 0.0f;
        float camera_near = 0.0f;
        float camera_far = 0.0f;
        float fov_y = 0.0f;
        bool common = false;  // depth and motion in the common state (TemporalUpscaleInputs::common)
    };
    // This frame's scene, after its upscale.
    bool prepare(const Prepare& in) noexcept;

    // At the present: on or off this frame (on only after a prepare), the frame without its 2D layer (render target; null: none)
    // and the rectangle of the swap chain the game's picture covers.
    void configure(IDXGISwapChain4* swap_chain, bool enabled, ID3D12Resource* hudless, unsigned left, unsigned top, unsigned width,
                   unsigned height) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace galaxy::render
