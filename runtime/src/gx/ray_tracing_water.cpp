// SPDX-License-Identifier: GPL-3.0-only
// SceneRayTracer: translucent water. The scene's blended draws of the water class (seas, lakes,
// pools; noted from the trace to the end of the translucent phases) are transformed by the same
// geometry pass into their own buffers, become their own acceleration structure, and the
// reflections trace the water's surface from the picture the translucent phases left
// (RtReflections::trace_water).
#include "galaxy/gx/ray_tracing.h"

#include <cstring>
#include <iostream>

namespace galaxy::gx {
namespace {

using Microsoft::WRL::ComPtr;

D3D12_RESOURCE_BARRIER uav(ID3D12Resource* resource) {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = resource;
    return barrier;
}

}  // namespace

bool SceneRayTracer::ensure_water_buffers() {
    if (water_tlas_) {
        return true;
    }
    D3D12_RAYTRACING_GEOMETRY_DESC geometry{};
    geometry.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    geometry.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
    geometry.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
    geometry.Triangles.VertexCount = kWaterMaxTriangles * 3u;
    geometry.Triangles.VertexBuffer.StrideInBytes = 12u;
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS bottom{};
    bottom.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    bottom.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;
    bottom.NumDescs = 1u;
    bottom.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    bottom.pGeometryDescs = &geometry;
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO bottom_info{};
    device_->GetRaytracingAccelerationStructurePrebuildInfo(&bottom, &bottom_info);
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS top{};
    top.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    top.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    top.NumDescs = 1u;
    top.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO top_info{};
    device_->GetRaytracingAccelerationStructurePrebuildInfo(&top, &top_info);
    if (bottom_info.ResultDataMaxSizeInBytes == 0u || top_info.ResultDataMaxSizeInBytes == 0u) {
        return false;
    }
    const auto align = [](std::uint64_t value) {
        return (value + D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT - 1u) &
               ~std::uint64_t{D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT - 1u};
    };
    water_scratch_tlas_ = align(bottom_info.ScratchDataSizeInBytes);
    constexpr D3D12_RESOURCE_STATES kUav = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    constexpr D3D12_RESOURCE_FLAGS kUavFlag = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    constexpr D3D12_RESOURCE_STATES kStructure = D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
    if (!create_buffer(static_cast<std::uint64_t>(kWaterMaxTriangles) * 36u, D3D12_HEAP_TYPE_DEFAULT, kUav, kUavFlag,
                       water_positions_, L"rt water triangles") ||
        !create_buffer(static_cast<std::uint64_t>(kWaterMaxTriangles) * 16u, D3D12_HEAP_TYPE_DEFAULT, kUav, kUavFlag,
                       water_surfaces_, L"rt water surfaces") ||
        !create_buffer(bottom_info.ResultDataMaxSizeInBytes, D3D12_HEAP_TYPE_DEFAULT, kStructure, kUavFlag,
                       water_blas_, L"rt water blas") ||
        !create_buffer(top_info.ResultDataMaxSizeInBytes, D3D12_HEAP_TYPE_DEFAULT, kStructure, kUavFlag, water_tlas_,
                       L"rt water tlas") ||
        !create_buffer(water_scratch_tlas_ + align(top_info.ScratchDataSizeInBytes), D3D12_HEAP_TYPE_DEFAULT, kUav,
                       kUavFlag, water_scratch_, L"rt water scratch") ||
        !create_buffer(sizeof(D3D12_RAYTRACING_INSTANCE_DESC), D3D12_HEAP_TYPE_UPLOAD,
                       D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_FLAG_NONE, water_instance_,
                       L"rt water instance")) {
        water_positions_.Reset();
        water_surfaces_.Reset();
        water_blas_.Reset();
        water_tlas_.Reset();
        water_scratch_.Reset();
        water_instance_.Reset();
        return false;
    }
    water_positions_state_ = kUav;
    water_surfaces_state_ = kUav;
    D3D12_RAYTRACING_INSTANCE_DESC instance{};
    instance.Transform[0][0] = 1.0f;
    instance.Transform[1][1] = 1.0f;
    instance.Transform[2][2] = 1.0f;
    instance.InstanceMask = 0xFFu;
    instance.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE | D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_OPAQUE;
    instance.AccelerationStructure = water_blas_->GetGPUVirtualAddress();
    void* mapped = nullptr;
    const D3D12_RANGE no_read{0, 0};
    if (FAILED(water_instance_->Map(0, &no_read, &mapped))) {
        water_tlas_.Reset();
        return false;
    }
    std::memcpy(mapped, &instance, sizeof(instance));
    water_instance_->Unmap(0, nullptr);
    std::cerr << "[rt] water triangles=" << kWaterMaxTriangles << " blas-kb=" << (bottom_info.ResultDataMaxSizeInBytes >> 10u)
              << '\n';
    return true;
}

void SceneRayTracer::flush_water(ID3D12GraphicsCommandList* list, const RtSources& sources, Allocate allocate,
                                 void* allocate_context) {
    if (!water_open_ || list == nullptr || allocate == nullptr || water_flushed_draws_ >= water_draws_.size()) {
        return;
    }
    const std::size_t first = water_flushed_draws_;
    const std::uint32_t base = water_flushed_triangles_;
    water_flushed_draws_ = water_draws_.size();
    water_flushed_triangles_ = water_triangles_;
    if (!ensure_water_buffers() ||
        !transform_draws(list, sources, allocate, allocate_context, water_draws_.data() + first,
                         water_draws_.size() - first, base, water_triangles_ - base, water_positions_.Get(),
                         water_positions_state_, water_surfaces_.Get(), water_surfaces_state_)) {
        water_incomplete_ = true;
    }
}

int SceneRayTracer::trace_water(ID3D12GraphicsCommandList* list, const RtSources& sources, Allocate allocate,
                                void* allocate_context, D3D12_GPU_DESCRIPTOR_HANDLE color_depth_table,
                                std::uint32_t width, std::uint32_t height) {
    if (!water_open_) {
        return -1;
    }
    flush_water(list, sources, allocate, allocate_context);
    water_open_ = false;
    if (list == nullptr || allocate == nullptr || water_incomplete_ || water_triangles_ == 0u ||
        !reflections_active() || light_slot_ < 0 || !water_tlas_ || !tlas_ || !positions_ || !surfaces_ ||
        !light_primary_ || color_depth_table.ptr == 0u) {
        return -1;
    }
    const auto slot = static_cast<unsigned>(light_slot_);
    const LightSlot& light = light_slots_[slot];
    if (light.width != width || light.height != height || !light_final_[slot]) {
        return -1;
    }
    ComPtr<ID3D12GraphicsCommandList4> list4;
    if (FAILED(list->QueryInterface(IID_PPV_ARGS(&list4)))) {
        return -1;
    }
    const RtUpload upload = allocate(allocate_context, RtReflections::kConstantsBytes, 256u);
    if (upload.cpu == nullptr) {
        return -1;
    }
    to_state(list, water_positions_.Get(), water_positions_state_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    to_state(list, water_surfaces_.Get(), water_surfaces_state_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    D3D12_RAYTRACING_GEOMETRY_DESC geometry{};
    geometry.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    geometry.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
    geometry.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
    geometry.Triangles.VertexCount = water_triangles_ * 3u;
    geometry.Triangles.VertexBuffer.StartAddress = water_positions_->GetGPUVirtualAddress();
    geometry.Triangles.VertexBuffer.StrideInBytes = 12u;
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build{};
    build.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    build.Inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;
    build.Inputs.NumDescs = 1u;
    build.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    build.Inputs.pGeometryDescs = &geometry;
    build.DestAccelerationStructureData = water_blas_->GetGPUVirtualAddress();
    build.ScratchAccelerationStructureData = water_scratch_->GetGPUVirtualAddress();
    list4->BuildRaytracingAccelerationStructure(&build, 0u, nullptr);
    D3D12_RESOURCE_BARRIER barrier = uav(water_blas_.Get());
    list->ResourceBarrier(1u, &barrier);
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC top{};
    top.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    top.Inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    top.Inputs.NumDescs = 1u;
    top.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    top.Inputs.InstanceDescs = water_instance_->GetGPUVirtualAddress();
    top.DestAccelerationStructureData = water_tlas_->GetGPUVirtualAddress();
    top.ScratchAccelerationStructureData = water_scratch_->GetGPUVirtualAddress() + water_scratch_tlas_;
    list4->BuildRaytracingAccelerationStructure(&top, 0u, nullptr);
    barrier = uav(water_tlas_.Get());
    list->ResourceBarrier(1u, &barrier);
    // The opaque scene stays readable from its trace until the next scene's geometry.
    to_state(list, positions_.Get(), positions_state_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    to_state(list, surfaces_.Get(), surfaces_state_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    if (sky_radiance_) {
        to_state(list, sky_radiance_.Get(), sky_radiance_state_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
    RtReflectScene scene = reflect_scene(slot);
    // GALAXY_RT_LOG_CAMERA=1: the camera's world position and view direction (positions for test
    // teleports into the water).
    static const bool log_camera = [] {
        char value[8]{};
        std::size_t length = 0u;
        return getenv_s(&length, value, sizeof(value), "GALAXY_RT_LOG_CAMERA") == 0 && length > 1u && value[0] == '1';
    }();
    static std::uint64_t logged = 0u;
    if (log_camera && light.camera_known && ++logged % 30u == 0u) {
        const std::array<float, 12>& m = light.camera;
        // Rows of world to eye: the eye is -R^T t, it looks along -z (the third row).
        const float eye[3] = {-(m[0] * m[3] + m[4] * m[7] + m[8] * m[11]), -(m[1] * m[3] + m[5] * m[7] + m[9] * m[11]),
                              -(m[2] * m[3] + m[6] * m[7] + m[10] * m[11])};
        std::cerr << "[rt-camera] water=" << logged << " eye=" << eye[0] << ',' << eye[1] << ',' << eye[2]
                  << " forward=" << -m[8] << ',' << -m[9] << ',' << -m[10] << '\n';
    }
    scene.water_tlas = water_tlas_->GetGPUVirtualAddress();
    scene.water_positions = water_positions_->GetGPUVirtualAddress();
    scene.water_surfaces = water_surfaces_->GetGPUVirtualAddress();
    reflections_.trace_water(list, scene, slot, upload.cpu, upload.gpu, color_depth_table);
    return static_cast<int>(slot);
}

void SceneRayTracer::composite_water(ID3D12GraphicsCommandList* list, D3D12_CPU_DESCRIPTOR_HANDLE target,
                                     D3D12_GPU_DESCRIPTOR_HANDLE depth_srv, std::uint32_t width,
                                     std::uint32_t height, int slot, bool reproject,
                                     D3D12_GPU_DESCRIPTOR_HANDLE scene_srv, D3D12_GPU_DESCRIPTOR_HANDLE opaque_srv) {
    if (list == nullptr || slot < 0 || slot >= static_cast<int>(kLightSlots) || depth_srv.ptr == 0u ||
        show_lighting_) {
        return;
    }
    const LightSlot& info = light_slots_[static_cast<unsigned>(slot)];
    reproject = reproject && reproject_;
    const float split = split_ > 0.0f ? info.view.viewport[0] + split_ * info.view.viewport[2] : -1.0f;
    reflections_.composite_water(list, target, depth_srv, width, height, static_cast<unsigned>(slot),
                                 reproject ? reproject_->GetGPUVirtualAddress() : 0u, split, scene_srv, opaque_srv);
}

}  // namespace galaxy::gx
