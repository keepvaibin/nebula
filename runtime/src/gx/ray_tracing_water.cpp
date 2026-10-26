// SPDX-License-Identifier: GPL-3.0-only
// SceneRayTracer's translucent water: the blended water-class draws noted after the trace get their own acceleration
// structure, and the reflections trace their surface from the picture the translucent phases left.
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
        !create_buffer(static_cast<std::uint64_t>(kWaterMaxTriangles) * kRtTriUvStride, D3D12_HEAP_TYPE_DEFAULT, kUav,
                       kUavFlag, water_uv_, L"rt water texture coordinates") ||
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
        water_uv_.Reset();
        water_blas_.Reset();
        water_tlas_.Reset();
        water_scratch_.Reset();
        water_instance_.Reset();
        return false;
    }
    water_positions_state_ = kUav;
    water_surfaces_state_ = kUav;
    water_uv_state_ = kUav;
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
                         water_positions_state_, water_surfaces_.Get(), water_surfaces_state_, water_uv_.Get(),
                         &water_uv_state_)) {
        water_incomplete_ = true;
    }
}

// Uploads the per-draw tables again for the water trace. The trace wrote them into the upload ring of its renderer
// frame; the water trace runs renderer frames later (a game frame spans several), when that space may hold newer
// uploads, and stale TEV records would send its texture fetches through invalid views and hang the GPU. Draws noted
// since the trace only append to the tables, so the trace's indices and counts still hold.
void SceneRayTracer::refresh_draw_tables(Allocate allocate, void* allocate_context) {
    if (draw_texture_table_ != 0u) {
        draw_texture_table_ = 0u;
        if (surface_uv_ && !draw_textures_.empty()) {
            const std::size_t bytes = draw_textures_.size() * sizeof(draw_textures_[0]);
            const RtUpload textures = allocate(allocate_context, bytes, 256u);
            if (textures.cpu != nullptr) {
                std::memcpy(textures.cpu, draw_textures_.data(), bytes);
                draw_texture_table_ = textures.gpu;
            }
        }
    }
    D3D12_GPU_VIRTUAL_ADDRESS draws = 0u;
    if (draw_table_ != 0u || engine_bind_.valid) {
        const std::size_t count = std::max<std::size_t>(draw_colors_.size(), 1u);
        const RtUpload table = allocate(allocate_context, count * sizeof(draw_colors_[0]), 256u);
        if (table.cpu != nullptr) {
            if (draw_colors_.empty()) {
                std::memset(table.cpu, 0, sizeof(draw_colors_[0]));
            } else {
                std::memcpy(table.cpu, draw_colors_.data(), draw_colors_.size() * sizeof(draw_colors_[0]));
            }
            draws = table.gpu;
        }
        draw_table_ = draw_colors_.empty() ? 0u : draws;
    }
    const bool physical_tev = reflect_physical_.tev_view != ~0u;
    RtUpload tev_upload{};
    std::uint64_t tev_bytes = 0u;
    const D3D12_GPU_VIRTUAL_ADDRESS tev =
        physical_tev || (engine_bind_.valid && engine_bind_.tev_records)
            ? upload_tev(tev_records_, tev_stages_, allocate, allocate_context, &tev_upload, &tev_bytes)
            : 0u;
    if (physical_tev) {
        reflect_physical_.tev_view =
            tev != 0u && tev_upload.resource != nullptr ? buffer_view(tev_upload.resource, tev_upload.offset, tev_bytes) : ~0u;
    }
    if (engine_bind_.valid) {
        engine_bind_.valid = draws != 0u && (!engine_bind_.tev_records || tev != 0u) &&
                             (!engine_bind_.textured || draw_texture_table_ != 0u);
        engine_bind_.draws = draws;
        engine_bind_.tev = engine_bind_.tev_records ? tev : draws;
        engine_bind_.draw_textures = engine_bind_.textured ? draw_texture_table_ : draws;
        engine_bind_.surface_uv =
            surface_uv_ ? surface_uv_->GetGPUVirtualAddress() : positions_->GetGPUVirtualAddress();
    }
}

int SceneRayTracer::trace_water(ID3D12GraphicsCommandList* list, const RtSources& sources, Allocate allocate,
                                void* allocate_context, D3D12_GPU_DESCRIPTOR_HANDLE color_depth_table,
                                std::uint32_t width, std::uint32_t height) {
    // The late draws (Rosalina) come before the water: its reflections show them.
    finish_late(list, sources, allocate, allocate_context);
    if (!water_open_) {
        return -1;
    }
    flush_water(list, sources, allocate, allocate_context);
    water_open_ = false;
    if (log_seq_) {
        std::fprintf(stderr, "[rt-seq] trace_water draws=%zu triangles=%u incomplete=%d\n", water_draws_.size(),
                     water_triangles_, water_incomplete_ ? 1 : 0);
    }
    // The water window's surfaces, else the scene's clear solids alone (the clear pass traces them; no water to trace).
    const bool water = water_triangles_ != 0u;
    if (list == nullptr || allocate == nullptr || water_incomplete_ || (!water && solid_draws_ == 0u) ||
        !reflections_active() || light_slot_ < 0 || !water_tlas_ || !tlas_ || !positions_ || !surfaces_ ||
        !light_primary_ || color_depth_table.ptr == 0u) {
        if (log_seq_) {
            std::fprintf(stderr, "[rt-seq] trace_water refused reflections=%d slot=%d water-tlas=%d tlas=%d table=%d\n",
                         reflections_active() ? 1 : 0, light_slot_, water_tlas_ ? 1 : 0, tlas_ ? 1 : 0,
                         color_depth_table.ptr != 0u ? 1 : 0);
        }
        water_light_triangles_ = 0u;
        return -1;
    }
    const auto slot = static_cast<unsigned>(light_slot_);
    const LightSlot& light = light_slots_[slot];
    if (light.width != width || light.height != height || !light_final_[slot]) {
        if (log_seq_) {
            std::fprintf(stderr, "[rt-seq] trace_water size light=%ux%u efb=%ux%u\n", light.width, light.height, width,
                         height);
        }
        return -1;
    }
    ComPtr<ID3D12GraphicsCommandList4> list4;
    if (FAILED(list->QueryInterface(IID_PPV_ARGS(&list4)))) {
        return -1;
    }
    if (late_built_ != 0u && !build_tlas(list4.Get(), allocate, allocate_context)) {
        return -1;
    }
    const RtUpload upload = allocate(allocate_context, RtReflections::kConstantsBytes, 256u);
    if (upload.cpu == nullptr) {
        return -1;
    }
    refresh_draw_tables(allocate, allocate_context);
    to_state(list, water_positions_.Get(), water_positions_state_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    to_state(list, water_surfaces_.Get(), water_surfaces_state_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    to_state(list, water_uv_.Get(), water_uv_state_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    // The water draws' waves (water_wave): their records in the frame's upload ring.
    RtUpload waves{};
    if (!water_waves_.empty()) {
        waves = allocate(allocate_context, water_waves_.size() * sizeof(water_waves_[0]), 256u);
        if (waves.cpu != nullptr) {
            std::memcpy(waves.cpu, water_waves_.data(), water_waves_.size() * sizeof(water_waves_[0]));
        }
    }
    if (water) {
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
    }
    // The next trace's light under water reads this water (engine_prepare).
    water_light_triangles_ = water_triangles_;
    // The opaque scene stays readable from its trace until the next scene's geometry.
    to_state(list, positions_.Get(), positions_state_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    to_state(list, surfaces_.Get(), surfaces_state_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    if (surface_uv_) {
        to_state(list, surface_uv_.Get(), surface_uv_state_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
    if (sky_radiance_) {
        to_state(list, sky_radiance_.Get(), sky_radiance_state_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
    RtReflectScene scene = reflect_scene(slot);
    // GALAXY_RT_LOG_CAMERA=1: logs the camera's world position and view direction (for test teleports into the water).
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
    // The engine's glass and crystal (rt_engine.hlsl EngineClearCS); the EFB depth is the table's second view.
    if (engine_clear_active()) {
        D3D12_GPU_DESCRIPTOR_HANDLE depth_table = color_depth_table;
        depth_table.ptr += texture_view_increment_;
        scene.engine_clear = trace_engine_clear(list, allocate, allocate_context, depth_table, slot);
    }
    scene.water_tlas = water_tlas_->GetGPUVirtualAddress();
    scene.water_positions = water_positions_->GetGPUVirtualAddress();
    scene.water_surfaces = water_surfaces_->GetGPUVirtualAddress();
    scene.water_uv = water_uv_->GetGPUVirtualAddress();
    scene.water_waves = waves.cpu != nullptr ? waves.gpu : 0u;
    scene.water_game_waves = waves.cpu != nullptr && water_game_waves_;
    water_traced_[slot] = water;
    if (water) {
        reflections_.trace_water(list, scene, slot, upload.cpu, upload.gpu, color_depth_table);
    }
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
    if (water_traced_[static_cast<unsigned>(slot)]) {
        reflections_.composite_water(list, target, depth_srv, width, height, static_cast<unsigned>(slot),
                                     reproject ? reproject_->GetGPUVirtualAddress() : 0u, split, scene_srv, opaque_srv);
    }
    composite_clear(list, target, depth_srv, width, height, static_cast<unsigned>(slot), reproject, scene_srv);
}

}  // namespace galaxy::gx
