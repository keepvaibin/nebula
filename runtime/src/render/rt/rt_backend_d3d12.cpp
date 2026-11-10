// SPDX-License-Identifier: GPL-3.0-only
// The tracer on Direct3D 12 (rt_backend.h): DXR 1.1, acceleration structures in pages and inline ray queries in compute
// shaders.
#include "galaxy/render/rt/rt_backend.h"

#include "galaxy/render/rt/rt_shaders.h"

#include <d3d12.h>
#include <wrl/client.h>

#include <cstdio>

namespace galaxy::render::rt {

using Microsoft::WRL::ComPtr;

bool shaders_load(gpu::Device* device) {
    const RtShaderBytes probe = rt_shader(RtShader::Probe);
    if (device == nullptr || probe.size == 0u) {
        std::fprintf(stderr, "[rt] shaders: none in this build\n");
        return false;
    }
    // rt_probe.hlsl: the scene at t0, its output at u0, its constants at b0, each a root descriptor.
    D3D12_ROOT_PARAMETER params[3]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    D3D12_ROOT_SIGNATURE_DESC desc{};
    desc.NumParameters = 3;
    desc.pParameters = params;
    ComPtr<ID3DBlob> blob, errors;
    ComPtr<ID3D12RootSignature> root;
    if (FAILED(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors)) ||
        FAILED(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&root)))) {
        std::fprintf(stderr, "[rt] shaders: the root signature failed\n");
        return false;
    }
    D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline_desc{};
    pipeline_desc.pRootSignature = root.Get();
    pipeline_desc.CS = {probe.data, probe.size};
    ComPtr<ID3D12PipelineState> pipeline;
    const HRESULT made = device->CreateComputePipelineState(&pipeline_desc, IID_PPV_ARGS(&pipeline));
    if (FAILED(made)) {
        std::fprintf(stderr, "[rt] shaders: the driver refused the measuring pipeline (%08lx)\n", static_cast<unsigned long>(made));
        return false;
    }
    std::fprintf(stderr, "[rt] shaders: loaded (d3d12, %zu bytes)\n", probe.size);
    return true;
}

struct Backend::State {
    struct Page {
        ComPtr<ID3D12Resource> buffer;
        std::uint64_t bytes = 0u;
        std::uint64_t address = 0u;
    };
    ComPtr<ID3D12Device5> device;
    ComPtr<ID3D12GraphicsCommandList4> commands;
    std::vector<Page> pages;
    // Compacted sizes: the GPU writes them into `sizes`, which is copied whole into the frame slot's readback buffer.
    ComPtr<ID3D12Resource> sizes;
    ComPtr<ID3D12Resource> sizes_read[kFramesInFlight];
    const std::uint64_t* sizes_mapped[kFramesInFlight]{};
    unsigned frame_slot = 0u;
    bool asked = false;

    [[nodiscard]] ComPtr<ID3D12Resource> buffer(std::uint64_t bytes, D3D12_HEAP_TYPE heap, D3D12_RESOURCE_STATES state,
                                                D3D12_RESOURCE_FLAGS flags, const wchar_t* name) const {
        D3D12_HEAP_PROPERTIES hp{};
        hp.Type = heap;
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        rd.Width = bytes;
        rd.Height = 1;
        rd.DepthOrArraySize = 1;
        rd.MipLevels = 1;
        rd.SampleDesc.Count = 1;
        rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        rd.Flags = flags;
        ComPtr<ID3D12Resource> out;
        if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, state, nullptr, IID_PPV_ARGS(&out)))) {
            return {};
        }
        out->SetName(name);
        return out;
    }
};

namespace {

D3D12_RAYTRACING_GEOMETRY_DESC geometry_of(const TriangleInput& input) {
    D3D12_RAYTRACING_GEOMETRY_DESC g{};
    g.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    // Opaque here; an instance whose material tests alpha says so itself.
    g.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
    g.Triangles.VertexBuffer.StartAddress = input.positions;
    g.Triangles.VertexBuffer.StrideInBytes = input.position_stride;
    g.Triangles.VertexCount = input.vertex_count;
    g.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
    if (input.indices != 0u) {
        g.Triangles.IndexBuffer = input.indices;
        g.Triangles.IndexCount = input.index_count;
        g.Triangles.IndexFormat = input.index_bytes == 2u ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
    }
    return g;
}

D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS flags_of(BuildKind kind) {
    return kind == BuildKind::Static ? D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE |
                                           D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION
                                     : D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD |
                                           D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
}

}  // namespace

Backend::Backend() = default;

Backend::~Backend() { stop(); }

bool Backend::started() const noexcept { return state_ != nullptr; }

bool Backend::start(gpu::Device* device) {
    stop();
    auto state = new State{};
    if (device == nullptr || FAILED(device->QueryInterface(IID_PPV_ARGS(&state->device)))) {
        delete state;
        return false;
    }
    const std::uint64_t bytes = std::uint64_t{kSizeSlots} * 8u;
    state->sizes = state->buffer(bytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                 D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, L"Nebula RT Sizes");
    bool ok = state->sizes != nullptr;
    for (unsigned slot = 0; ok && slot < kFramesInFlight; ++slot) {
        state->sizes_read[slot] =
            state->buffer(bytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_FLAG_NONE, L"Nebula RT Sizes Read");
        void* mapped = nullptr;
        ok = state->sizes_read[slot] != nullptr && SUCCEEDED(state->sizes_read[slot]->Map(0, nullptr, &mapped));
        state->sizes_mapped[slot] = static_cast<const std::uint64_t*>(mapped);
    }
    if (!ok) {
        delete state;
        return false;
    }
    state_ = state;
    return true;
}

void Backend::stop() noexcept {
    delete state_;
    state_ = nullptr;
}

BuildSizes Backend::sizes(const TriangleInput& input, BuildKind kind) const {
    const D3D12_RAYTRACING_GEOMETRY_DESC geometry = geometry_of(input);
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs{};
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    inputs.Flags = flags_of(kind);
    inputs.NumDescs = 1;
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.pGeometryDescs = &geometry;
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{};
    state_->device->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);
    return {info.ResultDataMaxSizeInBytes, info.ScratchDataSizeInBytes, info.UpdateScratchDataSizeInBytes};
}

std::uint32_t Backend::make_page(std::uint64_t bytes, bool scratch) {
    State::Page page;
    page.buffer = state_->buffer(bytes, D3D12_HEAP_TYPE_DEFAULT,
                                 scratch ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
                                 D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, scratch ? L"Nebula RT Scratch" : L"Nebula RT Structures");
    if (page.buffer == nullptr) {
        return ~0u;
    }
    page.bytes = bytes;
    page.address = page.buffer->GetGPUVirtualAddress();
    for (std::uint32_t i = 0; i < state_->pages.size(); ++i) {
        if (state_->pages[i].buffer == nullptr) {
            state_->pages[i] = std::move(page);
            return i;
        }
    }
    state_->pages.push_back(std::move(page));
    return static_cast<std::uint32_t>(state_->pages.size() - 1u);
}

void Backend::free_page(std::uint32_t page) noexcept {
    if (state_ != nullptr && page < state_->pages.size()) {
        state_->pages[page] = State::Page{};
    }
}

Structure Backend::make_structure(const Range& range, bool) {
    Structure out;
    out.range = range;
    out.name = state_->pages[range.page].address + range.offset;
    return out;
}

void Backend::free_structure(Structure& structure) noexcept { structure = Structure{}; }

void Backend::begin(void* commands, unsigned frame_slot) noexcept {
    state_->commands.Reset();
    static_cast<ID3D12GraphicsCommandList*>(commands)->QueryInterface(IID_PPV_ARGS(&state_->commands));
    state_->frame_slot = frame_slot % kFramesInFlight;
    state_->asked = false;
}

void Backend::build(const TriangleInput& input, BuildKind kind, const Structure& to, const Range& scratch, bool refit) {
    const D3D12_RAYTRACING_GEOMETRY_DESC geometry = geometry_of(input);
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC desc{};
    desc.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    desc.Inputs.Flags = flags_of(kind);
    desc.Inputs.NumDescs = 1;
    desc.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    desc.Inputs.pGeometryDescs = &geometry;
    desc.DestAccelerationStructureData = to.name;
    desc.ScratchAccelerationStructureData = state_->pages[scratch.page].address + scratch.offset;
    if (refit) {
        desc.Inputs.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
        desc.SourceAccelerationStructureData = to.name;
    }
    state_->commands->BuildRaytracingAccelerationStructure(&desc, 0, nullptr);
}

void Backend::barrier() {
    // One barrier for every structure and scratch range: nothing names a resource.
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    state_->commands->ResourceBarrier(1, &barrier);
}

void Backend::ask_compacted_size(const Structure& structure, std::uint32_t slot) {
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC desc{};
    desc.DestBuffer = state_->sizes->GetGPUVirtualAddress() + std::uint64_t{slot} * 8u;
    desc.InfoType = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_COMPACTED_SIZE;
    const D3D12_GPU_VIRTUAL_ADDRESS source = structure.name;
    state_->commands->EmitRaytracingAccelerationStructurePostbuildInfo(&desc, 1, &source);
    state_->asked = true;
}

void Backend::end_size_asks() {
    if (!state_->asked) {
        return;
    }
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = state_->sizes.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    state_->commands->ResourceBarrier(1, &barrier);
    state_->commands->CopyResource(state_->sizes_read[state_->frame_slot].Get(), state_->sizes.Get());
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    state_->commands->ResourceBarrier(1, &barrier);
    state_->asked = false;
}

std::uint64_t Backend::compacted_size(std::uint32_t slot, unsigned frame_slot) const {
    return state_->sizes_mapped[frame_slot % kFramesInFlight][slot];
}

void Backend::compact(const Structure& from, const Structure& to) {
    state_->commands->CopyRaytracingAccelerationStructure(to.name, from.name, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE_COMPACT);
}

}  // namespace galaxy::render::rt
