// SPDX-License-Identifier: GPL-3.0-only
// TextureStore's D3D12 side (gpu_texture_backend.h): the view heap, upload staging, placed textures and upload copies.

#include "galaxy/render/gpu_texture_backend.h"
#include "barriers_d3d12.h"

#include <algorithm>
#include <bit>
#include <sstream>
#include <stdexcept>

namespace galaxy::render {

namespace {

[[noreturn]] void d3d_fail(HRESULT hr, const char* label) {
    std::ostringstream ss;
    ss << "[TextureStore] " << label
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
    views_per_page_ = view_capacity;
    srv_pages_.clear();
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
    // Every view slot holds the null view from the start, so a copy never reads an undefined descriptor.
    fill_null(srv_cpu_base_, view_capacity);
    return true;
}

void TextureBackend::fill_null(D3D12_CPU_DESCRIPTOR_HANDLE first, std::uint32_t count) {
    if (count == 0u) {
        return;
    }
    device_->CopyDescriptorsSimple(1u, first, null_srv_, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    for (std::uint32_t done = 1u; done < count;) {
        const std::uint32_t run = std::min(done, count - done);
        D3D12_CPU_DESCRIPTOR_HANDLE to = first;
        to.ptr += static_cast<SIZE_T>(done) * srv_stride_;
        device_->CopyDescriptorsSimple(run, to, first, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        done += run;
    }
}

void TextureBackend::release_view(std::uint32_t index) {
    if (index >= views_made_) {
        return;
    }
    device_->CopyDescriptorsSimple(1u, srv_cpu(index), null_srv_, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    publish_view(index);
}

void TextureBackend::shutdown() {
    texture_heap_.shutdown();
    srv_heap_.Reset();
    srv_pages_.clear();
    views_per_page_ = 0;
    srv_cpu_base_ = {};
    srv_stride_ = 0u;
    null_srv_ = {};
    mirror_ = {};
    mirror_count_ = 0u;
    views_made_ = 0u;
    device_.Reset();
}

void TextureBackend::ensure_view(std::uint32_t index) {
    views_made_ = std::max(views_made_, index + 1u);
    const std::size_t page = index / views_per_page_;
    while (page > srv_pages_.size()) {
        D3D12_DESCRIPTOR_HEAP_DESC desc{};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        desc.NumDescriptors = views_per_page_;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> next;
        d3d_check(device_->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&next)), "create texture view page");
        fill_null(next->GetCPUDescriptorHandleForHeapStart(), views_per_page_);
        srv_pages_.push_back(std::move(next));
    }
}

D3D12_CPU_DESCRIPTOR_HANDLE TextureBackend::srv_cpu(std::uint32_t index) const noexcept {
    const auto page = index / views_per_page_;
    auto cpu = page == 0 ? srv_cpu_base_ : srv_pages_[page - 1]->GetCPUDescriptorHandleForHeapStart();
    cpu.ptr += static_cast<SIZE_T>(index % views_per_page_) * srv_stride_;
    return cpu;
}

void TextureBackend::write_view(std::uint32_t index, ID3D12Resource* texture, unsigned levels) {
    const DXGI_FORMAT format = texture->GetDesc().Format;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc{};
    srv_desc.Format                        = format;
    srv_desc.ViewDimension                 = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Shader4ComponentMapping =
        format == DXGI_FORMAT_R8_UNORM   ? D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(0, 0, 0, 0)
        : format == DXGI_FORMAT_R8G8_UNORM ? D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(0, 0, 0, 1)
                                           : D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv_desc.Texture2D.MipLevels           = static_cast<UINT>(levels);
    srv_desc.Texture2D.MostDetailedMip     = 0;
    srv_desc.Texture2D.PlaneSlice          = 0;
    srv_desc.Texture2D.ResourceMinLODClamp = 0.0f;
    ensure_view(index);
    const auto cpu = srv_cpu(index);
    device_->CreateShaderResourceView(texture, &srv_desc, cpu);
    publish_view(index);
}

void TextureBackend::mirror_into(D3D12_CPU_DESCRIPTOR_HANDLE slots, std::uint32_t count, GrowFn grow, void* owner) {
    if (views_made_ > count) {
        throw std::runtime_error("[textures] more texture views than the shader-visible heap holds");
    }
    mirror_ = slots;
    mirror_count_ = count;
    mirror_grow_ = grow;
    mirror_owner_ = owner;
    // The views made so far, one copy per page of the CPU views.
    for (std::uint32_t first = 0; first < views_made_;) {
        const std::uint32_t in_page = views_per_page_ - first % views_per_page_;
        const std::uint32_t run = std::min(in_page, views_made_ - first);
        D3D12_CPU_DESCRIPTOR_HANDLE slot = mirror_;
        slot.ptr += static_cast<SIZE_T>(first) * srv_stride_;
        device_->CopyDescriptorsSimple(run, slot, srv_cpu(first), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        first += run;
    }
}

void TextureBackend::publish_view(std::uint32_t index) {
    if (mirror_.ptr == 0u) {
        return;
    }
    if (index >= mirror_count_) {
        // A larger heap that already holds the earlier views (mirror_into); this one is copied below.
        if (mirror_grow_ != nullptr) {
            mirror_grow_(mirror_owner_, index);
        }
        if (index >= mirror_count_) {
            throw std::runtime_error("[textures] more texture views than the shader-visible heap holds");
        }
    }
    D3D12_CPU_DESCRIPTOR_HANDLE slot = mirror_;
    slot.ptr += static_cast<SIZE_T>(index) * srv_stride_;
    device_->CopyDescriptorsSimple(1u, slot, srv_cpu(index), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
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
    std::uint32_t width, std::uint32_t height, unsigned levels, std::uint64_t& allocation_bytes, TexelFormat format) {
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width            = width;
    rd.Height           = height;
    rd.DepthOrArraySize = 1;
    rd.MipLevels        = static_cast<UINT16>(levels);
    rd.Format           = format == TexelFormat::R8    ? DXGI_FORMAT_R8_UNORM
                          : format == TexelFormat::Rg8 ? DXGI_FORMAT_R8G8_UNORM
                          : format == TexelFormat::Bc1 ? DXGI_FORMAT_BC1_UNORM
                                                       : DXGI_FORMAT_R8G8B8A8_UNORM;
    rd.SampleDesc.Count = 1;
    rd.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    gpu::TextureRef resource;
    d3d_check(texture_heap_.create(
        rd, gpu::Use::CopyDest, nullptr, resource, allocation_bytes),
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

void TextureBackend::record_upload(ID3D12Resource* texture, bool reused, const UploadChunk& staging,
                                   std::uint64_t offset, std::uint32_t width, std::uint32_t height, unsigned levels,
                                   const std::uint64_t* offsets, const std::uint32_t* pitches) {
    if (reused) {
        // A retired texture rests in the shader-resource state; the copies need copy-destination.
        BarriersD3D12 uses{upload_list_};
        uses.texture(texture, gpu::Use::PixelRead, gpu::Use::CopyDest);
        uses.issue();
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
        // A block format's footprint covers whole blocks, also at levels smaller than one.
        const bool blocks = texture->GetDesc().Format == DXGI_FORMAT_BC1_UNORM;
        copy_src.PlacedFootprint.Footprint.Width    = blocks ? (lw + 3u) & ~3u : lw;
        copy_src.PlacedFootprint.Footprint.Height   = blocks ? (lh + 3u) & ~3u : lh;
        copy_src.PlacedFootprint.Footprint.Depth    = 1;
        copy_src.PlacedFootprint.Footprint.RowPitch = pitches[l];

        upload_list_->CopyTextureRegion(&copy_dst, 0, 0, 0, &copy_src, nullptr);
    }
    BarriersD3D12 uses{upload_list_};
    uses.texture(texture, gpu::Use::CopyDest, gpu::Use::PixelRead);
    uses.issue();
}

}  // namespace galaxy::render
