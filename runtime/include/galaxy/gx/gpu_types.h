// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The GPU API types GxBackend names, chosen when the backend is built (docs/VULKAN.md): D3D12 on Windows, Vulkan
// (NEBULA_GPU_VULKAN) elsewhere and in the Windows Vulkan test build. Compile-time, so draws make no virtual calls.

#include <cstddef>
#include <cstdint>
#include <memory>

#if defined(NEBULA_GPU_VULKAN)
#include <volk.h>

namespace galaxy::gx::gpu {

// Handles keep D3D12's field names where the shared code reads them, so one GxBackend compiles for both.
using Device = VkDevice_T;
using Pipeline = VkPipeline_T;
using Resource = VkBuffer_T;
// A 64-bit address in the renderer's own buffer address space: each buffer the GX path binds owns a range, and the
// renderer turns an address back into (buffer, offset) when it binds it. Arithmetic on it works as on D3D12's.
using Address = std::uint64_t;
struct Table {
    std::uint64_t ptr = 0;  // VkDescriptorSet
};
using Topology = VkPrimitiveTopology;
// D3D12's values: pipeline keys store them.
enum TopologyType : std::uint8_t {
    kTopologyTypePointValue = 1,
    kTopologyTypeLineValue = 2,
    kTopologyTypeTriangleValue = 3,
    kTopologyTypePatchValue = 4,
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

inline constexpr TopologyType kTopologyTypeTriangle = kTopologyTypeTriangleValue;
inline constexpr TopologyType kTopologyTypeLine = kTopologyTypeLineValue;
inline constexpr TopologyType kTopologyTypePoint = kTopologyTypePointValue;
inline constexpr TopologyType kTopologyTypePatch = kTopologyTypePatchValue;
inline constexpr Topology kTopologyTriangleList = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
inline constexpr Topology kTopologyLineList = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
inline constexpr Topology kTopologyPointList = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
// Curved surfaces' triangles as three-point patches (the pipeline's patch control points: 3).
inline constexpr Topology kTopologyTrianglePatches = VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;

[[nodiscard]] inline VertexView vertex_view(Address address, std::uint32_t size, std::uint32_t stride) noexcept {
    return VertexView{address, size, stride};
}

[[nodiscard]] inline IndexView index_view(Address address, std::uint32_t size) noexcept {
    return IndexView{address, size, 16u};
}

[[nodiscard]] inline Rect rect(std::int32_t left, std::int32_t top, std::int32_t right, std::int32_t bottom) noexcept {
    return Rect{left, top, right, bottom};
}

}  // namespace galaxy::gx::gpu

namespace galaxy::gx::vk {
struct Texture;  // vk/vk_context.h
struct Buffer;
}

namespace galaxy::gx::gpu {

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
// GX copies stay below 640x528 at scale 6; every Vulkan device reports at least 4096.
inline constexpr unsigned kMaxTexture2DDimension = 4096u;

// An RGBA8 texture that is rendered to and sampled (XFB copies); empty when it cannot be created.
[[nodiscard]] TextureRef create_render_texture(Device* device, std::uint32_t width, std::uint32_t height);

}  // namespace galaxy::gx::gpu
#else
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d12.h>
#include <wrl/client.h>

namespace galaxy::gx::gpu {

using Device = ID3D12Device;
using DeviceRef = Microsoft::WRL::ComPtr<ID3D12Device>;
using TextureRef = Microsoft::WRL::ComPtr<ID3D12Resource>;
using BufferRef = Microsoft::WRL::ComPtr<ID3D12Resource>;
using TextureObject = ID3D12Resource;
[[nodiscard]] inline TextureObject* raw(const TextureRef& texture) noexcept { return texture.Get(); }
// Another reference to a texture a handle names.
[[nodiscard]] inline TextureRef texture_ref(TextureObject* texture) { return TextureRef{texture}; }
using CpuDescriptor = D3D12_CPU_DESCRIPTOR_HANDLE;
inline constexpr unsigned kMaxTexture2DDimension = D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION;
using Pipeline = ID3D12PipelineState;
using Resource = ID3D12Resource;
// A GPU address a draw binds (constants, matrix palette, vertex and index data).
using Address = D3D12_GPU_VIRTUAL_ADDRESS;
// A shader-visible table of texture or sampler descriptors.
using Table = D3D12_GPU_DESCRIPTOR_HANDLE;
using Topology = D3D12_PRIMITIVE_TOPOLOGY;
using TopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE;
using VertexView = D3D12_VERTEX_BUFFER_VIEW;
using IndexView = D3D12_INDEX_BUFFER_VIEW;
using Rect = D3D12_RECT;

inline constexpr TopologyType kTopologyTypeTriangle = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
inline constexpr TopologyType kTopologyTypeLine = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
inline constexpr TopologyType kTopologyTypePoint = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
inline constexpr TopologyType kTopologyTypePatch = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
inline constexpr Topology kTopologyTriangleList = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
inline constexpr Topology kTopologyLineList = D3D_PRIMITIVE_TOPOLOGY_LINELIST;
inline constexpr Topology kTopologyPointList = D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
// Curved surfaces' triangles as three-point patches.
inline constexpr Topology kTopologyTrianglePatches = D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST;

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

// An RGBA8 texture that is rendered to and sampled (XFB copies); empty when it cannot be created.
[[nodiscard]] TextureRef create_render_texture(Device* device, std::uint32_t width, std::uint32_t height);

}  // namespace galaxy::gx::gpu
#endif

namespace galaxy::gx::gpu {

// A persistently mapped buffer the CPU writes and the GPU reads: the upload rings and GxBackend's resident vertices.
struct UploadMemory {
    BufferRef buffer;
    std::byte* cpu = nullptr;
    Address gpu = 0;
    explicit operator bool() const noexcept { return cpu != nullptr; }
};

// `bytes` of upload memory, mapped for its lifetime; empty when it cannot be created.
[[nodiscard]] UploadMemory create_upload_memory(Device* device, std::size_t bytes, const char* name);

}  // namespace galaxy::gx::gpu
