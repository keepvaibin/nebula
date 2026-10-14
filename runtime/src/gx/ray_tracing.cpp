// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/gx/ray_tracing.h"

#include "rt_geometry_cs.h"
#include "rt_validate_cs.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>

namespace galaxy::gx {
namespace {

using Microsoft::WRL::ComPtr;

// Mirrors cbuffer RtFrame in ray_tracing.hlsl.
struct RtFrameConstants {
    std::uint32_t draw_count;
    std::uint32_t triangle_count;
    std::uint32_t vertex_ring_limit;
    std::uint32_t resident_limit;
    std::uint32_t index_ring_limit;
    std::uint32_t constant_ring_limit;
    std::uint32_t matrix_ring_limit;
    std::uint32_t frame_flags;
    float projection[4];
    float depth[4];
    float viewport[4];
    std::uint32_t target_size[2];
    float near_plane;
    float pad0;
};
static_assert(sizeof(RtFrameConstants) == 96u);

enum RootParameter : UINT {
    kRootFrame = 0,
    kRootDraws,
    kRootVertexRing,
    kRootResident,
    kRootIndexRing,
    kRootConstantRing,
    kRootMatrixRing,
    kRootPositions,
    kRootScene,
    kRootDepth,
    kRootStats,
    kRootDump,
    kRootCount,
};

constexpr std::uint32_t kVsConstantsBytes = 1280u;
constexpr std::uint32_t kPaletteMatrixBytes = 48u;
constexpr UINT kTimestampsPerSlot = 3u;
constexpr UINT64 kReadbackSlotBytes = 128u;

// Highest readable byte offset for a `size`-byte load (0 for an empty or too small buffer).
std::uint32_t read_limit(std::uint64_t bytes, std::uint32_t size) {
    return bytes > size ? static_cast<std::uint32_t>(std::min<std::uint64_t>(bytes - size, 0xFFFFFFF0u)) : 0u;
}

D3D12_RESOURCE_BARRIER transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES before,
                                  D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    return barrier;
}

D3D12_RESOURCE_BARRIER uav_barrier(ID3D12Resource* resource) {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = resource;
    return barrier;
}

}  // namespace

bool SceneRayTracer::initialize(ID3D12Device* device, std::uint64_t timestamp_frequency, bool validate) {
    shutdown();
    if (device == nullptr || FAILED(device->QueryInterface(IID_PPV_ARGS(&device_)))) {
        device_.Reset();
        return false;
    }
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5{};
    D3D12_FEATURE_DATA_SHADER_MODEL shader_model{D3D_SHADER_MODEL_6_5};
    if (FAILED(device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5))) ||
        options5.RaytracingTier < D3D12_RAYTRACING_TIER_1_1 ||
        FAILED(device_->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shader_model, sizeof(shader_model))) ||
        shader_model.HighestShaderModel < D3D_SHADER_MODEL_6_5) {
        std::cerr << "[rt] unavailable: DXR 1.1 with shader model 6.5 required (tier "
                  << static_cast<int>(options5.RaytracingTier) << ")\n";
        device_.Reset();
        return false;
    }

    D3D12_DESCRIPTOR_RANGE depth_range{};
    depth_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    depth_range.NumDescriptors = 1u;
    depth_range.BaseShaderRegister = 7u;
    depth_range.OffsetInDescriptorsFromTableStart = 0u;
    D3D12_ROOT_PARAMETER params[kRootCount]{};
    params[kRootFrame].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[kRootFrame].Descriptor.ShaderRegister = 0u;
    for (UINT i = kRootDraws; i <= kRootMatrixRing; ++i) {
        params[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[i].Descriptor.ShaderRegister = i - kRootDraws;
    }
    params[kRootPositions].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    params[kRootPositions].Descriptor.ShaderRegister = 0u;
    params[kRootScene].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[kRootScene].Descriptor.ShaderRegister = 6u;
    params[kRootDepth].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[kRootDepth].DescriptorTable.NumDescriptorRanges = 1u;
    params[kRootDepth].DescriptorTable.pDescriptorRanges = &depth_range;
    params[kRootStats].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    params[kRootStats].Descriptor.ShaderRegister = 1u;
    params[kRootDump].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    params[kRootDump].Descriptor.ShaderRegister = 2u;
    for (auto& param : params) {
        param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
    D3D12_ROOT_SIGNATURE_DESC rsd{};
    rsd.NumParameters = kRootCount;
    rsd.pParameters = params;
    ComPtr<ID3DBlob> blob, error;
    if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error)) ||
        FAILED(device_->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                            IID_PPV_ARGS(&root_)))) {
        std::cerr << "[rt] root signature failed\n";
        shutdown();
        return false;
    }
    D3D12_COMPUTE_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature = root_.Get();
    pso.CS = {g_rt_geometry_cs, sizeof(g_rt_geometry_cs)};
    if (FAILED(device_->CreateComputePipelineState(&pso, IID_PPV_ARGS(&geometry_pso_)))) {
        std::cerr << "[rt] geometry pipeline failed\n";
        shutdown();
        return false;
    }
    pso.CS = {g_rt_validate_cs, sizeof(g_rt_validate_cs)};
    if (FAILED(device_->CreateComputePipelineState(&pso, IID_PPV_ARGS(&validate_pso_)))) {
        std::cerr << "[rt] validation pipeline failed\n";
        shutdown();
        return false;
    }

    if (!create_buffer(kStatsWords * 4u, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST,
                       D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, stats_, L"rt stats") ||
        !create_buffer(kStatsWords * 4u, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ,
                       D3D12_RESOURCE_FLAG_NONE, stats_zero_, L"rt stats zero") ||
        !create_buffer(kResultSlots * kReadbackSlotBytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST,
                       D3D12_RESOURCE_FLAG_NONE, readback_, L"rt readback") ||
        !create_buffer(sizeof(D3D12_RAYTRACING_INSTANCE_DESC), D3D12_HEAP_TYPE_UPLOAD,
                       D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_FLAG_NONE, instance_, L"rt instance")) {
        shutdown();
        return false;
    }
    void* zero = nullptr;
    const D3D12_RANGE no_read{0, 0};
    if (FAILED(stats_zero_->Map(0, &no_read, &zero))) {
        shutdown();
        return false;
    }
    std::memset(zero, 0, kStatsWords * 4u);
    stats_zero_->Unmap(0, nullptr);
    D3D12_QUERY_HEAP_DESC query{};
    query.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    query.Count = kResultSlots * kTimestampsPerSlot;
    if (timestamp_frequency != 0u && FAILED(device_->CreateQueryHeap(&query, IID_PPV_ARGS(&timestamps_)))) {
        timestamps_.Reset();
    }
    timestamp_frequency_ = timestamp_frequency;
    validate_ = validate;
    {
        char value[32]{};
        std::size_t length = 0u;
        dump_scene_ = getenv_s(&length, value, sizeof(value), "GALAXY_RT_DUMP") == 0 && length > 1u
            ? std::strtoull(value, nullptr, 10) : 0u;
    }
    ready_ = true;
    std::cerr << "[rt] ready tier=1.1 validate=" << (validate ? 1 : 0) << '\n';
    return true;
}

void SceneRayTracer::shutdown() noexcept {
    ready_ = false;
    scene_open_ = false;
    draws_.clear();
    retired_.clear();
    slots_ = {};
    window_ = {};
    positions_.Reset();
    blas_.Reset();
    tlas_.Reset();
    scratch_.Reset();
    instance_.Reset();
    stats_.Reset();
    stats_zero_.Reset();
    readback_.Reset();
    timestamps_.Reset();
    dump_.Reset();
    dump_readback_.Reset();
    dump_pending_ = false;
    geometry_pso_.Reset();
    validate_pso_.Reset();
    root_.Reset();
    device_.Reset();
    capacity_ = 0u;
    triangles_ = 0u;
    positions_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
}

bool SceneRayTracer::create_buffer(std::uint64_t bytes, D3D12_HEAP_TYPE heap, D3D12_RESOURCE_STATES state,
                                   D3D12_RESOURCE_FLAGS flags, ComPtr<ID3D12Resource>& out, const wchar_t* name) {
    D3D12_HEAP_PROPERTIES properties{};
    properties.Type = heap;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = std::max<std::uint64_t>(bytes, 256u);
    desc.Height = 1u;
    desc.DepthOrArraySize = 1u;
    desc.MipLevels = 1u;
    desc.SampleDesc.Count = 1u;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = flags;
    if (FAILED(device_->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr,
                                                IID_PPV_ARGS(&out)))) {
        std::cerr << "[rt] buffer allocation failed bytes=" << bytes << '\n';
        out.Reset();
        return false;
    }
    out->SetName(name);
    return true;
}

void SceneRayTracer::retire(ComPtr<ID3D12Resource>& resource) {
    if (resource) {
        retired_.push_back({std::move(resource), frame_index_});
    }
}

bool SceneRayTracer::ensure_capacity(std::uint32_t triangles) {
    if (triangles <= capacity_ && blas_ && tlas_) {
        return true;
    }
    if (triangles > kMaxTriangles) {
        return false;
    }
    std::uint32_t capacity = std::max<std::uint32_t>(capacity_, 128u * 1024u);
    while (capacity < triangles) {
        capacity *= 2u;
    }
    capacity = std::min(capacity, kMaxTriangles);

    D3D12_RAYTRACING_GEOMETRY_DESC geometry{};
    geometry.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    geometry.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
    geometry.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
    geometry.Triangles.VertexCount = capacity * 3u;
    geometry.Triangles.VertexBuffer.StrideInBytes = 12u;
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS blas_inputs{};
    blas_inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    blas_inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;
    blas_inputs.NumDescs = 1u;
    blas_inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    blas_inputs.pGeometryDescs = &geometry;
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO blas_info{};
    device_->GetRaytracingAccelerationStructurePrebuildInfo(&blas_inputs, &blas_info);
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS tlas_inputs{};
    tlas_inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    tlas_inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    tlas_inputs.NumDescs = 1u;
    tlas_inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO tlas_info{};
    device_->GetRaytracingAccelerationStructurePrebuildInfo(&tlas_inputs, &tlas_info);
    if (blas_info.ResultDataMaxSizeInBytes == 0u || tlas_info.ResultDataMaxSizeInBytes == 0u) {
        return false;
    }
    const auto align = [](std::uint64_t value) {
        return (value + D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT - 1u) &
               ~std::uint64_t{D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT - 1u};
    };
    const std::uint64_t scratch_tlas_offset = align(blas_info.ScratchDataSizeInBytes);
    const std::uint64_t scratch_bytes = scratch_tlas_offset + align(tlas_info.ScratchDataSizeInBytes);

    retire(positions_);
    retire(blas_);
    retire(tlas_);
    retire(scratch_);
    capacity_ = 0u;
    if (!create_buffer(static_cast<std::uint64_t>(capacity) * 36u, D3D12_HEAP_TYPE_DEFAULT,
                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, positions_,
                       L"rt positions") ||
        !create_buffer(blas_info.ResultDataMaxSizeInBytes, D3D12_HEAP_TYPE_DEFAULT,
                       D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
                       D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, blas_, L"rt blas") ||
        !create_buffer(tlas_info.ResultDataMaxSizeInBytes, D3D12_HEAP_TYPE_DEFAULT,
                       D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
                       D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, tlas_, L"rt tlas") ||
        !create_buffer(scratch_bytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                       D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, scratch_, L"rt scratch")) {
        positions_.Reset();
        blas_.Reset();
        tlas_.Reset();
        scratch_.Reset();
        return false;
    }
    positions_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    blas_bytes_ = blas_info.ResultDataMaxSizeInBytes;
    tlas_bytes_ = tlas_info.ResultDataMaxSizeInBytes;
    scratch_bytes_ = scratch_tlas_offset;

    // One instance: the frame's eye-space geometry, untransformed. Written while no frame
    // references the new BLAS; the previous instance buffer retires with the old BLAS.
    retire(instance_);
    if (!create_buffer(sizeof(D3D12_RAYTRACING_INSTANCE_DESC), D3D12_HEAP_TYPE_UPLOAD,
                       D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_FLAG_NONE, instance_, L"rt instance")) {
        return false;
    }
    D3D12_RAYTRACING_INSTANCE_DESC instance{};
    instance.Transform[0][0] = 1.0f;
    instance.Transform[1][1] = 1.0f;
    instance.Transform[2][2] = 1.0f;
    instance.InstanceMask = 0xFFu;
    instance.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE |
                     D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_OPAQUE;
    instance.AccelerationStructure = blas_->GetGPUVirtualAddress();
    void* mapped = nullptr;
    const D3D12_RANGE no_read{0, 0};
    if (FAILED(instance_->Map(0, &no_read, &mapped))) {
        return false;
    }
    std::memcpy(mapped, &instance, sizeof(instance));
    instance_->Unmap(0, nullptr);
    capacity_ = capacity;
    std::cerr << "[rt] capacity triangles=" << capacity << " blas-mb=" << (blas_bytes_ >> 20u)
              << " scratch-mb=" << (scratch_bytes >> 20u) << " positions-mb="
              << ((static_cast<std::uint64_t>(capacity) * 36u) >> 20u) << '\n';
    return true;
}

void SceneRayTracer::begin_frame(std::uint64_t frame_index) {
    frame_index_ = frame_index;
    if (!ready_) {
        return;
    }
    std::erase_if(retired_, [&](const Retired& entry) { return entry.frame + kResultDelay <= frame_index; });
    for (unsigned slot = 0; slot < kResultSlots; ++slot) {
        ResultSlot& result = slots_[slot];
        if (!result.pending || result.frame + kResultDelay > frame_index) {
            continue;
        }
        result.pending = false;
        const D3D12_RANGE range{slot * kReadbackSlotBytes, (slot + 1u) * kReadbackSlotBytes};
        void* mapped = nullptr;
        if (FAILED(readback_->Map(0, &range, &mapped))) {
            continue;
        }
        const auto* base = static_cast<const std::byte*>(mapped) + range.Begin;
        if (result.validated) {
            std::uint32_t stats[kStatsWords]{};
            std::memcpy(stats, base, sizeof(stats));
            for (unsigned i = 0; i < kStatsWords; ++i) {
                window_.stats[i] += stats[i];
            }
            ++window_.validated;
        }
        if (timestamps_ && timestamp_frequency_ != 0u) {
            std::uint64_t ticks[kTimestampsPerSlot]{};
            std::memcpy(ticks, base + 64u, sizeof(ticks));
            const double to_ms = 1000.0 / static_cast<double>(timestamp_frequency_);
            if (ticks[1] >= ticks[0] && ticks[1] - ticks[0] < timestamp_frequency_) {
                window_.build_ms += static_cast<double>(ticks[1] - ticks[0]) * to_ms;
                ++window_.timed;
                if (result.validated && ticks[2] >= ticks[1]) {
                    window_.validate_ms += static_cast<double>(ticks[2] - ticks[1]) * to_ms;
                }
            }
        }
        const D3D12_RANGE no_write{0, 0};
        readback_->Unmap(0, &no_write);
    }
    if (dump_pending_ && dump_frame_ + kResultDelay <= frame_index) {
        dump_pending_ = false;
        write_dump();
    }
    if (window_.scenes >= 240u) {
        report();
    }
}

void SceneRayTracer::write_dump() {
    const std::uint64_t pixels = static_cast<std::uint64_t>(dump_width_) * dump_height_;
    const D3D12_RANGE range{0, static_cast<SIZE_T>(pixels * 16u)};
    void* mapped = nullptr;
    if (!dump_readback_ || FAILED(dump_readback_->Map(0, &range, &mapped))) {
        return;
    }
    const auto* texels = static_cast<const float*>(mapped);
    std::string directory = ".";
    {
        char value[MAX_PATH]{};
        std::size_t length = 0u;
        if (getenv_s(&length, value, sizeof(value), "GALAXY_RT_DUMP_DIR") == 0 && length > 1u) {
            directory = value;
        }
    }
    static constexpr std::uint8_t kColors[7][3] = {
        {0, 0, 0}, {0, 255, 255}, {0, 200, 0}, {255, 255, 0}, {255, 0, 0}, {0, 0, 255}, {255, 0, 255}};
    const auto shade = [](float z) {
        // log10 of the eye distance over 1 .. 1e6.
        const float d = std::max(-z, 1.0f);
        return static_cast<std::uint8_t>(std::clamp(std::log10(d) / 6.0f, 0.0f, 1.0f) * 255.0f);
    };
    const char* names[3] = {"class", "raster", "traced"};
    for (unsigned image = 0; image < 3u; ++image) {
        const std::string path = directory + "\\rt_" + names[image] + '_' + std::to_string(dump_scene_) + ".ppm";
        std::ofstream file(path, std::ios::binary);
        file << "P6\n" << dump_width_ << ' ' << dump_height_ << "\n255\n";
        std::vector<std::uint8_t> row(static_cast<std::size_t>(dump_width_) * 3u);
        for (std::uint32_t y = 0; y < dump_height_; ++y) {
            for (std::uint32_t x = 0; x < dump_width_; ++x) {
                const float* texel = texels + (static_cast<std::size_t>(y) * dump_width_ + x) * 4u;
                std::uint8_t* out = row.data() + x * 3u;
                if (image == 0u) {
                    const unsigned kind = std::min(static_cast<unsigned>(texel[0]), 6u);
                    std::memcpy(out, kColors[kind], 3u);
                } else {
                    const bool background = texel[0] < 1.5f;
                    const std::uint8_t v = image == 1u ? (background ? 0u : shade(texel[1]))
                                                       : (texel[2] != 0.0f ? shade(texel[2]) : 0u);
                    out[0] = out[1] = out[2] = v;
                }
            }
            file.write(reinterpret_cast<const char*>(row.data()), static_cast<std::streamsize>(row.size()));
        }
    }
    // Pixels per class for every draw (by the traced triangle), then the draws in table order.
    std::vector<std::array<std::uint32_t, 8>> per_draw(dump_draws_.size());
    for (std::uint64_t i = 0; i < pixels; ++i) {
        const float* texel = texels + i * 4u;
        std::uint32_t triangle = 0u;
        std::memcpy(&triangle, texel + 3, 4u);
        if (triangle == 0xFFFFFFFFu || dump_draws_.empty()) {
            continue;
        }
        const auto it = std::upper_bound(dump_draws_.begin(), dump_draws_.end(), triangle,
                                         [](std::uint32_t t, const RtDraw& d) { return t < d.first_triangle; });
        const std::size_t index = static_cast<std::size_t>(std::distance(dump_draws_.begin(), it)) - 1u;
        if (index < per_draw.size()) {
            ++per_draw[index][std::min(static_cast<unsigned>(texel[0]), 7u)];
        }
    }
    std::ofstream report(directory + "\\rt_draws_" + std::to_string(dump_scene_) + ".txt");
    report << "draw first triangles flags vertices proj(A B C D E F) depth(min max) cull phase | "
              "bg bg-hit match0.1 match1 nearer farther miss outside\n";
    for (std::size_t d = 0; d < dump_draws_.size(); ++d) {
        const RtDraw& draw = dump_draws_[d];
        report << d << ' ' << draw.first_triangle << ' ' << draw.triangle_count << ' ' << draw.flags << ' '
               << draw.vertex_count;
        if (d < dump_infos_.size()) {
            const RtDrawInfo& info = dump_infos_[d];
            for (float p : info.projection) report << ' ' << p;
            report << ' ' << info.min_depth << ' ' << info.max_depth << ' ' << info.cull << ' ' << info.phase;
        }
        report << " |";
        for (std::uint32_t n : per_draw[d]) report << ' ' << n;
        report << '\n';
    }
    const D3D12_RANGE no_write{0, 0};
    dump_readback_->Unmap(0, &no_write);
    std::cerr << "[rt] dump scene=" << dump_scene_ << " " << dump_width_ << 'x' << dump_height_ << " dir=" << directory
              << '\n';
}

void SceneRayTracer::report() {
    const auto per = [](double value, std::uint64_t n) { return n != 0u ? value / static_cast<double>(n) : 0.0; };
    const auto pct = [&](unsigned word, unsigned of) {
        return window_.stats[of] != 0u
            ? 100.0 * static_cast<double>(window_.stats[word]) / static_cast<double>(window_.stats[of])
            : 0.0;
    };
    char line[768];
    std::snprintf(line, sizeof(line),
                  "[rt-stats] scenes=%llu skipped=%llu draws-per-scene=%.1f triangles-per-scene=%.0f "
                  "max-triangles=%llu capacity=%u build-ms=%.3f validate-ms=%.3f validated=%llu "
                  "geometry-pixels=%.2f%% match-0.1%%=%.3f%% match-1%%=%.3f%% hit-nearer=%.3f%% hit-farther=%.3f%% "
                  "miss=%.3f%% background-hit=%.3f%%\n",
                  static_cast<unsigned long long>(window_.scenes), static_cast<unsigned long long>(window_.skipped),
                  per(static_cast<double>(window_.draws), window_.scenes),
                  per(static_cast<double>(window_.triangles), window_.scenes),
                  static_cast<unsigned long long>(window_.max_triangles), capacity_,
                  per(window_.build_ms, window_.timed), per(window_.validate_ms, window_.validated),
                  static_cast<unsigned long long>(window_.validated), pct(2u, 0u), pct(3u, 2u), pct(4u, 2u),
                  pct(5u, 2u), pct(6u, 2u), pct(7u, 2u), pct(8u, 1u));
    std::cerr << line;
    window_ = {};
}

void SceneRayTracer::begin_scene() noexcept {
    draws_.clear();
    infos_.clear();
    triangles_ = 0u;
    view_known_ = false;
    scene_open_ = ready_;
}

void SceneRayTracer::add_draw(const RtDraw& draw, const RtDrawInfo& info) {
    if (!scene_open_ || draw.triangle_count == 0u || triangles_ + draw.triangle_count > kMaxTriangles) {
        return;
    }
    RtDraw& entry = draws_.emplace_back(draw);
    entry.first_triangle = triangles_;
    triangles_ += draw.triangle_count;
    if (dump_scene_ != 0u && scene_serial_ == dump_scene_) {
        infos_.push_back(info);
    }
}

void SceneRayTracer::set_view(const RtView& view) noexcept {
    view_ = view;
    view_known_ = true;
}

D3D12_GPU_VIRTUAL_ADDRESS SceneRayTracer::scene_tlas() const noexcept {
    return tlas_ ? tlas_->GetGPUVirtualAddress() : 0u;
}

bool SceneRayTracer::trace_scene(ID3D12GraphicsCommandList* list, const RtSources& sources, Allocate allocate,
                                 void* allocate_context, D3D12_GPU_DESCRIPTOR_HANDLE depth_srv,
                                 std::uint32_t width, std::uint32_t height) {
    if (!ready_ || !scene_open_) {
        return false;
    }
    scene_open_ = false;
    if (draws_.empty() || triangles_ == 0u || list == nullptr || allocate == nullptr) {
        return false;
    }
    ++window_.scenes;
    window_.draws += draws_.size();
    window_.triangles += triangles_;
    window_.max_triangles = std::max<std::uint64_t>(window_.max_triangles, triangles_);
    ComPtr<ID3D12GraphicsCommandList4> list4;
    if (FAILED(list->QueryInterface(IID_PPV_ARGS(&list4))) || !ensure_capacity(triangles_)) {
        ++window_.skipped;
        return false;
    }
    const std::size_t table_bytes = draws_.size() * sizeof(RtDraw);
    const RtUpload table = allocate(allocate_context, table_bytes, 256u);
    const RtUpload frame = allocate(allocate_context, sizeof(RtFrameConstants), 256u);
    if (table.cpu == nullptr || frame.cpu == nullptr) {
        ++window_.skipped;
        return false;
    }
    std::memcpy(table.cpu, draws_.data(), table_bytes);
    RtFrameConstants constants{};
    constants.draw_count = static_cast<std::uint32_t>(draws_.size());
    constants.triangle_count = triangles_;
    constants.vertex_ring_limit = read_limit(sources.vertex_ring.bytes, kRtVertexStride);
    constants.resident_limit = read_limit(sources.resident.bytes, kRtVertexStride);
    constants.index_ring_limit = read_limit(sources.index_ring.bytes, 4u);
    constants.constant_ring_limit = read_limit(sources.constant_ring.bytes, kVsConstantsBytes);
    constants.matrix_ring_limit = read_limit(sources.matrix_ring.bytes, kPaletteMatrixBytes);
    std::copy(view_.projection.begin(), view_.projection.end(), constants.projection);
    constants.depth[0] = view_.z_scale;
    constants.depth[1] = view_.z_offset;
    constants.depth[2] = view_.min_depth;
    constants.depth[3] = view_.max_depth;
    std::copy(view_.viewport.begin(), view_.viewport.end(), constants.viewport);
    const std::uint64_t serial = scene_serial_++;
    const unsigned slot = static_cast<unsigned>(serial % kResultSlots);
    const bool record_results = !slots_[slot].pending;
    const bool validated = validate_ && view_known_ && depth_srv.ptr != 0u && width != 0u && height != 0u &&
                           record_results;
    bool dumping = validated && serial == dump_scene_ && dump_scene_ != 0u && !dump_pending_;
    if (dumping) {
        const std::uint64_t bytes = static_cast<std::uint64_t>(width) * height * 16u;
        dump_.Reset();
        dump_readback_.Reset();
        dumping = create_buffer(bytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, dump_, L"rt dump") &&
                  create_buffer(bytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST,
                                D3D12_RESOURCE_FLAG_NONE, dump_readback_, L"rt dump readback");
    }
    constants.frame_flags = dumping ? 1u : 0u;
    constants.target_size[0] = width;
    constants.target_size[1] = height;
    // Near plane: depth 1 (reversed Z) is ndc 1, z = -[2][3] / (1 + [2][2]).
    const float near_denominator = 1.0f + view_.z_scale;
    constants.near_plane = near_denominator != 0.0f ? std::max(0.0f, view_.z_offset / near_denominator) : 0.0f;
    std::memcpy(frame.cpu, &constants, sizeof(constants));

    if (record_results && timestamps_) {
        list->EndQuery(timestamps_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, slot * kTimestampsPerSlot);
    }

    if (positions_state_ != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        const D3D12_RESOURCE_BARRIER barrier =
            transition(positions_.Get(), positions_state_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        list->ResourceBarrier(1u, &barrier);
        positions_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }
    const D3D12_GPU_VIRTUAL_ADDRESS resident = sources.resident.base != 0u ? sources.resident.base
                                                                           : sources.vertex_ring.base;
    list->SetComputeRootSignature(root_.Get());
    list->SetPipelineState(geometry_pso_.Get());
    list->SetComputeRootConstantBufferView(kRootFrame, frame.gpu);
    list->SetComputeRootShaderResourceView(kRootDraws, table.gpu);
    list->SetComputeRootShaderResourceView(kRootVertexRing, sources.vertex_ring.base);
    list->SetComputeRootShaderResourceView(kRootResident, resident);
    list->SetComputeRootShaderResourceView(kRootIndexRing, sources.index_ring.base);
    list->SetComputeRootShaderResourceView(kRootConstantRing, sources.constant_ring.base);
    list->SetComputeRootShaderResourceView(kRootMatrixRing, sources.matrix_ring.base);
    list->SetComputeRootUnorderedAccessView(kRootPositions, positions_->GetGPUVirtualAddress());
    list->Dispatch((triangles_ + 63u) / 64u, 1u, 1u);
    {
        const D3D12_RESOURCE_BARRIER barrier = transition(
            positions_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        list->ResourceBarrier(1u, &barrier);
        positions_state_ = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    }

    D3D12_RAYTRACING_GEOMETRY_DESC geometry{};
    geometry.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    geometry.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
    geometry.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
    geometry.Triangles.VertexCount = triangles_ * 3u;
    geometry.Triangles.VertexBuffer.StartAddress = positions_->GetGPUVirtualAddress();
    geometry.Triangles.VertexBuffer.StrideInBytes = 12u;
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build{};
    build.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    build.Inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;
    build.Inputs.NumDescs = 1u;
    build.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    build.Inputs.pGeometryDescs = &geometry;
    build.DestAccelerationStructureData = blas_->GetGPUVirtualAddress();
    build.ScratchAccelerationStructureData = scratch_->GetGPUVirtualAddress();
    list4->BuildRaytracingAccelerationStructure(&build, 0u, nullptr);
    {
        const D3D12_RESOURCE_BARRIER barrier = uav_barrier(blas_.Get());
        list->ResourceBarrier(1u, &barrier);
    }
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC top{};
    top.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    top.Inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    top.Inputs.NumDescs = 1u;
    top.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    top.Inputs.InstanceDescs = instance_->GetGPUVirtualAddress();
    top.DestAccelerationStructureData = tlas_->GetGPUVirtualAddress();
    top.ScratchAccelerationStructureData = scratch_->GetGPUVirtualAddress() + scratch_bytes_;
    list4->BuildRaytracingAccelerationStructure(&top, 0u, nullptr);
    {
        const D3D12_RESOURCE_BARRIER barrier = uav_barrier(tlas_.Get());
        list->ResourceBarrier(1u, &barrier);
    }
    if (record_results && timestamps_) {
        list->EndQuery(timestamps_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, slot * kTimestampsPerSlot + 1u);
    }

    if (validated) {
        list->CopyBufferRegion(stats_.Get(), 0u, stats_zero_.Get(), 0u, kStatsWords * 4u);
        D3D12_RESOURCE_BARRIER barrier = transition(stats_.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                                                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        list->ResourceBarrier(1u, &barrier);
        list->SetPipelineState(validate_pso_.Get());
        list->SetComputeRootShaderResourceView(kRootScene, tlas_->GetGPUVirtualAddress());
        list->SetComputeRootDescriptorTable(kRootDepth, depth_srv);
        list->SetComputeRootUnorderedAccessView(kRootStats, stats_->GetGPUVirtualAddress());
        list->SetComputeRootUnorderedAccessView(kRootDump, dumping ? dump_->GetGPUVirtualAddress()
                                                                   : stats_->GetGPUVirtualAddress());
        list->Dispatch((width + 7u) / 8u, (height + 7u) / 8u, 1u);
        if (dumping) {
            D3D12_RESOURCE_BARRIER dump_barrier = transition(dump_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                                             D3D12_RESOURCE_STATE_COPY_SOURCE);
            list->ResourceBarrier(1u, &dump_barrier);
            list->CopyBufferRegion(dump_readback_.Get(), 0u, dump_.Get(), 0u,
                                   static_cast<std::uint64_t>(width) * height * 16u);
            dump_pending_ = true;
            dump_frame_ = frame_index_;
            dump_draws_ = draws_;
            dump_infos_ = infos_;
            dump_width_ = width;
            dump_height_ = height;
        }
        barrier = transition(stats_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        list->ResourceBarrier(1u, &barrier);
        list->CopyBufferRegion(readback_.Get(), slot * kReadbackSlotBytes, stats_.Get(), 0u, kStatsWords * 4u);
        barrier = transition(stats_.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
        list->ResourceBarrier(1u, &barrier);
        if (timestamps_) {
            list->EndQuery(timestamps_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, slot * kTimestampsPerSlot + 2u);
        }
    }
    if (record_results) {
        if (timestamps_) {
            list->ResolveQueryData(timestamps_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, slot * kTimestampsPerSlot,
                                   validated ? 3u : 2u, readback_.Get(), slot * kReadbackSlotBytes + 64u);
        }
        slots_[slot] = ResultSlot{frame_index_, true, validated, static_cast<std::uint32_t>(draws_.size()),
                                  triangles_};
    }
    return true;
}

}  // namespace galaxy::gx
