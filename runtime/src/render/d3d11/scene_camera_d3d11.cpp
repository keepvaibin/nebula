// SPDX-License-Identifier: GPL-3.0-only
// The scene camera on Direct3D 11 (d3d11/scene_camera_d3d11.h), pass for pass scene_camera.cpp's.
#include "galaxy/render/d3d11/scene_camera_d3d11.h"

#include "galaxy/render/scene_camera_shape.h"

#include <cstring>
#include <iostream>

#include "camera_meter_cs.h"
#include "camera_ps.h"
#include "camera_resolve_ps.h"
#include "camera_samples_ps.h"
#include "camera_vs.h"
#include "camera_widen_ps.h"

namespace galaxy::render::d3d11 {

bool SceneCamera::initialize(ID3D11Device1* device) {
    shutdown();
    if (device == nullptr) {
        return false;
    }
    D3D11_BUFFER_DESC cb{};
    cb.ByteWidth = 16u * ((sizeof(camera::Constants) + 15u) / 16u);
    cb.Usage = D3D11_USAGE_DYNAMIC;
    cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    D3D11_RASTERIZER_DESC raster{};
    raster.FillMode = D3D11_FILL_SOLID;
    raster.CullMode = D3D11_CULL_NONE;
    raster.DepthClipEnable = TRUE;
    D3D11_DEPTH_STENCIL_DESC depth{};
    depth.DepthEnable = FALSE;
    depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    depth.DepthFunc = D3D11_COMPARISON_ALWAYS;
    if (FAILED(device->CreateComputeShader(g_camera_meter_cs, sizeof(g_camera_meter_cs), nullptr, &meter_)) ||
        FAILED(device->CreateVertexShader(g_camera_vs, sizeof(g_camera_vs), nullptr, &vs_)) ||
        FAILED(device->CreatePixelShader(g_camera_ps, sizeof(g_camera_ps), nullptr, &camera_)) ||
        FAILED(device->CreatePixelShader(g_camera_samples_ps, sizeof(g_camera_samples_ps), nullptr, &camera_samples_)) ||
        FAILED(device->CreatePixelShader(g_camera_resolve_ps, sizeof(g_camera_resolve_ps), nullptr, &camera_resolve_)) ||
        FAILED(device->CreatePixelShader(g_camera_widen_ps, sizeof(g_camera_widen_ps), nullptr, &widen_)) ||
        FAILED(device->CreateBuffer(&cb, nullptr, &constants_)) ||
        FAILED(device->CreateRasterizerState(&raster, &raster_)) ||
        FAILED(device->CreateDepthStencilState(&depth, &no_depth_))) {
        std::cerr << "[scene-camera] Direct3D 11 shaders or states failed\n";
        shutdown();
        return false;
    }
    exposure_ = Context::get().create_texture(2u, 1u, 1u, DXGI_FORMAT_R32_FLOAT,
                                              D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
                                              "scene camera exposure");
    if (!exposure_) {
        std::cerr << "[scene-camera] exposure target failed\n";
        shutdown();
        return false;
    }
    ready_ = true;
    return true;
}

void SceneCamera::shutdown() noexcept {
    exposure_ready_ = false;
    ready_ = false;
    meter_.Reset();
    vs_.Reset();
    camera_.Reset();
    camera_samples_.Reset();
    camera_resolve_.Reset();
    widen_.Reset();
    constants_.Reset();
    raster_.Reset();
    no_depth_.Reset();
    exposure_.reset();
}

void SceneCamera::set_constants(ID3D11DeviceContext1* context, const void* constants, unsigned bytes) {
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (SUCCEEDED(context->Map(constants_.Get(), 0u, D3D11_MAP_WRITE_DISCARD, 0u, &mapped))) {
        std::memcpy(mapped.pData, constants, bytes);
        context->Unmap(constants_.Get(), 0u);
    }
}

// A full-screen triangle into `rtv` reading `picture` (t0) and the exposure (t1); the views let go after.
void SceneCamera::draw(ID3D11DeviceContext1* context, ID3D11PixelShader* shader, ID3D11RenderTargetView* rtv,
                       ID3D11ShaderResourceView* picture, unsigned width, unsigned height) {
    context->OMSetRenderTargets(1u, &rtv, nullptr);
    ID3D11ShaderResourceView* const inputs[2] = {picture, exposure_->view.Get()};
    context->PSSetShaderResources(0u, 2u, inputs);
    context->VSSetConstantBuffers(0u, 1u, constants_.GetAddressOf());
    context->PSSetConstantBuffers(0u, 1u, constants_.GetAddressOf());
    context->IASetInputLayout(nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(vs_.Get(), nullptr, 0u);
    context->PSSetShader(shader, nullptr, 0u);
    context->RSSetState(raster_.Get());
    context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFFu);
    context->OMSetDepthStencilState(no_depth_.Get(), 0u);
    const D3D11_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f};
    context->RSSetViewports(1u, &viewport);
    context->Draw(3u, 0u);
    ID3D11ShaderResourceView* const none[2] = {};
    context->PSSetShaderResources(0u, 2u, none);
    context->OMSetRenderTargets(0u, nullptr, nullptr);
}

bool SceneCamera::record(ID3D11DeviceContext1* context, const Inputs& in) {
    if (!ready_ || in.wide == nullptr || in.depth == nullptr || in.rtv == nullptr || in.width == 0u || in.height == 0u ||
        !in.wide->view || !in.depth->view) {
        return false;
    }
    const bool samples = in.samples > 1u && in.wide_ms != nullptr && in.wide_ms->view;
    ID3D11PixelShader* const shader =
        samples ? (in.resolve ? camera_resolve_.Get() : camera_samples_.Get()) : camera_.Get();
    camera::Constants constants{};
    constants.width = static_cast<float>(in.width);
    constants.height = static_cast<float>(in.height);
    constants.seconds = exposure_ready_ ? in.seconds : -1.0f;
    exposure_ready_ = true;
    set_constants(context, &constants, sizeof(constants));

    // The meter: the resolved picture (t0) and the depth (t2) read, the exposure (u0) read and written. No target stays
    // bound over the pictures or the depth.
    context->OMSetRenderTargets(0u, nullptr, nullptr);
    ID3D11ShaderResourceView* const none[3] = {};
    context->PSSetShaderResources(0u, 3u, none);
    ID3D11UnorderedAccessView* const storage = exposure_->storage.Get();
    context->CSSetUnorderedAccessViews(0u, 1u, &storage, nullptr);
    ID3D11ShaderResourceView* const meter_inputs[3] = {in.wide->view.Get(), nullptr, in.depth->view.Get()};
    context->CSSetShaderResources(0u, 3u, meter_inputs);
    context->CSSetConstantBuffers(0u, 1u, constants_.GetAddressOf());
    context->CSSetShader(meter_.Get(), nullptr, 0u);
    context->Dispatch(1u, 1u, 1u);
    ID3D11UnorderedAccessView* const no_storage = nullptr;
    context->CSSetShaderResources(0u, 3u, none);
    context->CSSetUnorderedAccessViews(0u, 1u, &no_storage, nullptr);

    // The camera: every pixel (every sample) under the exposure and the shoulder into the scene target.
    draw(context, shader, in.rtv, (samples ? in.wide_ms : in.wide)->view.Get(), in.width, in.height);
    return true;
}

bool SceneCamera::widen(ID3D11DeviceContext1* context, const WidenInputs& in) {
    if (!ready_ || in.picture == nullptr || !in.picture->view || in.rtv == nullptr || in.width == 0u ||
        in.height == 0u) {
        return false;
    }
    draw(context, widen_.Get(), in.rtv, in.picture->view.Get(), in.width, in.height);
    return true;
}

}  // namespace galaxy::render::d3d11
