#pragma once

// Ambient occlusion of the D3D12 renderer's 3D scene.

#include "galaxy/windows_api.h"
#include "galaxy/render/gpu_use.h"
#include <d3d12.h>
#include <wrl/client.h>

#include "galaxy/render/ambient_occlusion_objects.h"
#include "galaxy/render/gpu_upload_ring.h"
#include "galaxy/render/rt_types.h"

#include <array>
#include <cstdint>
#include <vector>

namespace galaxy::render {

class DescriptorRing;

// Ambient occlusion of the 3D scene (RenderConfig::ambient_occlusion; ambient_occlusion.hlsl): ground-truth ambient
// occlusion from the scene target's depth, multiplied into its colour after the opaque phases, before the translucent ones
// (glass, water, glows and effects stay as drawn). Every perspective scene gets it, the game's baked map geometry
// included; ray-traced lighting has its own. Occlusion pixels: ambient_occlusion_shape.h.
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
        ID3D12Resource* depth = nullptr;  // the scene target's single-sample depth, in a shader-readable state
        unsigned width = 0;               // its size in pixels
        unsigned height = 0;
        D3D12_CPU_DESCRIPTOR_HANDLE color_rtv{};  // the scene target's colour (multisampled with MSAA)
        unsigned samples = 1;
        bool hdr = false;                 // the colour is the HDR scene scene target (kSceneHdrColorFormat)
        bool ssao = false;                // SSAO instead of GTAO
        const std::vector<ao::ObjectOccluder>* objects = nullptr;  // the scene's models
        UploadRing* ring = nullptr;       // where their buffers go
        RtView view{};                    // the scene's projection, depth range and viewport (scene target pixels)
    };
    // Records the occlusion and multiplies it into `color_rtv`'s RGB. Leaves this pass's root signatures, pipeline
    // and render target bound. Returns false when nothing was recorded.
    bool record(ID3D12GraphicsCommandList* list, DescriptorRing& ring, const Inputs& in, std::uint64_t frame_index,
                unsigned frames_in_flight);
    // Once a frame: frees what record() released, and hands back the targets no record() has asked for in 600 frames
    // (the occlusion switched off), at the frame's fence; the next record() makes them again.
    void trim_idle(std::uint64_t frame_index, unsigned frames_in_flight);

private:
    struct Target {
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        unsigned width = 0;
        unsigned height = 0;
        gpu::Use use = gpu::Use::ComputeRead;
        std::uint64_t used_frame = 0;   // the last frame record() asked for it
    };
    struct Retired {
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        std::uint64_t release_after_frame = 0;
    };
    std::vector<float> object_values_;
    std::vector<std::uint32_t> object_tiles_;
    ao::ObjectBinScratch object_scratch_;
    bool ensure_target(Target& target, DXGI_FORMAT format, unsigned width, unsigned height,
                       std::uint64_t frame_index, unsigned frames_in_flight);
    void transition(ID3D12GraphicsCommandList* list, Target& target, gpu::Use use);
    [[nodiscard]] ID3D12PipelineState* apply_pipeline(unsigned samples, bool hdr);

    ID3D12Device* device_ = nullptr;
    bool ready_ = false;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_signature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> distance_pass_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> occlusion_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> ssao_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> denoise_;
    // 1, 2, 4, 8 samples; then HDR.
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, 8> apply_{};
    Target distance_;  // eye distance per occlusion pixel (R32F)
    Target raw_;       // occlusion, eye distance (R16G16F)
    Target denoised_;
    std::vector<Retired> retired_;
};

}  // namespace galaxy::render
