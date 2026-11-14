// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The GPU API types RenderCore names, chosen when the backend is built (docs/VULKAN.md): D3D12 on Windows, Vulkan
// (NEBULA_GPU_VULKAN) elsewhere and in the Windows Vulkan test build, Metal (NEBULA_GPU_METAL) on Apple, Direct3D 11
// (NEBULA_GPU_D3D11) for Windows adapters Direct3D 12 cannot use. Compile-time, so draws make no virtual calls.

#include <cstddef>
#include <cstdint>
#include <memory>

#if defined(NEBULA_GPU_VULKAN)
#include <volk.h>

namespace galaxy::render::gpu {

// Handles keep D3D12's field names where the shared code reads them, so one RenderCore compiles for both.
using Device = VkDevice_T;
using Pipeline = VkPipeline_T;
using PipelineRef = VkPipeline;
using Resource = VkBuffer_T;
// A 64-bit address in the renderer's own buffer address space: each buffer the old path binds owns a range, and the
// renderer turns an address back into (buffer, offset) when it binds it. Arithmetic on it works as on D3D12's.
using Address = std::uint64_t;
struct Table {
    std::uint64_t ptr = 0;  // VkDescriptorSet
};
struct VertexView {
    Address BufferLocation = 0;
    std::uint32_t SizeInBytes = 0;
    std::uint32_t StrideInBytes = 0;
};
struct IndexView {
    Address BufferLocation = 0;
    std::uint32_t SizeInBytes = 0;
    std::uint32_t Format = 0;  // 16-bit indices only
};
struct Rect {
    std::int32_t left = 0;
    std::int32_t top = 0;
    std::int32_t right = 0;
    std::int32_t bottom = 0;
};


[[nodiscard]] inline VertexView vertex_view(Address address, std::uint32_t size, std::uint32_t stride) noexcept {
    return VertexView{address, size, stride};
}

[[nodiscard]] inline IndexView index_view(Address address, std::uint32_t size) noexcept {
    return IndexView{address, size, 16u};
}

[[nodiscard]] inline Rect rect(std::int32_t left, std::int32_t top, std::int32_t right, std::int32_t bottom) noexcept {
    return Rect{left, top, right, bottom};
}

}  // namespace galaxy::render::gpu

namespace galaxy::render::vk {
struct Texture;  // vk/vk_context.h
struct Buffer;
}

namespace galaxy::render::gpu {

// An owned texture: the last reference frees it once the GPU is done with it (vk_context.h).
using TextureRef = std::shared_ptr<vk::Texture>;
// An owned buffer, freed as a texture is.
using BufferRef = std::shared_ptr<vk::Buffer>;
[[nodiscard]] Resource* raw(const BufferRef& buffer) noexcept;
using TextureObject = vk::Texture;
[[nodiscard]] inline TextureObject* raw(const TextureRef& texture) noexcept { return texture.get(); }
// Another reference to a texture a handle names (vk::Texture derives from enable_shared_from_this).
[[nodiscard]] TextureRef texture_ref(TextureObject* texture);
// A render target view: the texture's image view, recorded as the handle's bits.
struct CpuDescriptor {
    std::uint64_t ptr = 0;
};
// ComPtr<ID3D12Device>'s interface over a VkDevice the renderer owns.
struct DeviceRef {
    VkDevice handle = VK_NULL_HANDLE;
    DeviceRef& operator=(VkDevice device) noexcept {
        handle = device;
        return *this;
    }
    [[nodiscard]] VkDevice Get() const noexcept { return handle; }
    void Reset() noexcept { handle = VK_NULL_HANDLE; }
    explicit operator bool() const noexcept { return handle != VK_NULL_HANDLE; }
    bool operator!=(std::nullptr_t) const noexcept { return handle != VK_NULL_HANDLE; }
    bool operator==(std::nullptr_t) const noexcept { return handle == VK_NULL_HANDLE; }
};

// An RGBA8 texture that is rendered to and sampled (display picture copies); empty when it cannot be created.
[[nodiscard]] TextureRef create_render_texture(Device* device, std::uint32_t width, std::uint32_t height);

}  // namespace galaxy::render::gpu
#elif defined(NEBULA_GPU_METAL)
#include "galaxy/render/mtl/draw_types.h"

namespace galaxy::render::gpu {

// Metal (docs/APPLE.md). The names the shared code reads are those of the other two; Metal's own objects stay behind
// mtl_context.h's untyped pointers, so this header is plain C++.
struct Device;  // never defined: a Device* is mtl::Context's id<MTLDevice>
using Pipeline = mtl::Pipeline;
using PipelineRef = Pipeline*;  // owned by core::Pipelines, deleted in its destroy_all
using Resource = mtl::Buffer;
// A 64-bit address in the renderer's own buffer address space (mtl::Context::resolve), as on Vulkan.
using Address = std::uint64_t;
struct Table {
    std::uint64_t ptr = 0;  // mtl/draw_types.h: a bound table, or an array's buffer
};
struct VertexView {
    Address BufferLocation = 0;
    std::uint32_t SizeInBytes = 0;
    std::uint32_t StrideInBytes = 0;
};
struct IndexView {
    Address BufferLocation = 0;
    std::uint32_t SizeInBytes = 0;
    std::uint32_t Format = 0;  // 16-bit indices only
};
struct Rect {
    std::int32_t left = 0;
    std::int32_t top = 0;
    std::int32_t right = 0;
    std::int32_t bottom = 0;
};

[[nodiscard]] inline VertexView vertex_view(Address address, std::uint32_t size, std::uint32_t stride) noexcept {
    return VertexView{address, size, stride};
}

[[nodiscard]] inline IndexView index_view(Address address, std::uint32_t size) noexcept {
    return IndexView{address, size, 16u};
}

[[nodiscard]] inline Rect rect(std::int32_t left, std::int32_t top, std::int32_t right, std::int32_t bottom) noexcept {
    return Rect{left, top, right, bottom};
}

// An owned texture and an owned buffer: the last reference releases it once the GPU is done with it.
using TextureRef = std::shared_ptr<mtl::Texture>;
using BufferRef = std::shared_ptr<mtl::Buffer>;
[[nodiscard]] inline Resource* raw(const BufferRef& buffer) noexcept { return buffer.get(); }
using TextureObject = mtl::Texture;
[[nodiscard]] inline TextureObject* raw(const TextureRef& texture) noexcept { return texture.get(); }
// Another reference to a texture a handle names.
[[nodiscard]] inline TextureRef texture_ref(TextureObject* texture) {
    return texture != nullptr ? texture->shared_from_this() : TextureRef{};
}
// A render target: the texture itself.
struct CpuDescriptor {
    std::uint64_t ptr = 0;
};
// ComPtr<ID3D12Device>'s interface over the context's device.
struct DeviceRef {
    Device* handle = nullptr;
    DeviceRef& operator=(Device* device) noexcept {
        handle = device;
        return *this;
    }
    [[nodiscard]] Device* Get() const noexcept { return handle; }
    void Reset() noexcept { handle = nullptr; }
    explicit operator bool() const noexcept { return handle != nullptr; }
    bool operator!=(std::nullptr_t) const noexcept { return handle != nullptr; }
    bool operator==(std::nullptr_t) const noexcept { return handle == nullptr; }
};

// An RGBA8 texture that is rendered to and sampled (display picture copies); empty when it cannot be created.
[[nodiscard]] TextureRef create_render_texture(Device* device, std::uint32_t width, std::uint32_t height);

}  // namespace galaxy::render::gpu
#elif defined(NEBULA_GPU_D3D11)
#include "galaxy/windows_api.h"
#include <d3d11_1.h>
#include <wrl/client.h>

namespace galaxy::render::d3d11 {
struct Texture;     // d3d11/d3d11_context.h
struct Buffer;
struct Pipeline;    // one pipeline's state objects
struct BoundTable;  // a draw's own table of views or samplers
}

namespace galaxy::render::gpu {

// Direct3D 11 (feature level 11_0 and up): for the adapters Direct3D 12 passes over (renderer_d3d12.cpp
// try_adapter: resource binding tier 1, no shader model 6, or no Direct3D 12 driver). The names the shared code reads
// are those of the other back ends.
using Device = ID3D11Device1;
using DeviceRef = Microsoft::WRL::ComPtr<ID3D11Device1>;
using Pipeline = d3d11::Pipeline;
using PipelineRef = Pipeline*;  // owned by core::Pipelines, deleted in its destroy_all
using Resource = d3d11::Buffer;
// A 64-bit address in the renderer's own buffer address space (d3d11::Context::resolve), as on Vulkan and Metal.
using Address = std::uint64_t;
struct Table {
    std::uint64_t ptr = 0;  // const d3d11::BoundTable*
};
struct VertexView {
    Address BufferLocation = 0;
    std::uint32_t SizeInBytes = 0;
    std::uint32_t StrideInBytes = 0;
};
struct IndexView {
    Address BufferLocation = 0;
    std::uint32_t SizeInBytes = 0;
    std::uint32_t Format = 0;  // 16-bit indices only
};
struct Rect {
    std::int32_t left = 0;
    std::int32_t top = 0;
    std::int32_t right = 0;
    std::int32_t bottom = 0;
};

[[nodiscard]] inline VertexView vertex_view(Address address, std::uint32_t size, std::uint32_t stride) noexcept {
    return VertexView{address, size, stride};
}

[[nodiscard]] inline IndexView index_view(Address address, std::uint32_t size) noexcept {
    return IndexView{address, size, 16u};
}

[[nodiscard]] inline Rect rect(std::int32_t left, std::int32_t top, std::int32_t right, std::int32_t bottom) noexcept {
    return Rect{left, top, right, bottom};
}

// An owned texture and an owned buffer: the last reference releases it (Direct3D 11 keeps a released object until the
// GPU is done with it).
using TextureRef = std::shared_ptr<d3d11::Texture>;
using BufferRef = std::shared_ptr<d3d11::Buffer>;
[[nodiscard]] inline Resource* raw(const BufferRef& buffer) noexcept { return buffer.get(); }
using TextureObject = d3d11::Texture;
[[nodiscard]] inline TextureObject* raw(const TextureRef& texture) noexcept { return texture.get(); }
// Another reference to a texture a handle names (d3d11::Texture derives from enable_shared_from_this).
[[nodiscard]] TextureRef texture_ref(TextureObject* texture);
// A render target: its ID3D11RenderTargetView*, as the pointer's bits.
struct CpuDescriptor {
    std::uint64_t ptr = 0;
};

// An RGBA8 texture that is rendered to and sampled (display picture copies); empty when it cannot be created.
[[nodiscard]] TextureRef create_render_texture(Device* device, std::uint32_t width, std::uint32_t height);

}  // namespace galaxy::render::gpu
#else
#include "galaxy/windows_api.h"
#include <d3d12.h>
#include <wrl/client.h>

namespace galaxy::render::gpu {

using Device = ID3D12Device;
using DeviceRef = Microsoft::WRL::ComPtr<ID3D12Device>;
using TextureRef = Microsoft::WRL::ComPtr<ID3D12Resource>;
using BufferRef = Microsoft::WRL::ComPtr<ID3D12Resource>;
using TextureObject = ID3D12Resource;
[[nodiscard]] inline TextureObject* raw(const TextureRef& texture) noexcept { return texture.Get(); }
// Another reference to a texture a handle names.
[[nodiscard]] inline TextureRef texture_ref(TextureObject* texture) { return TextureRef{texture}; }
using CpuDescriptor = D3D12_CPU_DESCRIPTOR_HANDLE;
using Pipeline = ID3D12PipelineState;
using PipelineRef = Microsoft::WRL::ComPtr<ID3D12PipelineState>;
using Resource = ID3D12Resource;
// A GPU address a draw binds (constants, matrix palette, vertex and index data).
using Address = D3D12_GPU_VIRTUAL_ADDRESS;
// A shader-visible table of texture or sampler descriptors.
using Table = D3D12_GPU_DESCRIPTOR_HANDLE;
using VertexView = D3D12_VERTEX_BUFFER_VIEW;
using IndexView = D3D12_INDEX_BUFFER_VIEW;
using Rect = D3D12_RECT;


[[nodiscard]] inline VertexView vertex_view(Address address, std::uint32_t size, std::uint32_t stride) noexcept {
    VertexView view{};
    view.BufferLocation = address;
    view.SizeInBytes = size;
    view.StrideInBytes = stride;
    return view;
}

// 16-bit indices.
[[nodiscard]] inline IndexView index_view(Address address, std::uint32_t size) noexcept {
    IndexView view{};
    view.BufferLocation = address;
    view.SizeInBytes = size;
    view.Format = DXGI_FORMAT_R16_UINT;
    return view;
}

[[nodiscard]] inline Rect rect(std::int32_t left, std::int32_t top, std::int32_t right, std::int32_t bottom) noexcept {
    return Rect{static_cast<LONG>(left), static_cast<LONG>(top), static_cast<LONG>(right), static_cast<LONG>(bottom)};
}

// An RGBA8 texture that is rendered to and sampled (display picture copies); empty when it cannot be created.
[[nodiscard]] TextureRef create_render_texture(Device* device, std::uint32_t width, std::uint32_t height);

}  // namespace galaxy::render::gpu
#endif

namespace galaxy::render::gpu {

// Where upload memory lies: Host (system memory the GPU reads across the bus; the CPU writes it write-combined) or GpuLocal
// (video memory the CPU writes through a resizable BAR, for data the GPU reads every frame: the vertex and constant rings and the resident geometry pages),
// on a device that maps its video memory for the CPU in bulk (Direct3D 12: the GPU upload heap; Vulkan: device-local
// host-visible memory in a heap of its own larger than the 256 MB window); Host elsewhere.
enum class UploadPlace : std::uint8_t { Host, GpuLocal };

// A persistently mapped buffer the CPU writes and the GPU reads: the upload rings and RenderCore's resident vertices.
struct UploadMemory {
    BufferRef buffer;
    std::byte* cpu = nullptr;
    Address gpu = 0;
    bool local = false;  // in video memory (UploadPlace::GpuLocal was given)
    explicit operator bool() const noexcept { return cpu != nullptr; }
};

// `bytes` of upload memory, mapped for its lifetime; empty when it cannot be created.
[[nodiscard]] UploadMemory create_upload_memory(Device* device, std::size_t bytes, const char* name,
                                                UploadPlace place = UploadPlace::Host);
// The device reports itself lost, where the API can say so without a failed call.
[[nodiscard]] bool device_removed(Device* device) noexcept;

}  // namespace galaxy::render::gpu
