// SPDX-License-Identifier: GPL-3.0-only
// The presentation chain on Direct3D 11: post_process.h's passes (the D3D12 model, post_process.cpp: SMAA or FXAA of the
// scene, FSR 1 EASU and RCAS, the box-filter resample and the exact-size copy, the last write, HDR10) from the same
// shaders, which fxc compiles for shader model 5.0 (post_process.hlsl: constants at b0, samplers s0 and s1, inputs t0-t2).
//
// Every call is on the render thread with the immediate context; no scene pass is open. A call leaves its shaders,
// states, constants and samplers bound and unbinds its targets and inputs: the renderer binds its own again.
//
// Direct3D 11 bakes no target format or sample count into a pass: one pixel shader serves the window, the HDR10 swap
// chain, an intermediate and a multisampled scene target alike; the constants (set_post_output) carry the difference.
#pragma once

#include "galaxy/render/d3d11/d3d11_context.h"
#include "galaxy/render/post_process_settings.h"

#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>

namespace galaxy::render::d3d11 {

class PostProcessor {
public:
    PostProcessor() = default;
    PostProcessor(const PostProcessor&) = delete;
    PostProcessor& operator=(const PostProcessor&) = delete;
    ~PostProcessor() { shutdown(); }

    // Every pass, the SMAA lookup textures and the AA self-tests (status() final on return); false: the plain blit.
    bool initialize(ID3D11Device1* device, ID3D11DeviceContext1* context, const PostProcessSettings& settings);
    void shutdown() noexcept;
    [[nodiscard]] bool ready() const noexcept { return ready_; }
    [[nodiscard]] const PostProcessStatus& status() const noexcept { return status_; }

    // The scaling chain from `source` (its `source_width` x `source_height` picture from its top left) into the
    // `viewport` rectangle of `target`. `output.hdr`: the target is the HDR10 swap chain buffer. At the picture's own
    // size, or enlarged without sharpening, or enlarged on both axes with sharpening, it is one pass into the target.
    void record(ID3D11DeviceContext1* context, const Texture& source, unsigned source_width, unsigned source_height,
                ID3D11RenderTargetView* target, const D3D11_VIEWPORT& viewport, const PostOutput& output = PostOutput{});
    // True once ready: the last write's passes take an R10G10B10A2 target as they are.
    [[nodiscard]] bool prepare_hdr() const noexcept { return ready_; }

    // The scene target's image traded for `spare` (null: the renderer makes one): `image` is the scene's own image,
    // sampled from here on by the passes, and `target` the scene target's view over the image that took its place,
    // whose old contents are dropped.
    struct Traded {
        std::shared_ptr<Texture> image;
        ID3D11RenderTargetView* target = nullptr;
    };
    using SceneTrade = std::function<Traded(std::shared_ptr<Texture> spare)>;

    // SMAA or FXAA of the 3D scene, as post_process.h's. `color` is the scene's single-sample picture (resolved when the
    // scene is multisampled) and `target` the scene target's view (the multisampled one, whose every sample takes the
    // result, with MSAA; the resolved picture is stale afterwards). The passes read a copy of `color` and write its RGB
    // into `target` (the game's alpha stays). With `trade` (a single-sample scene whose image may change hands) the
    // passes read the scene's own image instead, without the copy, and write every pixel and channel of the image that
    // took its place (the alpha the scene's): that image is discarded first, so a tiled GPU never loads it.
    AaRun record_scene_aa(ID3D11DeviceContext1* context, const Texture& color, ID3D11RenderTargetView* target,
                          unsigned width, unsigned height, unsigned picture_width, unsigned picture_height,
                          unsigned display_width, unsigned display_height, const SceneTrade& trade = {});
    [[nodiscard]] const PostProcessSettings& settings() const noexcept { return settings_; }
    // New settings between frames on the render thread.
    void apply_settings(const PostProcessSettings& settings);

private:
    // A work picture and the frame it was last drawn (idle ones go after kTargetIdleFrames).
    struct Target {
        std::shared_ptr<Texture> texture;
        std::uint64_t used = 0;
    };
    bool ensure_target(Target& target, unsigned width, unsigned height, DXGI_FORMAT format, std::uint64_t frame);
    void trim_idle_targets(std::uint64_t frame) noexcept;
    bool create_smaa_shaders(unsigned quality, Microsoft::WRL::ComPtr<ID3D11PixelShader>& edges,
                             Microsoft::WRL::ComPtr<ID3D11VertexShader>& weights_vs,
                             Microsoft::WRL::ComPtr<ID3D11PixelShader>& weights);
    // The state every pass shares: no vertex input, triangles, no culling or scissor, the samplers, the constants.
    void bind_passes(ID3D11DeviceContext1* context);
    void draw(ID3D11DeviceContext1* context, ID3D11VertexShader* vs, ID3D11PixelShader* ps,
              std::initializer_list<ID3D11ShaderResourceView*> inputs, const D3D11_VIEWPORT& viewport,
              const PostConstants& constants);
    // The AA passes from `source` into `target`, the last one with `whole` (every channel, its alpha the source's) or
    // RGB only. SMAA needs edges_, weights_ and stencil_ at `width` x `height`.
    void run_aa(ID3D11DeviceContext1* context, AaRun pass, ID3D11ShaderResourceView* source,
                ID3D11RenderTargetView* target, unsigned width, unsigned height, bool whole);
    void note_aa_run(const PostProcessPlan& plan, AaRun ran);
    void self_test(ID3D11DeviceContext1* context);

    ID3D11Device1* device_ = nullptr;
    bool ready_ = false;
    PostProcessStatus status_{};
    PostProcessSettings settings_{};

    Microsoft::WRL::ComPtr<ID3D11Buffer> constants_;  // PostConstants at b0, rewritten for each pass
    Microsoft::WRL::ComPtr<ID3D11SamplerState> linear_;  // s0
    Microsoft::WRL::ComPtr<ID3D11SamplerState> point_;   // s1
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterizer_;
    Microsoft::WRL::ComPtr<ID3D11BlendState> rgb_;  // the scene AA's last write without a trade: RGB only
    // SMAA's edge mask: the edge pass marks 1 where it keeps a pixel; the weights pass runs only there.
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> stencil_mark_;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> stencil_test_;

    Microsoft::WRL::ComPtr<ID3D11VertexShader> fullscreen_vs_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> easu_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> rcas_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> rcas_scaled_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> resample_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> copy_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> fxaa_;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> smaa_edges_vs_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> smaa_edges_;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> smaa_weights_vs_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> smaa_weights_;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> smaa_blend_vs_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> smaa_blend_;       // every channel, the source's alpha
    Microsoft::WRL::ComPtr<ID3D11PixelShader> smaa_blend_keep_;  // under the RGB-only write
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> area_texture_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> search_texture_;

    Target edges_;
    Target weights_;
    Target stencil_;
    Target antialiased_;  // the scene's copy the AA passes read without a trade
    Target scaled_;       // the window-sized picture RCAS reads after a separate enlargement
    // The scene's image traded out (record_scene_aa with a trade): the passes' source, the next trade's spare.
    std::shared_ptr<Texture> traded_;
    std::uint64_t traded_frame_ = 0;
};

}  // namespace galaxy::render::d3d11
