// SPDX-License-Identifier: GPL-3.0-only
// The scene camera on Direct3D 11 (the enhanced lighting's exposure and tone map): scene_camera.h's passes (the D3D12
// model, scene_camera.cpp) from the same shaders compiled for shader model 5.0. Owned by the effects lane: until its
// implementation lands, ready() is false and the scene is not drawn wide.
#pragma once

#include "galaxy/render/d3d11/d3d11_context.h"

namespace galaxy::render::d3d11 {

class SceneCamera {
public:
    SceneCamera() = default;
    SceneCamera(const SceneCamera&) = delete;
    SceneCamera& operator=(const SceneCamera&) = delete;
    ~SceneCamera() { shutdown(); }

    bool initialize(ID3D11Device1* device) {
        (void)device;
        return false;
    }
    void shutdown() noexcept {}
    [[nodiscard]] bool ready() const noexcept { return ready_; }

    struct Inputs {
        Texture* wide = nullptr;                // single-sample, resolved
        Texture* wide_ms = nullptr;             // the multisampled target, or null
        Texture* depth = nullptr;               // the scene's single-sample depth
        ID3D11RenderTargetView* rtv = nullptr;  // the scene target
        unsigned width = 0u, height = 0u, samples = 1u;
        float seconds = 0.0f;  // since the last meter; below zero the eye starts open
        bool resolve = false;  // tone-map MSAA samples directly into a single-sample output
    };
    // The meter and the tone map into `rtv`.
    bool record(ID3D11DeviceContext1* context, const Inputs& in) {
        (void)context, (void)in;
        return false;
    }

    struct WidenInputs {
        Texture* picture = nullptr;             // the scene target's single-sample colour
        ID3D11RenderTargetView* rtv = nullptr;  // the wide target
        unsigned width = 0u, height = 0u, samples = 1u;
    };
    // The scene so far into the wide target.
    bool widen(ID3D11DeviceContext1* context, const WidenInputs& in) {
        (void)context, (void)in;
        return false;
    }

private:
    bool ready_ = false;
};

}  // namespace galaxy::render::d3d11
