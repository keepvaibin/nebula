// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The GPU API types GxBackend names, chosen when the backend is built (docs/VULKAN.md): D3D12 on Windows, Vulkan
// (NEBULA_GPU_VULKAN) elsewhere and in the Windows Vulkan test build. Compile-time, so draws make no virtual calls.

#include <cstdint>

#if defined(NEBULA_GPU_VULKAN)
#error "the Vulkan backend's GPU types are not written yet (docs/VULKAN.md, V1)"
#else
#include <d3d12.h>

namespace galaxy::gx::gpu {

using Device = ID3D12Device;
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
inline constexpr Topology kTopologyTriangleList = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
inline constexpr Topology kTopologyLineList = D3D_PRIMITIVE_TOPOLOGY_LINELIST;
inline constexpr Topology kTopologyPointList = D3D_PRIMITIVE_TOPOLOGY_POINTLIST;

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

[[nodiscard]] inline bool is_null(Table table) noexcept {
    return table.ptr == 0;
}

}  // namespace galaxy::gx::gpu
#endif
