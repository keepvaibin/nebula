// SPDX-License-Identifier: GPL-3.0-only
// Ambient occlusion on Direct3D 11 (d3d11/ambient_occlusion_d3d11.h), pass for pass ambient_occlusion.cpp's.
#include "galaxy/render/d3d11/ambient_occlusion_d3d11.h"

#include "galaxy/render/ambient_occlusion_shape.h"

#include <algorithm>
#include <bit>
#include <cstring>
#include <iostream>

#include "ao_apply_ps.h"
#include "ao_apply_vs.h"
#include "ao_denoise_cs.h"
#include "ao_distance_cs.h"
#include "ao_occlusion_cs.h"
#include "ao_ssao_cs.h"

namespace galaxy::render::d3d11 {
namespace {

using ao::Constants;
// Occlusion and eye distance: the two channels the passes write and read (a typed store to R16G16 is in every
// feature level 11_0 device; the shaders' float4 writes keep x and y).
constexpr DXGI_FORMAT kOcclusionFormat = DXGI_FORMAT_R16G16_FLOAT;
constexpr DXGI_FORMAT kDistanceFormat = DXGI_FORMAT_R32_FLOAT;
constexpr std::uint64_t kIdleFrames = 600u;

}  // namespace

bool AmbientOcclusion::initialize(ID3D11Device1* device) {
    shutdown();
    device_ = device;
    if (device_ == nullptr) {
        return false;
    }
    D3D11_BUFFER_DESC cb{};
    cb.ByteWidth = 16u * ((sizeof(Constants) + 15u) / 16u);
    cb.Usage = D3D11_USAGE_DYNAMIC;
    cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    // The multiply: the shader's output times the scene target's colour (RGB; the game's alpha stays).
    D3D11_BLEND_DESC blend{};
    D3D11_RENDER_TARGET_BLEND_DESC& target = blend.RenderTarget[0];
    target.BlendEnable = TRUE;
    target.SrcBlend = D3D11_BLEND_ZERO;
    target.DestBlend = D3D11_BLEND_SRC_COLOR;
    target.BlendOp = D3D11_BLEND_OP_ADD;
    target.SrcBlendAlpha = D3D11_BLEND_ZERO;
    target.DestBlendAlpha = D3D11_BLEND_ONE;
    target.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    target.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN |
                                   D3D11_COLOR_WRITE_ENABLE_BLUE;
    D3D11_RASTERIZER_DESC raster{};
    raster.FillMode = D3D11_FILL_SOLID;
    raster.CullMode = D3D11_CULL_NONE;
    raster.DepthClipEnable = TRUE;
    D3D11_DEPTH_STENCIL_DESC depth{};
    depth.DepthEnable = FALSE;
    depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    depth.DepthFunc = D3D11_COMPARISON_ALWAYS;
    if (FAILED(device_->CreateComputeShader(g_ao_distance_cs, sizeof(g_ao_distance_cs), nullptr, &distance_pass_)) ||
        FAILED(device_->CreateComputeShader(g_ao_occlusion_cs, sizeof(g_ao_occlusion_cs), nullptr, &occlusion_)) ||
        FAILED(device_->CreateComputeShader(g_ao_ssao_cs, sizeof(g_ao_ssao_cs), nullptr, &ssao_)) ||
        FAILED(device_->CreateComputeShader(g_ao_denoise_cs, sizeof(g_ao_denoise_cs), nullptr, &denoise_)) ||
        FAILED(device_->CreateVertexShader(g_ao_apply_vs, sizeof(g_ao_apply_vs), nullptr, &apply_vs_)) ||
        FAILED(device_->CreatePixelShader(g_ao_apply_ps, sizeof(g_ao_apply_ps), nullptr, &apply_ps_)) ||
        FAILED(device_->CreateBuffer(&cb, nullptr, &constants_)) ||
        FAILED(device_->CreateBlendState(&blend, &multiply_)) ||
        FAILED(device_->CreateRasterizerState(&raster, &raster_)) ||
        FAILED(device_->CreateDepthStencilState(&depth, &no_depth_))) {
        std::cerr << "[ambient-occlusion] Direct3D 11 shaders or states failed\n";
        shutdown();
        return false;
    }
    ready_ = true;
    return true;
}

void AmbientOcclusion::shutdown() noexcept {
    ready_ = false;
    distance_pass_.Reset();
    occlusion_.Reset();
    ssao_.Reset();
    denoise_.Reset();
    apply_vs_.Reset();
    apply_ps_.Reset();
    constants_.Reset();
    multiply_.Reset();
    raster_.Reset();
    no_depth_.Reset();
    distance_.reset();
    raw_.reset();
    denoised_.reset();
    objects_ = {};
    tiles_ = {};
    device_ = nullptr;
}

bool AmbientOcclusion::ensure(std::shared_ptr<Texture>& target, DXGI_FORMAT format, unsigned width, unsigned height,
                              const char* name) {
    if (target && target->width == width && target->height == height) {
        return true;
    }
    target = Context::get().create_texture(width, height, 1u, format,
                                           D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS, name);
    return target != nullptr;
}

void AmbientOcclusion::trim_idle() {
    ++clock_;
    if (clock_ > used_ + kIdleFrames && (distance_ || raw_ || denoised_ || objects_.buffer || tiles_.buffer)) {
        distance_.reset();
        raw_.reset();
        denoised_.reset();
        objects_ = {};
        tiles_ = {};
    }
}

// `count` elements of `stride` bytes (at least one element: an empty list still binds a buffer) into `rows`.
bool AmbientOcclusion::upload(ID3D11DeviceContext1* context, Rows& rows, const void* data, std::uint32_t count,
                              std::uint32_t stride) {
    const std::uint32_t needed = std::max(count, 1u);
    if (!rows.buffer || rows.capacity < needed) {
        rows = {};
        const std::uint32_t capacity = std::bit_ceil(std::max(needed, 64u));
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = capacity * stride;
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        desc.StructureByteStride = stride;
        D3D11_SHADER_RESOURCE_VIEW_DESC view{};
        view.Format = DXGI_FORMAT_UNKNOWN;
        view.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        view.Buffer.FirstElement = 0u;
        view.Buffer.NumElements = capacity;
        if (FAILED(device_->CreateBuffer(&desc, nullptr, &rows.buffer)) ||
            FAILED(device_->CreateShaderResourceView(rows.buffer.Get(), &view, &rows.view))) {
            rows = {};
            return false;
        }
        rows.capacity = capacity;
    }
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(rows.buffer.Get(), 0u, D3D11_MAP_WRITE_DISCARD, 0u, &mapped))) {
        return false;
    }
    if (count != 0u) {
        std::memcpy(mapped.pData, data, static_cast<std::size_t>(count) * stride);
    } else {
        std::memset(mapped.pData, 0, stride);
    }
    context->Unmap(rows.buffer.Get(), 0u);
    return true;
}

// One compute pass: t1 the occlusion it reads (t0 the depth, t2 the distances, t3-t4 the models stay bound), u0 its
// output. Its output's old inputs are let go first (Direct3D 11 nulls a view bound for reading and writing at once).
void AmbientOcclusion::dispatch(ID3D11DeviceContext1* context, ID3D11ComputeShader* shader,
                                ID3D11ShaderResourceView* occlusion, Texture& output, unsigned width, unsigned height) {
    ID3D11ShaderResourceView* const none = nullptr;
    context->CSSetShaderResources(1u, 1u, &none);
    ID3D11UnorderedAccessView* const storage = output.storage.Get();
    context->CSSetUnorderedAccessViews(0u, 1u, &storage, nullptr);
    context->CSSetShaderResources(1u, 1u, &occlusion);
    context->CSSetShader(shader, nullptr, 0u);
    context->Dispatch((width + 7u) / 8u, (height + 7u) / 8u, 1u);
}

bool AmbientOcclusion::record(ID3D11DeviceContext1* context, const Inputs& in) {
    if (!ready_ || in.depth == nullptr || !in.depth->view || in.color == nullptr || in.width == 0u || in.height == 0u ||
        in.objects == nullptr) {
        return false;
    }
    used_ = clock_;
    const unsigned divisor = ao::occlusion_divisor(in.height);
    const unsigned width = (in.width + divisor - 1u) / divisor;
    const unsigned height = (in.height + divisor - 1u) / divisor;
    Constants c{};
    if (!ao::make_constants(in.view, width, height, divisor, in.ssao, c) ||
        !ensure(distance_, kDistanceFormat, width, height, "occlusion distance") ||
        !ensure(raw_, kOcclusionFormat, width, height, "occlusion") ||
        !ensure(denoised_, kOcclusionFormat, width, height, "occlusion denoised")) {
        return false;
    }
    ao::bin_objects(*in.objects, c, object_values_, object_tiles_, object_scratch_);
    if (!upload(context, objects_, object_values_.data(), static_cast<std::uint32_t>(object_values_.size() / 4u), 16u) ||
        !upload(context, tiles_, object_tiles_.data(), static_cast<std::uint32_t>(object_tiles_.size()), 4u)) {
        return false;
    }
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(constants_.Get(), 0u, D3D11_MAP_WRITE_DISCARD, 0u, &mapped))) {
        return false;
    }
    std::memcpy(mapped.pData, &c, sizeof(c));
    context->Unmap(constants_.Get(), 0u);

    // No target stays bound over the depth (its depth-stencil view) while it is read.
    context->OMSetRenderTargets(0u, nullptr, nullptr);
    context->CSSetConstantBuffers(0u, 1u, constants_.GetAddressOf());
    ID3D11ShaderResourceView* const fixed[5] = {in.depth->view.Get(), nullptr, nullptr, objects_.view.Get(),
                                                tiles_.view.Get()};
    context->CSSetShaderResources(0u, 5u, fixed);
    dispatch(context, distance_pass_.Get(), nullptr, *distance_, width, height);
    ID3D11UnorderedAccessView* const no_storage = nullptr;
    context->CSSetUnorderedAccessViews(0u, 1u, &no_storage, nullptr);
    ID3D11ShaderResourceView* const distances = distance_->view.Get();
    context->CSSetShaderResources(2u, 1u, &distances);
    dispatch(context, in.ssao ? ssao_.Get() : occlusion_.Get(), nullptr, *raw_, width, height);
    context->CSSetUnorderedAccessViews(0u, 1u, &no_storage, nullptr);
    dispatch(context, denoise_.Get(), raw_->view.Get(), *denoised_, width, height);
    context->CSSetUnorderedAccessViews(0u, 1u, &no_storage, nullptr);
    ID3D11ShaderResourceView* const none[5] = {};
    context->CSSetShaderResources(0u, 5u, none);

    // The multiply over the frame: t0 the depth, t1 the denoised occlusion; the multiplier made per pixel.
    ID3D11RenderTargetView* const color = in.color;
    context->OMSetRenderTargets(1u, &color, nullptr);
    ID3D11ShaderResourceView* const inputs[2] = {in.depth->view.Get(), denoised_->view.Get()};
    context->PSSetShaderResources(0u, 2u, inputs);
    context->VSSetConstantBuffers(0u, 1u, constants_.GetAddressOf());
    context->PSSetConstantBuffers(0u, 1u, constants_.GetAddressOf());
    context->IASetInputLayout(nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(apply_vs_.Get(), nullptr, 0u);
    context->PSSetShader(apply_ps_.Get(), nullptr, 0u);
    context->RSSetState(raster_.Get());
    context->OMSetBlendState(multiply_.Get(), nullptr, 0xFFFFFFFFu);
    context->OMSetDepthStencilState(no_depth_.Get(), 0u);
    const D3D11_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(in.width), static_cast<float>(in.height), 0.0f, 1.0f};
    context->RSSetViewports(1u, &viewport);
    context->Draw(3u, 0u);
    context->PSSetShaderResources(0u, 2u, none);
    context->OMSetRenderTargets(0u, nullptr, nullptr);
    return true;
}

}  // namespace galaxy::render::d3d11
