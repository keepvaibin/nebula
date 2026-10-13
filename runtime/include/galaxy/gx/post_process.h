#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <d3d12.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace galaxy::gx {

class DescriptorRing;

// Presentation image quality, applied to the presented XFB on the render
// thread only.
//   aa:          SMAA 1x or FXAA at the game image's resolution, before any
//                scaling. kAuto runs SMAA only when the game image is not
//                already supersampled (less than 1.5 source pixels per window
//                pixel on either axis). SMAA falls back to FXAA when its
//                pipelines, lookup textures or start-up self-test fail.
//   upscaler:    FidelityFX FSR 1 EASU when the game image is smaller than the
//                window on both axes; otherwise a footprint box filter, which
//                averages every source pixel when the image is reduced.
//   sharpness:   FidelityFX RCAS strength, 0-100; 0 skips the pass.
struct PostProcessSettings {
    enum class Aa : std::uint8_t { kOff = 0, kSmaa = 1, kAuto = 2, kFxaa = 3 };
    enum class Upscaler : std::uint8_t { kBilinear = 0, kFsr1 = 1 };
    Aa aa = Aa::kAuto;
    std::uint8_t smaa_quality = 2;  // 0 low, 1 medium, 2 high, 3 ultra
    Upscaler upscaler = Upscaler::kFsr1;
    std::uint8_t sharpness = 20;
};

// Which passes one frame runs, decided from sizes and settings alone so the
// choice is testable without a device.
struct PostProcessPlan {
    bool aa_wanted = false; // the settings ask for an AA pass this frame
    bool supersampled = false;
    bool smaa = false;
    bool fxaa = false;
    bool easu = false;     // EASU into the window-sized intermediate
    bool resample = false; // box filter (or identity copy) step
    bool rcas = false;
    // The resample writes the window directly unless RCAS follows it.
    bool resample_to_target = false;
};

// smaa_available / fxaa_available: the pass passed its start-up self-test.
[[nodiscard]] PostProcessPlan plan_post_process(
    unsigned source_width, unsigned source_height,
    unsigned target_width, unsigned target_height,
    const PostProcessSettings& settings,
    bool smaa_available = true, bool fxaa_available = true) noexcept;

enum class AaRun : std::uint8_t { kNone = 0, kSmaa = 1, kFxaa = 2 };

// What the presentation chain can do and did last frame; published by the
// renderer for the settings overlay and the logs.
struct PostProcessStatus {
    bool initialized = false;  // the chain exists (otherwise the plain blit)
    bool smaa_ok = false;      // pipelines, lookup textures and self-test
    bool fxaa_ok = false;
    std::array<char, 40> smaa_reason{};  // why SMAA is unavailable
    std::array<char, 40> fxaa_reason{};
    std::array<char, 40> skip_reason{};  // why last frame's wanted AA did not run
    AaRun last_run = AaRun::kNone;
    bool last_wanted = false;
    bool last_supersampled = false;
    std::uint8_t live_aa = 0;       // PostProcessSettings::Aa the chain uses
    std::uint8_t live_quality = 0;
    std::uint64_t frames = 0;       // totals since start
    std::uint64_t aa_wanted_frames = 0;
    std::uint64_t smaa_frames = 0;
    std::uint64_t fxaa_frames = 0;
    std::uint64_t aa_skipped_frames = 0;  // wanted, no AA pass ran
};

// Overlay row text. draft_aa is the overlay's unapplied value: the live state
// is shown only while it matches what the chain runs.
[[nodiscard]] std::string anti_aliasing_row_text(
    unsigned draft_aa, unsigned draft_quality, const PostProcessStatus& status);
[[nodiscard]] std::string post_process_caps_text(const PostProcessStatus& status);

// Thread-safe copy for readers off the render thread.
[[nodiscard]] PostProcessStatus get_post_process_status();
void set_post_process_status(const PostProcessStatus& status);

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

    // Records the chain from `source` (RGBA8, PIXEL_SHADER_RESOURCE) into the
    // `viewport` rectangle of `target_rtv`. Intermediate targets are kept
    // across frames and replaced only when a size changes; replaced
    // resources are released `frames_in_flight` frames later.
    void record(ID3D12GraphicsCommandList* list,
                DescriptorRing& srv_ring,
                ID3D12Resource* source,
                unsigned source_width,
                unsigned source_height,
                D3D12_CPU_DESCRIPTOR_HANDLE target_rtv,
                const D3D12_VIEWPORT& viewport,
                std::uint64_t frame_index,
                unsigned frames_in_flight);

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
        D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    };
    struct Retired {
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        std::uint64_t release_after_frame = 0;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline;
    };
    struct Constants {
        float rt_metrics[4];
        std::uint32_t con0[4];
        std::uint32_t con1[4];
        std::uint32_t con2[4];
        std::uint32_t con3[4];
        float params[4];
        std::int32_t origin[4];
    };
    static_assert(sizeof(Constants) == 28u * 4u);

    bool create_pipeline(D3D12_SHADER_BYTECODE vs, D3D12_SHADER_BYTECODE ps,
                         DXGI_FORMAT format, int stencil,
                         Microsoft::WRL::ComPtr<ID3D12PipelineState>& out);
    bool create_smaa_pipelines(unsigned quality,
                               Microsoft::WRL::ComPtr<ID3D12PipelineState>& edges,
                               Microsoft::WRL::ComPtr<ID3D12PipelineState>& weights);
    bool ensure_target(Target& target, unsigned width, unsigned height,
                       DXGI_FORMAT format, unsigned rtv_slot,
                       std::uint64_t frame_index, unsigned frames_in_flight);
    void transition(ID3D12GraphicsCommandList* list, Target& target,
                    D3D12_RESOURCE_STATES state);
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
              std::initializer_list<DXGI_FORMAT> formats,
              const D3D12_VIEWPORT& viewport,
              const Constants& constants);
    void release_retired(std::uint64_t frame_index) noexcept;

    ID3D12Device* device_ = nullptr;
    PostProcessSettings settings_{};
    bool ready_ = false;
    PostProcessStatus status_{};
    bool fault_selftest_ = false;  // GALAXY_TEST_POST_FAULT
    bool fault_targets_ = false;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_signature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> smaa_edges_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> smaa_weights_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> smaa_blend_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> fxaa_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> easu_;
    // RCAS and the resample run into intermediates and into the back buffer;
    // both use RGBA8, so one pipeline each serves every target.
    Microsoft::WRL::ComPtr<ID3D12PipelineState> rcas_;
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
    // SMAA's edge mask: the weight pass runs only where edge detection kept a
    // pixel. `rtv` holds its DSV.
    Target stencil_;
    std::vector<Retired> retired_;
};

}  // namespace galaxy::gx
