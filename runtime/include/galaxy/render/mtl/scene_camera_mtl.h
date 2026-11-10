// SPDX-License-Identifier: GPL-3.0-only
// The wide 3D scene's camera (scene_camera.h, scene_camera.hlsl turned to MSL at build) on Metal, as
// vk/scene_camera_vk.h for Vulkan. meter runs with no encoder open, widen and draw inside the scene pass they name.
//
// Plain C++ (mtl_context.h): Metal's objects are mtl::Object.
#pragma once

#include "galaxy/render/mtl/mtl_context.h"

#include <array>
#include <memory>

namespace galaxy::render::mtl {

class SceneCamera {
public:
    SceneCamera() = default;
    SceneCamera(const SceneCamera&) = delete;
    SceneCamera& operator=(const SceneCamera&) = delete;
    ~SceneCamera() { shutdown(); }

    // False (the device cannot read and write one texture in a pass, or a program fails) leaves the camera off.
    // `depth_format`: the scene pass's depth and stencil target.
    bool initialize(Format depth_format);
    void shutdown() noexcept;
    [[nodiscard]] bool ready() const noexcept { return ready_; }
    // `picture` is the scene target's colour; every sample of the wide target (the pass `encoder`'s colour, of
    // `color_format` over `samples` samples a pixel) takes it.
    void widen(Object encoder, Format color_format, unsigned samples, const Texture& picture);
    // `wide` is the resolved wide colour. Below zero `seconds` starts the eye open. `commands`: an
    // id<MTLCommandBuffer> with no encoder open.
    void meter(Object commands, const Texture& wide, const Texture& depth, float seconds);
    // `wide` is the wide colour: the multisampled target when `per_sample` or `resolve`, else the resolved one.
    void draw(Object encoder, Format color_format, unsigned samples, const Texture& wide, bool per_sample, bool resolve = false);

private:
    enum Kind : unsigned { kWiden, kCamera, kCameraSamples, kCameraResolve, kKinds };
    [[nodiscard]] Object function(unsigned index);  // id<MTLFunction>, by kShaders' order
    [[nodiscard]] Object pipeline(Kind kind, Format color_format, unsigned samples);
    void full_screen(Object encoder, Object pipeline, const Texture& sampled, unsigned width, unsigned height) const;

    std::shared_ptr<Texture> exposure_;
    Format depth_format_ = Format::Depth32fStencil8;
    std::array<Object, 6> functions_{};  // id<MTLFunction>
    Object meter_ = nullptr;             // id<MTLComputePipelineState>
    Object no_depth_ = nullptr;          // id<MTLDepthStencilState>
    struct Made {
        Kind kind = kWiden;
        Format color = Format::None;
        unsigned samples = 0;
        Object pipeline = nullptr;  // id<MTLRenderPipelineState>
    };
    std::array<Made, 16> pipelines_{};
    bool exposure_ready_ = false;
    bool ready_ = false;
};

}  // namespace galaxy::render::mtl
