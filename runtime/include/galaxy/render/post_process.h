#pragma once

// The D3D12 presentation chain: scene SMAA/FXAA, FSR 1 EASU or box-filter scaling, and RCAS.

#include "galaxy/windows_api.h"
#include "galaxy/render/gpu_use.h"
#include <d3d12.h>
#include <wrl/client.h>

#include "galaxy/render/post_process_settings.h"

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace galaxy::render {

class DescriptorRing;

// Image quality on the render thread.
//   aa:          SMAA 1x or FXAA of the 3D scene in the scene target, before the 2D layer
//                draws (record_scene_aa), so menus and text stay sharp. kAuto
//                runs SMAA only when the scene is not already supersampled
//                (less than 1.5 scene pixels per window pixel on either axis).
//                SMAA falls back to FXAA when its pipelines, lookup textures or
//                start-up self-test fail. The presented frame is not
//                antialiased again.
//   upscaler:    FidelityFX FSR 1 EASU when the game image is smaller than the
//                window on both axes; otherwise a footprint box filter, which
//                averages every source pixel when the image is reduced.
//   sharpness:   FidelityFX RCAS strength, 0-100; 0 skips the pass.

class PostProcessor {
public:
    PostProcessor() = default;
    PostProcessor(const PostProcessor&) = delete;
    PostProcessor& operator=(const PostProcessor&) = delete;
    ~PostProcessor() { shutdown(); }

    // Builds the root signature and every pipeline (shaders are precompiled
    // into the runtime), uploads the SMAA lookup textures on `queue` and
    // runs the SMAA and FXAA self-tests before returning, so status() is
    // final before the first present. Returns false when the scaling passes
    // cannot be built; the caller then keeps its plain blit. An AA pass that
    // fails is reported in status() and replaced (SMAA -> FXAA -> none).
    bool initialize(ID3D12Device* device, ID3D12CommandQueue* queue,
                    const PostProcessSettings& settings);
    void shutdown() noexcept;
    [[nodiscard]] bool ready() const noexcept { return ready_; }
    // Capabilities, self-test results, last frame's AA pass and totals.
    [[nodiscard]] const PostProcessStatus& status() const noexcept { return status_; }

    // Records the scaling chain from `source` (RGBA8, PIXEL_SHADER_RESOURCE)
    // into the `viewport` rectangle of `target_rtv`. Intermediate targets are
    // kept across frames and replaced only when a size changes; replaced
    // resources are released `frames_in_flight` frames later.
    // `source` holds its `source_width` x `source_height` picture from its top left (a handed-over scene target is taller).
    // `output.hdr`: the target is an R10G10B10A2 HDR10 swap chain buffer and the last write encodes for it (prepare_hdr first);
    // the default is the 8-bit window, the SDR write.
    void record(ID3D12GraphicsCommandList* list,
                DescriptorRing& srv_ring,
                ID3D12Resource* source,
                unsigned source_width,
                unsigned source_height,
                D3D12_CPU_DESCRIPTOR_HANDLE target_rtv,
                const D3D12_VIEWPORT& viewport,
                std::uint64_t frame_index,
                unsigned frames_in_flight,
                const PostOutput& output = PostOutput{});

    // Builds the last-write pipelines for an R10G10B10A2 target (once; kept). False when they cannot be made: the renderer
    // then keeps its 8-bit swap chain, since record() with output.hdr writes nothing a different target format could take.
    [[nodiscard]] bool prepare_hdr();

    // The scene target's image traded for `spare` (RendererD3D12::swap_scene_color): the scene's own image comes back,
    // shader-readable, and the scene target goes on in `spare` (or one made for it), RENDER_TARGET state, its old
    // contents dropped.
    using SceneTrade = std::function<Microsoft::WRL::ComPtr<ID3D12Resource>(Microsoft::WRL::ComPtr<ID3D12Resource> spare)>;

    // SMAA or FXAA of the 3D scene in `color` (RGBA8, RENDER_TARGET state, `width` x `height`) in
    // place: RGB only, the alpha the game left there stays. `picture_*` is the part the display
    // copy shows and `display_*` its size on screen (kAuto skips a supersampled scene). Returns
    // the pass that ran. With `trade` the passes read the scene's own image instead of a copy of it and write every
    // pixel of the image that took its place at `color_rtv`, its alpha the scene's.
    AaRun record_scene_aa(ID3D12GraphicsCommandList* list, DescriptorRing& srv_ring,
                          ID3D12Resource* color, D3D12_CPU_DESCRIPTOR_HANDLE color_rtv,
                          unsigned width, unsigned height, unsigned picture_width,
                          unsigned picture_height, unsigned display_width, unsigned display_height,
                          std::uint64_t frame_index, unsigned frames_in_flight, unsigned samples = 1u,
                          const SceneTrade& trade = {});

    [[nodiscard]] const PostProcessSettings& settings() const noexcept { return settings_; }

    // Takes new settings between frames on the render thread. A quality change rebuilds the two SMAA
    // pipelines; the replaced ones are released `frames_in_flight` frames later. Everything else is read per frame.
    void apply_settings(const PostProcessSettings& settings,
                        std::uint64_t frame_index, unsigned frames_in_flight);

private:
    struct Target {
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        unsigned width = 0;
        unsigned height = 0;
        DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
        D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
        gpu::Use use = gpu::Use::PixelRead;
        std::uint64_t used_frame = 0;  // the last frame ensure_target was asked for it
    };
    struct Retired {
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        std::uint64_t release_after_frame = 0;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline;
    };
    using Constants = PostConstants;

    bool create_pipeline(D3D12_SHADER_BYTECODE vs, D3D12_SHADER_BYTECODE ps,
                         DXGI_FORMAT format, int stencil,
                         Microsoft::WRL::ComPtr<ID3D12PipelineState>& out,
                         UINT8 write_mask = D3D12_COLOR_WRITE_ENABLE_ALL, UINT samples = 1u);
    bool create_smaa_pipelines(unsigned quality,
                               Microsoft::WRL::ComPtr<ID3D12PipelineState>& edges,
                               Microsoft::WRL::ComPtr<ID3D12PipelineState>& weights);
    bool ensure_target(Target& target, unsigned width, unsigned height,
                       DXGI_FORMAT format, unsigned rtv_slot,
                       std::uint64_t frame_index, unsigned frames_in_flight);
    void transition(ID3D12GraphicsCommandList* list, Target& target, gpu::Use use);
    // Records the lookup-texture copies; the staging buffers go to `staging`.
    bool record_lookup_upload(ID3D12GraphicsCommandList* list,
                              std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>>& staging);
    // SMAA or FXAA from `source` into antialiased_ (source-sized); false when
    // an intermediate target cannot be created.
    bool record_aa(ID3D12GraphicsCommandList* list, DescriptorRing& srv_ring, AaRun pass,
                   ID3D12Resource* source, unsigned width, unsigned height,
                   std::uint64_t frame_index, unsigned frames_in_flight);
    // Uploads the lookup textures and runs both AA passes on a synthetic
    // stair-stepped edge, on `queue`, waiting for the GPU.
    void self_test(ID3D12CommandQueue* queue);
    // Binds `inputs` as t0..t2 and draws the full-target triangle into the
    // render target the caller has bound, inside `viewport`.
    void draw(ID3D12GraphicsCommandList* list, DescriptorRing& srv_ring,
              ID3D12PipelineState* pipeline,
              std::initializer_list<ID3D12Resource*> inputs,
              const D3D12_VIEWPORT& viewport,
              const Constants& constants);
    void note_aa_run(const PostProcessPlan& plan, AaRun ran);
    void release_retired(std::uint64_t frame_index) noexcept;
    // Hands back the targets no pass has asked for in kTargetIdleFrames frames (SMAA's after a switch to FXAA or off, the
    // scaling intermediate after a window size that needs none); the next pass that wants one makes it again.
    void trim_idle_targets(std::uint64_t frame_index, unsigned frames_in_flight);

    ID3D12Device* device_ = nullptr;
    PostProcessSettings settings_{};
    bool ready_ = false;
    PostProcessStatus status_{};
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_signature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> smaa_edges_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> smaa_weights_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> smaa_blend_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> fxaa_;
    // The scene passes' last step into the scene target: colour only.
    Microsoft::WRL::ComPtr<ID3D12PipelineState> smaa_blend_rgb_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> fxaa_rgb_;
    // FXAA's, SMAA's: per sample count (log2).
    Microsoft::WRL::ComPtr<ID3D12PipelineState> scene_pass_ms_[2][4];
    [[nodiscard]] ID3D12PipelineState* scene_pass_pipeline(bool smaa, unsigned samples);
    Microsoft::WRL::ComPtr<ID3D12PipelineState> easu_;
    // Into the window (8 bits, dithered), and into a picture for a pass after it (the scene's format).
    Microsoft::WRL::ComPtr<ID3D12PipelineState> easu_window_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> resample_window_;
    // The resample at 1:1 (PostProcessPlan::copy).
    Microsoft::WRL::ComPtr<ID3D12PipelineState> copy_window_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> rcas_;
    // Bilinear enlargement and RCAS in one pass (no window-sized intermediate).
    Microsoft::WRL::ComPtr<ID3D12PipelineState> rcas_scaled_;
    // The same last writes into an R10G10B10A2 HDR10 swap chain buffer (prepare_hdr).
    Microsoft::WRL::ComPtr<ID3D12PipelineState> easu_hdr_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> rcas_hdr_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> rcas_scaled_hdr_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> resample_hdr_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> copy_hdr_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> resample_;
    Microsoft::WRL::ComPtr<ID3D12Resource> area_texture_;
    Microsoft::WRL::ComPtr<ID3D12Resource> search_texture_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtv_heap_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsv_heap_;
    unsigned rtv_increment_ = 0;
    Target edges_;
    Target weights_;
    Target antialiased_;
    Target scaled_;
    // The scene's last image traded out (record_scene_aa with a trade): the next trade's spare.
    Microsoft::WRL::ComPtr<ID3D12Resource> traded_;
    std::uint64_t traded_frame_ = 0;
    // SMAA's edge mask: the weight pass runs only where edge detection kept a
    // pixel. `rtv` holds its DSV.
    Target stencil_;
    std::vector<Retired> retired_;
};

}  // namespace galaxy::render
