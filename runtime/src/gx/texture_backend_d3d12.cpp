// SPDX-License-Identifier: GPL-3.0-only
// TextureCache's D3D12 side (texture_backend.h): the view heap, upload staging, placed textures and upload copies.

#include "galaxy/gx/texture_backend.h"

#include <bit>
#include <sstream>
#include <stdexcept>

namespace galaxy::gx {

namespace {

[[noreturn]] void d3d_fail(HRESULT hr, const char* label) {
    std::ostringstream ss;
    ss << "[TextureCache] " << label
       << " HRESULT=0x" << std::hex << std::uppercase
       << std::bit_cast<std::uint32_t>(static_cast<std::int32_t>(hr));
    throw std::runtime_error(ss.str());
}

void d3d_check(HRESULT hr, const char* label) {
    if (FAILED(hr)) { d3d_fail(hr, label); }
}

}  // namespace

bool TextureBackend::initialize(gpu::Device* device, std::uint32_t view_capacity) {
    device_ = device;
    texture_heap_.initialize(device);

    D3D12_DESCRIPTOR_HEAP_DESC dhd{};
    dhd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    // Permanent null SRV is outside the allocatable texture slots.
    dhd.NumDescriptors = view_capacity + 1u;
    dhd.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE; // CPU-visible only
    dhd.NodeMask       = 0;
    if (FAILED(device->CreateDescriptorHeap(&dhd, IID_PPV_ARGS(&srv_heap_)))) {
        return false;
    }
    srv_stride_ = device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    srv_cpu_base_ = srv_heap_->GetCPUDescriptorHandleForHeapStart();
    null_srv_ = srv_cpu_base_;
    null_srv_.ptr += static_cast<SIZE_T>(view_capacity) * srv_stride_;
    D3D12_SHADER_RESOURCE_VIEW_DESC null_desc{};
    null_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    null_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    null_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    null_desc.Texture2D.MipLevels = 1u;
    device->CreateShaderResourceView(nullptr, &null_desc, null_srv_);
    return true;
}

void TextureBackend::shutdown() {
    srv_heap_.Reset();
    srv_cpu_base_ = {};
    srv_stride_ = 0u;
    null_srv_ = {};
    device_.Reset();
}

void TextureBackend::write_view(std::uint32_t index, ID3D12Resource* texture, unsigned levels) {
    const bool grey = texture->GetDesc().Format == DXGI_FORMAT_R8_UNORM;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc{};
    srv_desc.Format                        = grey ? DXGI_FORMAT_R8_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM;
    srv_desc.ViewDimension                 = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Shader4ComponentMapping       = grey ? D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(0, 0, 0, 0)
                                                  : D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv_desc.Texture2D.MipLevels           = static_cast<UINT>(levels);
    srv_desc.Texture2D.MostDetailedMip     = 0;
    srv_desc.Texture2D.PlaneSlice          = 0;
    srv_desc.Texture2D.ResourceMinLODClamp = 0.0f;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = srv_cpu_base_;
    cpu.ptr += static_cast<SIZE_T>(index) * srv_stride_;
    device_->CreateShaderResourceView(texture, &srv_desc, cpu);
}

TextureBackend::UploadChunk TextureBackend::create_upload_chunk(std::uint64_t size) {
    UploadChunk chunk{};
    chunk.size = size;

    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width            = size;
    rd.Height           = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels        = 1;
    rd.Format           = DXGI_FORMAT_UNKNOWN;
    rd.SampleDesc.Count = 1;
    rd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    d3d_check(device_->CreateCommittedResource(
        &hp, D3D12_HEAP_FLAG_NONE, &rd,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr, IID_PPV_ARGS(&chunk.resource)),
        "create texture upload arena");
    D3D12_RANGE no_read{0, 0};
    void* mapped = nullptr;
    d3d_check(
        chunk.resource->Map(0, &no_read, &mapped),
        "map texture upload arena");
    chunk.cpu_base = static_cast<std::uint8_t*>(mapped);
    return chunk;
}

void TextureBackend::release_upload_chunk(UploadChunk& chunk) noexcept {
    if (chunk.resource && chunk.cpu_base != nullptr) {
        chunk.resource->Unmap(0, nullptr);
        chunk.cpu_base = nullptr;
    }
}

gpu::TextureRef TextureBackend::create_texture(
    std::uint32_t width, std::uint32_t height, unsigned levels, std::uint64_t& allocation_bytes, bool grey) {
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width            = width;
    rd.Height           = height;
    rd.DepthOrArraySize = 1;
    rd.MipLevels        = static_cast<UINT16>(levels);
    rd.Format           = grey ? DXGI_FORMAT_R8_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM;
    rd.SampleDesc.Count = 1;
    rd.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    gpu::TextureRef resource;
    d3d_check(texture_heap_.create(
        rd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, resource, allocation_bytes),
        "create default texture");
    return resource;
}

bool TextureBackend::poolable(ID3D12Resource* texture, std::uint32_t& width, std::uint32_t& height,
                              std::uint32_t& levels) {
    const D3D12_RESOURCE_DESC desc = texture->GetDesc();
    if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
        desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM ||
        desc.Flags != D3D12_RESOURCE_FLAG_NONE || desc.DepthOrArraySize != 1u ||
        desc.SampleDesc.Count != 1u) {
        return false;
    }
    width = static_cast<std::uint32_t>(desc.Width);
    height = static_cast<std::uint32_t>(desc.Height);
    levels = static_cast<std::uint32_t>(desc.MipLevels);
    return true;
}

bool TextureBackend::alias_compatible(ID3D12Resource* texture, std::uint32_t& width, std::uint32_t& height) {
    const D3D12_RESOURCE_DESC resource_desc = texture->GetDesc();
    if (resource_desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
        (resource_desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM && resource_desc.Format != DXGI_FORMAT_R8_UNORM) ||
        resource_desc.DepthOrArraySize != 1u || resource_desc.SampleDesc.Count != 1u ||
        resource_desc.Width == 0u || resource_desc.Width > UINT16_MAX ||
        resource_desc.Height == 0u || resource_desc.Height > UINT16_MAX) {
        return false;
    }
    width = static_cast<std::uint32_t>(resource_desc.Width);
    height = static_cast<std::uint32_t>(resource_desc.Height);
    return true;
}

void TextureBackend::record_upload(ID3D12Resource* texture, bool reused, const UploadChunk& staging,
                                   std::uint64_t offset, std::uint32_t width, std::uint32_t height, unsigned levels,
                                   const std::uint64_t* offsets, const std::uint32_t* pitches) {
    if (reused) {
        // A retired texture rests in the shader-resource state; the copies need copy-destination.
        D3D12_RESOURCE_BARRIER bar{};
        bar.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        bar.Transition.pResource   = texture;
        bar.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        bar.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        bar.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_DEST;
        upload_list_->ResourceBarrier(1, &bar);
    }
    for (unsigned l = 0; l < levels; ++l) {
        const std::uint32_t lw = std::max<std::uint32_t>(1u, width >> l);
        const std::uint32_t lh = std::max<std::uint32_t>(1u, height >> l);

        D3D12_TEXTURE_COPY_LOCATION copy_dst{};
        copy_dst.pResource        = texture;
        copy_dst.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        copy_dst.SubresourceIndex = l;

        D3D12_TEXTURE_COPY_LOCATION copy_src{};
        copy_src.pResource                          = staging.resource.Get();
        copy_src.Type                               = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        copy_src.PlacedFootprint.Offset             = offset + offsets[l];
        copy_src.PlacedFootprint.Footprint.Format   = texture->GetDesc().Format;
        copy_src.PlacedFootprint.Footprint.Width    = lw;
        copy_src.PlacedFootprint.Footprint.Height   = lh;
        copy_src.PlacedFootprint.Footprint.Depth    = 1;
        copy_src.PlacedFootprint.Footprint.RowPitch = pitches[l];

        upload_list_->CopyTextureRegion(&copy_dst, 0, 0, 0, &copy_src, nullptr);
    }
    D3D12_RESOURCE_BARRIER bar{};
    bar.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    bar.Transition.pResource   = texture;
    bar.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    bar.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    bar.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    upload_list_->ResourceBarrier(1, &bar);
}

}  // namespace galaxy::gx
