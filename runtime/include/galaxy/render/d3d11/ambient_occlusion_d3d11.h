// SPDX-License-Identifier: GPL-3.0-only
// Ambient occlusion on Direct3D 11: ambient_occlusion.h's passes (the D3D12 model, ambient_occlusion.cpp: GTAO or SSAO,
// denoise, upsample, apply) from the same shaders compiled for shader model 5.0. Owned by the effects lane: until its
// implementation lands, ready() is false and the scene has none.
#pragma once

#include "galaxy/render/ambient_occlusion_objects.h"
#include "galaxy/render/d3d11/d3d11_context.h"
#include "galaxy/render/gpu_upload_ring.h"
#include "galaxy/render/rt_types.h"

#include <vector>

namespace galaxy::render::d3d11 {

class AmbientOcclusion {
public:
    AmbientOcclusion() = default;
    AmbientOcclusion(const AmbientOcclusion&) = delete;
    AmbientOcclusion& operator=(const AmbientOcclusion&) = delete;
    ~AmbientOcclusion() { shutdown(); }

    bool initialize(ID3D11Device1* device) {
        (void)device;
        return false;
    }
    void shutdown() noexcept {}
    [[nodiscard]] bool ready() const noexcept { return ready_; }

    struct Inputs {
        Texture* depth = nullptr;               // the scene target's single-sample depth (its view sampled)
        unsigned width = 0;                     // its size in pixels
        unsigned height = 0;
        ID3D11RenderTargetView* color = nullptr;  // the scene target's colour (multisampled with MSAA)
        unsigned samples = 1;
        bool hdr = false;                       // the colour is the wide scene target
        bool ssao = false;                      // SSAO instead of GTAO
        const std::vector<ao::ObjectOccluder>* objects = nullptr;  // the scene's models
        UploadRing* ring = nullptr;             // where their buffers go
        RtView view{};                          // the scene's projection, depth range and viewport (scene target pixels)
    };
    // The occlusion, multiplied into `color`'s RGB. False when nothing was drawn.
    bool record(ID3D11DeviceContext1* context, const Inputs& in) {
        (void)context, (void)in;
        return false;
    }
    // Once a frame: the targets no record() asked for in 600 frames are handed back.
    void trim_idle() {}

private:
    bool ready_ = false;
};

}  // namespace galaxy::render::d3d11
