// SPDX-License-Identifier: GPL-3.0-only
// The presentation chain on Direct3D 11: post_process.h's passes (the D3D12 model, post_process.cpp: SMAA or FXAA of the
// scene, FSR 1 EASU and RCAS, the last write, HDR10) from the same shaders compiled for shader model 5.0. Owned by the
// post lane: until its implementation lands, ready() is false and the renderer presents with its plain blit.
//
// Every call is on the render thread with the immediate context; no scene pass is open. A call leaves its own state
// bound: the renderer binds its own again.
#pragma once

#include "galaxy/render/d3d11/d3d11_context.h"
#include "galaxy/render/post_process_settings.h"

#include <cstdint>

namespace galaxy::render::d3d11 {

class PostProcessor {
public:
    PostProcessor() = default;
    PostProcessor(const PostProcessor&) = delete;
    PostProcessor& operator=(const PostProcessor&) = delete;
    ~PostProcessor() { shutdown(); }

    // Every pipeline, the SMAA lookup textures and the AA self-tests (status() final on return); false: the plain blit.
    bool initialize(ID3D11Device1* device, ID3D11DeviceContext1* context, const PostProcessSettings& settings) {
        (void)device, (void)context;
        settings_ = settings;
        return false;
    }
    void shutdown() noexcept {}
    [[nodiscard]] bool ready() const noexcept { return ready_; }
    [[nodiscard]] const PostProcessStatus& status() const noexcept { return status_; }

    // The scaling chain from `source` (its `source_width` x `source_height` picture from its top left) into the
    // `viewport` rectangle of `target`. `output.hdr`: the target is the HDR10 swap chain buffer (prepare_hdr first).
    void record(ID3D11DeviceContext1* context, const Texture& source, unsigned source_width, unsigned source_height,
                ID3D11RenderTargetView* target, const D3D11_VIEWPORT& viewport, const PostOutput& output = PostOutput{}) {
        (void)context, (void)source, (void)source_width, (void)source_height, (void)target, (void)viewport, (void)output;
    }
    // The last write's pipelines for an R10G10B10A2 target; false: the renderer keeps its 8-bit swap chain.
    [[nodiscard]] bool prepare_hdr() { return false; }
    // SMAA or FXAA of the 3D scene in `color` in place (RGB; the game's alpha stays), as post_process.h's.
    AaRun record_scene_aa(ID3D11DeviceContext1* context, Texture& color, unsigned width, unsigned height,
                          unsigned picture_width, unsigned picture_height, unsigned display_width, unsigned display_height,
                          unsigned samples = 1u) {
        (void)context, (void)color, (void)width, (void)height, (void)picture_width, (void)picture_height;
        (void)display_width, (void)display_height, (void)samples;
        return AaRun::kNone;
    }
    [[nodiscard]] const PostProcessSettings& settings() const noexcept { return settings_; }
    // New settings between frames on the render thread.
    void apply_settings(const PostProcessSettings& settings) { settings_ = settings; }

private:
    bool ready_ = false;
    PostProcessStatus status_{};
    PostProcessSettings settings_{};
};

}  // namespace galaxy::render::d3d11
