// SPDX-License-Identifier: GPL-3.0-only
// TextureStore's D3D12 side (gpu_texture_backend.h): the view heap, upload staging, placed textures and upload copies.

#include "galaxy/render/gpu_texture_backend.h"
#include "barriers_d3d12.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cstdio>
#include <sstream>
#include <stdexcept>
#include <string>

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
    // Direct host writes: only where the CPU and the GPU share one cache-coherent memory (write-back custom heaps are valid
    // there and nowhere else).
    direct_ = false;
    D3D12_FEATURE_DATA_ARCHITECTURE architecture{};
    if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_ARCHITECTURE, &architecture, sizeof(architecture))) &&
        architecture.UMA == TRUE && architecture.CacheCoherentUMA == TRUE) {
        direct_heap_.initialize(device);
        direct_ = true;
    }
    {
        // The path, once (again only if a new device chooses another).
        static std::atomic<int> logged{-1};
        const int path = direct_ ? 1 : 0;
        if (logged.exchange(path) != path) {
            std::fprintf(stderr, "[textures] uploads: %s\n",
                         direct_ ? "direct host writes (WriteToSubresource into write-back memory, no copy command)"
                                 : "staged copies");
        }
    }

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
    drain();
    async_.clear();
    named_.clear();
    copy_list_.Reset();
    acquire_list_.Reset();
    copy_allocators_.clear();
    acquire_allocators_.clear();
    slot_value_.clear();
    copy_fence_.Reset();
    copy_queue_.Reset();
    if (copy_event_ != nullptr) {
        CloseHandle(copy_event_);
        copy_event_ = nullptr;
    }
    copy_value_ = 0u;
    copy_open_ = false;
    texture_heap_.shutdown();
    direct_heap_.shutdown();
    direct_ = false;
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

    // Direct mode: write-back system memory the CPU reads back quickly (the texture upload writes from it) and the GPU reads
    // coherently (movie planes still copy from it); an UPLOAD heap is write-combined, which the CPU reads slowly.
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
    bool cached = false;
    if (direct_) {
        // The adapter that refuses it here keeps the UPLOAD heap: the levels are still read from it, only slower.
        cached = SUCCEEDED(device_->CreateCommittedResource(
            &kDirectProperties, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&chunk.resource)));
    }
    if (!cached) {
        d3d_check(device_->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr, IID_PPV_ARGS(&chunk.resource)),
            "create texture upload arena");
    }
    D3D12_RANGE no_read{0, 0};
    void* mapped = nullptr;
    d3d_check(
        chunk.resource->Map(0, cached ? nullptr : &no_read, &mapped),
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

bool TextureBackend::direct_written(ID3D12Resource* texture) noexcept {
    D3D12_HEAP_PROPERTIES properties{};
    D3D12_HEAP_FLAGS flags{};
    return texture != nullptr && SUCCEEDED(texture->GetHeapProperties(&properties, &flags)) &&
           properties.Type == D3D12_HEAP_TYPE_CUSTOM && properties.CPUPageProperty == D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;
}

gpu::TextureRef TextureBackend::create_texture(
    std::uint32_t width, std::uint32_t height, unsigned levels, std::uint64_t& allocation_bytes, TexelFormat format,
    bool writable) {
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
    if (copy_queue_ != nullptr) {
        // Born in the common state, where the copy queue writes it and the graphics queue picks it up (submit_uploads).
        if (SUCCEEDED(texture_heap_.create(rd, gpu::Use::Common, nullptr, resource, allocation_bytes))) {
            async_.insert_or_assign(resource.Get(), Async{});  // a stale entry of a freed texture at this address goes
            return resource;
        }
        resource.Reset();
    }
    if (writable && direct_) {
        // Born in the shader-resource state, where every uploaded texture rests (a texture is promoted from the common state
        // only with simultaneous access); the levels are written from the CPU by record_upload. A heap that refuses it falls
        // back to the copy path below.
        if (SUCCEEDED(direct_heap_.create(rd, gpu::Use::PixelRead, nullptr, resource, allocation_bytes))) {
            return resource;
        }
        resource.Reset();
    }
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
    if (direct_ && direct_written(texture)) {
        // Written from the staged levels by the CPU, no command and no state change (a new texture, which no frame reads yet).
        const DXGI_FORMAT format = texture->GetDesc().Format;
        const bool blocks = format == DXGI_FORMAT_BC1_UNORM;
        for (unsigned l = 0; l < levels; ++l) {
            const std::uint32_t lh = std::max<std::uint32_t>(1u, height >> l);
            const std::uint32_t rows = blocks ? (lh + 3u) / 4u : lh;
            d3d_check(texture->WriteToSubresource(l, nullptr, staging.cpu_base + offset + offsets[l], pitches[l],
                                                  pitches[l] * rows),
                      "write texture level");
        }
        return;
    }
    if (copy_queue_ != nullptr) {
        // A texture the graphics queue has not acquired yet (new, or rewritten before any frame named it) goes through the
        // copy queue again; one it has acquired rests in the shader-resource state and is rewritten on the graphics list.
        if (const auto found = async_.find(texture); found != async_.end()) {
            record_async(texture, found->second, staging, offset, width, height, levels, offsets, pitches);
            return;
        }
    }
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

namespace {

// One level's footprint copy from a staging buffer.
void copy_level(ID3D12GraphicsCommandList* list, ID3D12Resource* texture, ID3D12Resource* staging, unsigned level,
                std::uint32_t width, std::uint32_t height, std::uint64_t offset, std::uint32_t pitch) {
    const DXGI_FORMAT format = texture->GetDesc().Format;
    const bool blocks = format == DXGI_FORMAT_BC1_UNORM;
    D3D12_TEXTURE_COPY_LOCATION copy_dst{};
    copy_dst.pResource = texture;
    copy_dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    copy_dst.SubresourceIndex = level;
    D3D12_TEXTURE_COPY_LOCATION copy_src{};
    copy_src.pResource = staging;
    copy_src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    copy_src.PlacedFootprint.Offset = offset;
    copy_src.PlacedFootprint.Footprint.Format = format;
    // A block format's footprint covers whole blocks, also at levels smaller than one.
    copy_src.PlacedFootprint.Footprint.Width = blocks ? (width + 3u) & ~3u : width;
    copy_src.PlacedFootprint.Footprint.Height = blocks ? (height + 3u) & ~3u : height;
    copy_src.PlacedFootprint.Footprint.Depth = 1;
    copy_src.PlacedFootprint.Footprint.RowPitch = pitch;
    list->CopyTextureRegion(&copy_dst, 0, 0, 0, &copy_src, nullptr);
}

}  // namespace

bool TextureBackend::enable_async_uploads(unsigned frames_in_flight, unsigned frame_slot) {
    copy_queue_.Reset();
    if (device_ == nullptr || direct_ || frames_in_flight == 0u) {
        return false;
    }
    D3D12_COMMAND_QUEUE_DESC desc{};
    desc.Type = D3D12_COMMAND_LIST_TYPE_COPY;
    desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    bool ok = SUCCEEDED(device_->CreateCommandQueue(&desc, IID_PPV_ARGS(&copy_queue_))) &&
              SUCCEEDED(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&copy_fence_)));
    copy_event_ = ok ? CreateEventW(nullptr, FALSE, FALSE, nullptr) : nullptr;
    ok = ok && copy_event_ != nullptr;
    copy_allocators_.assign(frames_in_flight, nullptr);
    acquire_allocators_.assign(frames_in_flight, nullptr);
    for (unsigned i = 0; ok && i < frames_in_flight; ++i) {
        ok = SUCCEEDED(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COPY, IID_PPV_ARGS(&copy_allocators_[i]))) &&
             SUCCEEDED(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&acquire_allocators_[i])));
    }
    ok = ok && SUCCEEDED(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COPY, copy_allocators_[0].Get(), nullptr,
                                                    IID_PPV_ARGS(&copy_list_))) &&
         SUCCEEDED(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, acquire_allocators_[0].Get(), nullptr,
                                              IID_PPV_ARGS(&acquire_list_)));
    if (ok) {
        // Both lists start closed and are reset when a frame first uses them.
        ok = SUCCEEDED(copy_list_->Close()) && SUCCEEDED(acquire_list_->Close());
    }
    if (!ok) {
        copy_list_.Reset();
        acquire_list_.Reset();
        copy_allocators_.clear();
        acquire_allocators_.clear();
        copy_fence_.Reset();
        copy_queue_.Reset();
        if (copy_event_ != nullptr) {
            CloseHandle(copy_event_);
            copy_event_ = nullptr;
        }
        std::fprintf(stderr, "[textures] uploads: the copy queue could not be made; the frame's queue uploads\n");
        return false;
    }
    copy_value_ = 0u;
    copy_open_ = false;
    slot_value_.assign(frames_in_flight, 0u);
    slot_ = frame_slot < frames_in_flight ? frame_slot : 0u;
    std::fprintf(stderr, "[textures] uploads: copy queue (textures are written beside the frame and acquired by its first use)\n");
    return true;
}

void TextureBackend::wait_copy(std::uint64_t value) {
    if (copy_fence_ == nullptr || value == 0u || copy_fence_->GetCompletedValue() >= value) {
        return;
    }
    d3d_check(copy_fence_->SetEventOnCompletion(value, copy_event_), "copy fence event");
    // 10 s: past that the device is lost, as the frame fence's wait decides.
    if (WaitForSingleObject(copy_event_, 10000u) != WAIT_OBJECT_0 && copy_fence_->GetCompletedValue() < value) {
        throw std::runtime_error("device lost: the copy queue never finished an upload");
    }
}

void TextureBackend::drain() noexcept {
    if (copy_queue_ == nullptr) {
        return;
    }
    // The copy queue may still read staging memory and write textures.
    try {
        wait_copy(copy_value_);
    } catch (const std::exception&) {
    }
}

void TextureBackend::begin_frame(unsigned frame_slot) {
    if (copy_queue_ == nullptr || frame_slot >= slot_value_.size()) {
        return;
    }
    slot_ = frame_slot;
    // What this slot's last frame sent the copy queue is done before its staging is written again.
    wait_copy(slot_value_[frame_slot]);
    d3d_check(copy_allocators_[frame_slot]->Reset(), "reset copy allocator");
    d3d_check(acquire_allocators_[frame_slot]->Reset(), "reset acquire allocator");
    copy_open_ = false;
}

ID3D12GraphicsCommandList* TextureBackend::open_copy_list() {
    if (!copy_open_) {
        d3d_check(copy_list_->Reset(copy_allocators_[slot_].Get(), nullptr), "reset copy list");
        copy_open_ = true;
    }
    return copy_list_.Get();
}

void TextureBackend::note_named_slow(ID3D12Resource* texture) {
    const auto found = async_.find(texture);
    if (found != async_.end() && !found->second.named) {
        found->second.named = true;
        named_.push_back(texture);
    }
}

void TextureBackend::forget_slow(ID3D12Resource* texture) {
    if (async_.erase(texture) != 0u) {
        const auto at = std::find(named_.begin(), named_.end(), texture);
        if (at != named_.end()) {
            *at = named_.back();
            named_.pop_back();
        }
    }
}

void TextureBackend::record_async(ID3D12Resource* texture, Async& state, const UploadChunk& staging, std::uint64_t offset,
                                  std::uint32_t width, std::uint32_t height, unsigned levels, const std::uint64_t* offsets,
                                  const std::uint32_t* pitches) {
    ID3D12GraphicsCommandList* const list = open_copy_list();
    // Legacy states: the copy queue takes a texture from common to copy-destination and back. Enhanced barriers: a texture
    // in the common layout is copied to and from on any queue without a barrier.
    const bool states = !BarrierPath::enhanced();
    if (states) {
        BarriersD3D12 before{list};
        before.texture(texture, gpu::Use::Common, gpu::Use::CopyDest);
        before.issue();
    }
    for (unsigned l = 0; l < levels; ++l) {
        copy_level(list, texture, staging.resource.Get(), l, std::max<std::uint32_t>(1u, width >> l),
                   std::max<std::uint32_t>(1u, height >> l), offset + offsets[l], pitches[l]);
    }
    if (states) {
        BarriersD3D12 after{list};
        after.texture(texture, gpu::Use::CopyDest, gpu::Use::Common);
        after.issue();
    }
    // Carried by the list that closes at the next submit_uploads.
    state.value = copy_value_ + 1u;
}

ID3D12CommandList* TextureBackend::submit_uploads(ID3D12CommandQueue* graphics) {
    if (copy_queue_ == nullptr) {
        return nullptr;
    }
    if (copy_open_) {
        d3d_check(copy_list_->Close(), "close copy list");
        ID3D12CommandList* lists[] = {copy_list_.Get()};
        copy_queue_->ExecuteCommandLists(1, lists);
        d3d_check(copy_queue_->Signal(copy_fence_.Get(), ++copy_value_), "copy queue signal");
        copy_open_ = false;
    }
    slot_value_[slot_] = copy_value_;
    if (named_.empty()) {
        return nullptr;
    }
    // The textures this frame names, uploaded or not yet: an unuploaded one (made, never written) stays for later.
    std::uint64_t wait = 0u;
    d3d_check(acquire_list_->Reset(acquire_allocators_[slot_].Get(), nullptr), "reset acquire list");
    BarriersD3D12 acquire{acquire_list_.Get()};
    for (ID3D12Resource* const texture : named_) {
        const auto found = async_.find(texture);
        if (found == async_.end()) {
            continue;
        }
        if (found->second.value == 0u) {
            found->second.named = false;  // nothing written yet: named again when it is
            continue;
        }
        wait = std::max(wait, found->second.value);
        acquire.texture(texture, gpu::Use::Common, gpu::Use::PixelRead);
        async_.erase(found);
    }
    named_.clear();
    acquire.issue();
    d3d_check(acquire_list_->Close(), "close acquire list");
    if (wait == 0u) {
        return nullptr;
    }
    d3d_check(graphics->Wait(copy_fence_.Get(), wait), "wait for the copy queue");
    return acquire_list_.Get();
}

}  // namespace galaxy::render
