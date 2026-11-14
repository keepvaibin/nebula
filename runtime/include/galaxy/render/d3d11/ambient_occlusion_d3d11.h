// SPDX-License-Identifier: GPL-3.0-only
// Ambient occlusion on Direct3D 11: ambient_occlusion.h's passes (the D3D12 model, ambient_occlusion.cpp: distance, GTAO
// or SSAO, denoise, then the multiply, which makes the full-size multiplier itself from the depth and the denoised
// occlusion) from the same shaders, which fxc already compiles for shader model 5.0 with plain registers (ao_*.h: b0,
// t0-t4, u0). record() unbinds the views it bound (outputs and inputs) and leaves its shaders, constants and viewport
// bound. Nothing runs, and no texture is held after 600 frames, while the occlusion is off (the renderer calls nothing).
#pragma once

#include "galaxy/render/ambient_occlusion_objects.h"
#include "galaxy/render/d3d11/d3d11_context.h"
#include "galaxy/render/gpu_upload_ring.h"
#include "galaxy/render/rt_types.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace galaxy::render::d3d11 {

class AmbientOcclusion {
public:
    AmbientOcclusion() = default;
    AmbientOcclusion(const AmbientOcclusion&) = delete;
    AmbientOcclusion& operator=(const AmbientOcclusion&) = delete;
    ~AmbientOcclusion() { shutdown(); }

    bool initialize(ID3D11Device1* device);
    void shutdown() noexcept;
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
        UploadRing* ring = nullptr;             // unused on Direct3D 11: the models go to buffers of this pass's own
        RtView view{};                          // the scene's projection, depth range and viewport (scene target pixels)
    };
    // The occlusion, multiplied into `color`'s RGB. False when nothing was drawn.
    bool record(ID3D11DeviceContext1* context, const Inputs& in);
    // Once a frame: the targets no record() asked for in 600 frames are handed back.
    void trim_idle();

private:
    struct Rows {  // a structured buffer the CPU rewrites each frame, grown to the largest asked for
        ComPtr<ID3D11Buffer> buffer;
        ComPtr<ID3D11ShaderResourceView> view;
        std::uint32_t capacity = 0;  // elements
    };
    bool ensure(std::shared_ptr<Texture>& target, DXGI_FORMAT format, unsigned width, unsigned height, const char* name);
    bool upload(ID3D11DeviceContext1* context, Rows& rows, const void* data, std::uint32_t count, std::uint32_t stride);
    void dispatch(ID3D11DeviceContext1* context, ID3D11ComputeShader* shader, ID3D11ShaderResourceView* occlusion,
                  Texture& output, unsigned width, unsigned height);

    ID3D11Device1* device_ = nullptr;
    bool ready_ = false;
    ComPtr<ID3D11ComputeShader> distance_pass_;
    ComPtr<ID3D11ComputeShader> occlusion_;
    ComPtr<ID3D11ComputeShader> ssao_;
    ComPtr<ID3D11ComputeShader> denoise_;
    ComPtr<ID3D11VertexShader> apply_vs_;
    ComPtr<ID3D11PixelShader> apply_ps_;
    ComPtr<ID3D11Buffer> constants_;
    ComPtr<ID3D11BlendState> multiply_;
    ComPtr<ID3D11RasterizerState> raster_;
    ComPtr<ID3D11DepthStencilState> no_depth_;
    std::shared_ptr<Texture> distance_;  // eye distance per occlusion pixel (R32F)
    std::shared_ptr<Texture> raw_;       // occlusion, eye distance (RGBA16F, two used)
    std::shared_ptr<Texture> denoised_;
    Rows objects_;  // t3: the models (float4 rows)
    Rows tiles_;    // t4: their screen tiles (uint)
    std::vector<float> object_values_;
    std::vector<std::uint32_t> object_tiles_;
    ao::ObjectBinScratch object_scratch_;
    std::uint64_t clock_ = 0;  // trim_idle() calls
    std::uint64_t used_ = 0;   // the clock at the last record()
};

}  // namespace galaxy::render::d3d11
