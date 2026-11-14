// SPDX-License-Identifier: GPL-3.0-only
// The scene camera on Direct3D 11 (the enhanced lighting's exposure and tone map): scene_camera.h's passes (the D3D12
// model, scene_camera.cpp) from the same shaders, which fxc already compiles for shader model 5.0 with plain registers
// (camera_*.h: b0, t0-t2, u0). Each call unbinds the views it bound (outputs and inputs) before it returns and leaves
// its shaders, constants and viewport bound.
#pragma once

#include "galaxy/render/d3d11/d3d11_context.h"

#include <memory>

namespace galaxy::render::d3d11 {

class SceneCamera {
public:
    SceneCamera() = default;
    SceneCamera(const SceneCamera&) = delete;
    SceneCamera& operator=(const SceneCamera&) = delete;
    ~SceneCamera() { shutdown(); }

    bool initialize(ID3D11Device1* device);
    void shutdown() noexcept;
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
    bool record(ID3D11DeviceContext1* context, const Inputs& in);

    struct WidenInputs {
        Texture* picture = nullptr;             // the scene target's single-sample colour
        ID3D11RenderTargetView* rtv = nullptr;  // the wide target
        unsigned width = 0u, height = 0u, samples = 1u;
    };
    // The scene so far into the wide target.
    bool widen(ID3D11DeviceContext1* context, const WidenInputs& in);

private:
    void set_constants(ID3D11DeviceContext1* context, const void* constants, unsigned bytes);
    void draw(ID3D11DeviceContext1* context, ID3D11PixelShader* shader, ID3D11RenderTargetView* rtv,
              ID3D11ShaderResourceView* picture, unsigned width, unsigned height);

    ComPtr<ID3D11ComputeShader> meter_;
    ComPtr<ID3D11VertexShader> vs_;
    ComPtr<ID3D11PixelShader> camera_;          // single-sample
    ComPtr<ID3D11PixelShader> camera_samples_;  // every sample of a multisampled target
    ComPtr<ID3D11PixelShader> camera_resolve_;  // multisampled scene into a single-sample target
    ComPtr<ID3D11PixelShader> widen_;
    ComPtr<ID3D11Buffer> constants_;
    ComPtr<ID3D11RasterizerState> raster_;
    ComPtr<ID3D11DepthStencilState> no_depth_;
    std::shared_ptr<Texture> exposure_;  // linear exposure and its display-space gain (R32F, 2 x 1)
    bool exposure_ready_ = false;
    bool ready_ = false;
};

}  // namespace galaxy::render::d3d11
