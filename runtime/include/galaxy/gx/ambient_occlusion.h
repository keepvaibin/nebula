#pragma once

// Ambient occlusion of the D3D12 renderer's 3D scene.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <d3d12.h>
#include <wrl/client.h>

#include "galaxy/gx/rt_types.h"

#include <array>
#include <cstdint>
#include <vector>

namespace galaxy::gx {

class DescriptorRing;

// Ambient occlusion of the 3D scene (RenderConfig::ambient_occlusion; ambient_occlusion.hlsl): ground-truth ambient
// occlusion from the EFB's depth, multiplied into its colour after the opaque phases, before the translucent ones
// (glass, water, glows and effects stay as drawn). Every perspective scene gets it, the game's baked map geometry
// included; ray-traced lighting has its own. Occlusion is computed at the game's own resolution (one occlusion pixel
// per internal-scale x internal-scale EFB pixels, upsampled by depth), so its cost follows the game's 640x456
// picture, not the internal resolution.
class AmbientOcclusion {
public:
    AmbientOcclusion() = default;
    AmbientOcclusion(const AmbientOcclusion&) = delete;
    AmbientOcclusion& operator=(const AmbientOcclusion&) = delete;
    ~AmbientOcclusion() { shutdown(); }

    // The root signature and compute pipelines (precompiled shaders); the multiply's pipelines are made per sample
    // count at first use. False leaves the pass off.
    bool initialize(ID3D12Device* device);
    void shutdown() noexcept;
    [[nodiscard]] bool ready() const noexcept { return ready_; }

    struct Inputs {
        ID3D12Resource* depth = nullptr;  // the EFB's single-sample depth, in a shader-readable state
        unsigned width = 0;               // its size in pixels
        unsigned height = 0;
        unsigned efb_scale = 1;           // EFB pixels per game pixel
        D3D12_CPU_DESCRIPTOR_HANDLE color_rtv{};  // the EFB's colour (multisampled with MSAA)
        unsigned samples = 1;
        bool hdr = false;                 // the colour is the HDR scene EFB (kEfbHdrColorFormat)
        RtView view{};                    // the scene's projection, depth range and viewport (EFB pixels)
    };
    // Records the occlusion and multiplies it into `color_rtv`'s RGB. Leaves this pass's root signatures, pipeline
    // and render target bound. Returns false when nothing was recorded.
    bool record(ID3D12GraphicsCommandList* list, DescriptorRing& ring, const Inputs& in, std::uint64_t frame_index,
                unsigned frames_in_flight);

private:
    struct Target {
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        unsigned width = 0;
        unsigned height = 0;
        D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    };
    struct Retired {
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        std::uint64_t release_after_frame = 0;
    };
    bool ensure_target(Target& target, DXGI_FORMAT format, unsigned width, unsigned height,
                       std::uint64_t frame_index, unsigned frames_in_flight);
    void transition(ID3D12GraphicsCommandList* list, Target& target, D3D12_RESOURCE_STATES state);
    [[nodiscard]] ID3D12PipelineState* apply_pipeline(unsigned samples, bool hdr, bool view);

    ID3D12Device* device_ = nullptr;
    bool ready_ = false;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_signature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> distance_pass_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> occlusion_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> denoise_;
    // 1, 2, 4, 8 samples; then HDR; then the occlusion view's.
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, 16> apply_{};
    Target distance_;  // eye distance per occlusion pixel (R32F)
    Target raw_;       // occlusion, eye distance (R16G16F)
    Target denoised_;
    std::vector<Retired> retired_;
};

}  // namespace galaxy::gx
