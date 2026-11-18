#pragma once

// Ambient occlusion of the D3D12 renderer's 3D scene.

#include "galaxy/windows_api.h"
#include "galaxy/render/gpu_use.h"
#include <d3d12.h>
#include <wrl/client.h>

#include "galaxy/render/ambient_occlusion_objects.h"
#include "galaxy/render/gpu_upload_ring.h"
#include "galaxy/render/scene_view.h"

#include <array>
#include <cstdint>
#include <vector>

namespace galaxy::render {

class DescriptorRing;

// Ambient occlusion of the 3D scene (RenderConfig::ambient_occlusion; ambient_occlusion.hlsl): ground-truth ambient
// occlusion from the scene target's depth, multiplied into its colour after the opaque phases, before the translucent ones
// (glass, water, glows and effects stay as drawn). Every perspective scene gets it, the game's baked map geometry
// included. Occlusion pixels: ambient_occlusion_shape.h.
class AmbientOcclusion {
public:
    AmbientOcclusion() = default;
    AmbientOcclusion(const AmbientOcclusion&) = delete;
    AmbientOcclusion& operator=(const AmbientOcclusion&) = delete;
    ~AmbientOcclusion() { shutdown(); }

    // The root signature and compute pipelines (precompiled shaders); the multiply's pipelines are prepare_pipelines'.
    // False leaves the pass off.
    bool initialize(ID3D12Device* device);
    void shutdown() noexcept;
    // Every pipeline of the pass for each sample count up to `max_samples` (a power of two, at most 8) while the renderer
    // starts (P-052), so a frame never makes one; one the device refuses is not tried again. On any thread, while no
    // frame records with the pass.
    void prepare_pipelines(unsigned max_samples);
    [[nodiscard]] bool ready() const noexcept { return ready_; }

    struct Inputs {
        ID3D12Resource* depth = nullptr;  // the scene target's single-sample depth, in a shader-readable state
        unsigned width = 0;               // the part of it the scene is drawn in, in pixels
        unsigned height = 0;
        unsigned extent_width = 0;        // its whole size (0: the part's)
        unsigned extent_height = 0;
        unsigned lowest_sixteenths = 16u; // the smallest dynamic resolution step the scene may take (16: none)
        D3D12_CPU_DESCRIPTOR_HANDLE color_rtv{};  // the scene target's colour (multisampled with MSAA)
        unsigned samples = 1;
        bool hdr = false;                 // the colour is the HDR scene scene target (kSceneHdrColorFormat)
        bool ssao = false;                // SSAO instead of GTAO
        const std::vector<ao::ObjectOccluder>* objects = nullptr;  // the scene's models
        UploadRing* ring = nullptr;       // where their buffers go
        SceneView view{};                    // the scene's projection, depth range and viewport (scene target pixels)
    };
    // Records the occlusion and multiplies it into `color_rtv`'s RGB. Leaves this pass's root signatures, pipeline
    // and render target bound. Returns false when nothing was recorded.
    bool record(ID3D12GraphicsCommandList* list, DescriptorRing& ring, const Inputs& in, std::uint64_t frame_index,
                unsigned frames_in_flight);
    // Once a frame: frees what record() released, and hands back the targets no record() has asked for in kIdleSteps game steps (idle_clock.h)
    // (the occlusion switched off), at the frame's fence; the next record() makes them again.
    void trim_idle(std::uint64_t frame_index, unsigned frames_in_flight);

private:
    struct Target {
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        unsigned width = 0;
        unsigned height = 0;
        DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
        gpu::Use use = gpu::Use::ComputeRead;
        std::uint64_t used_step = 0;  // idle_clock() when last asked for
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
    unsigned apply_failed_ = 0u;  // apply_ entries the device would not make (not tried again)
    Target distance_;  // eye distance per occlusion pixel (R32F)
    Target raw_;       // occlusion, eye distance (R16G16F)
    Target denoised_;
    std::vector<Retired> retired_;
};

}  // namespace galaxy::render
