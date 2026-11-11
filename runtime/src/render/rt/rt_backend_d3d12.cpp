// SPDX-License-Identifier: GPL-3.0-only
// The tracer on Direct3D 12 (rt_backend.h): DXR 1.1, acceleration structures in pages and inline ray queries in compute
// shaders.
#include "galaxy/render/rt/rt_backend.h"

#include "galaxy/render/renderer_d3d12.h"
#include "galaxy/render/rt/rt_shaders.h"
#include "render/barriers_d3d12.h"

#include <d3d12.h>
#include <wrl/client.h>

#include <cstdio>
#include <cstring>
#include <string>

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

    // Passes: one root signature for every pass (rt_bindings.hlsli), a pipeline a shader made at its first dispatch.
    struct Image {
        ComPtr<ID3D12Resource> texture;
        DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
        gpu::Use use = gpu::Use::ComputeRead;
        std::uint32_t depth = 1u;  // above 1: a volume
    };
    std::vector<Image> images;
    DescriptorRing* descriptors = nullptr;
    ComPtr<ID3D12RootSignature> pass_root;
    ComPtr<ID3D12PipelineState> pipelines[static_cast<unsigned>(RtShader::Count)];
    bool refused[static_cast<unsigned>(RtShader::Count)]{};
    // Counters: the GPU adds to `counters`; `counters_zero` clears it; a frame slot's copy is read a frame later.
    ComPtr<ID3D12Resource> counters, counters_zero;
    ComPtr<ID3D12Resource> counters_read[kFramesInFlight];
    const std::uint32_t* counters_mapped[kFramesInFlight]{};
    bool counters_bound = false;
    // Buffers passes write and builds read.
    struct OwnBuffer {
        ComPtr<ID3D12Resource> buffer;
        bool for_builds = false;
    };
    std::vector<OwnBuffer> buffers;

    void transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) const {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = resource;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = before;
        barrier.Transition.StateAfter = after;
        commands->ResourceBarrier(1, &barrier);
    }

    [[nodiscard]] bool make_passes();
    [[nodiscard]] std::uint64_t own_address(const TriangleInput& input) const {
        return input.own_buffer < buffers.size() && buffers[input.own_buffer].buffer != nullptr
                   ? buffers[input.own_buffer].buffer->GetGPUVirtualAddress()
                   : 0u;
    }

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

// `own`: the address of the tracer's buffer the input names, or 0.
D3D12_RAYTRACING_GEOMETRY_DESC geometry_of(const TriangleInput& input, std::uint64_t own) {
    D3D12_RAYTRACING_GEOMETRY_DESC g{};
    g.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    // Opaque here; an instance whose material tests alpha says so itself.
    g.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
    g.Triangles.VertexBuffer.StartAddress = own + input.positions;
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
    ok = ok && state->make_passes();
    if (!ok) {
        delete state;
        return false;
    }
    state_ = state;
    return true;
}

// The passes' root signature: b0 constants, t0 the scene, t1-t4 buffers (root descriptors); t5-t20 textures and
// u0-u7 images (tables); u8 the counters, u9 a buffer written, t21 up the mesh memory (root descriptors); the renderer's
// textures (a table); b1 the pass's constants (root descriptor).
bool Backend::State::make_passes() {
    D3D12_DESCRIPTOR_RANGE ranges[3]{};
    // Every texture of the renderer's (t0 up in space1, as the draws' programs see them) and one sampler (s0).
    ranges[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[2].NumDescriptors = UINT_MAX;
    ranges[2].RegisterSpace = 1u;
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[0].NumDescriptors = kPassTextures + kPassReads;
    ranges[0].BaseShaderRegister = 1u + kPassBuffers;
    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    ranges[1].NumDescriptors = kPassWrites;
    D3D12_ROOT_PARAMETER params[12u + kPassMeshBuffers]{};
    params[10u + kPassMeshBuffers].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[10u + kPassMeshBuffers].DescriptorTable.NumDescriptorRanges = 1;
    params[10u + kPassMeshBuffers].DescriptorTable.pDescriptorRanges = &ranges[2];
    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    for (unsigned i = 0; i < 1u + kPassBuffers; ++i) {
        params[1u + i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[1u + i].Descriptor.ShaderRegister = i;
    }
    for (unsigned i = 0; i < 2u; ++i) {
        params[6u + i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[6u + i].DescriptorTable.NumDescriptorRanges = 1;
        params[6u + i].DescriptorTable.pDescriptorRanges = &ranges[i];
    }
    params[8].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    params[8].Descriptor.ShaderRegister = kPassWrites;
    params[9].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    params[9].Descriptor.ShaderRegister = kPassWrites + 1u;
    // t21 up: the mesh memory (root descriptors: buffers of the renderer's, by address).
    for (unsigned i = 0; i < kPassMeshBuffers; ++i) {
        params[10u + i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[10u + i].Descriptor.ShaderRegister = 1u + kPassBuffers + kPassTextures + kPassReads + i;
    }
    params[11u + kPassMeshBuffers].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[11u + kPassMeshBuffers].Descriptor.ShaderRegister = 1u;
    D3D12_ROOT_SIGNATURE_DESC desc{};
    desc.NumParameters = 12u + kPassMeshBuffers;
    desc.NumStaticSamplers = 1;
    desc.pStaticSamplers = &sampler;
    desc.pParameters = params;
    ComPtr<ID3DBlob> blob, errors;
    if (FAILED(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors)) ||
        FAILED(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&pass_root)))) {
        return false;
    }
    const std::uint64_t bytes = std::uint64_t{kCounters} * 4u;
    counters = buffer(bytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                      L"Nebula RT Counters");
    counters_zero = buffer(bytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_FLAG_NONE, L"Nebula RT Zeros");
    void* zeros = nullptr;
    if (counters == nullptr || counters_zero == nullptr || FAILED(counters_zero->Map(0, nullptr, &zeros))) {
        return false;
    }
    std::memset(zeros, 0, bytes);
    counters_zero->Unmap(0, nullptr);
    for (unsigned slot = 0; slot < kFramesInFlight; ++slot) {
        counters_read[slot] =
            buffer(bytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_FLAG_NONE, L"Nebula RT Counters Read");
        void* mapped = nullptr;
        if (counters_read[slot] == nullptr || FAILED(counters_read[slot]->Map(0, nullptr, &mapped))) {
            return false;
        }
        std::memset(mapped, 0, bytes);
        counters_mapped[slot] = static_cast<const std::uint32_t*>(mapped);
    }
    return true;
}

void Backend::stop() noexcept {
    delete state_;
    state_ = nullptr;
}

BuildSizes Backend::sizes(const TriangleInput& input, BuildKind kind) const {
    const D3D12_RAYTRACING_GEOMETRY_DESC geometry = geometry_of(input, state_->own_address(input));
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

void Backend::frame(unsigned) noexcept {
    state_->counters_bound = false;  // the frame's first pass that counts zeroes them
}

void Backend::begin(void* commands, unsigned frame_slot, void* descriptors) noexcept {
    state_->commands.Reset();
    static_cast<ID3D12GraphicsCommandList*>(commands)->QueryInterface(IID_PPV_ARGS(&state_->commands));
    state_->frame_slot = frame_slot % kFramesInFlight;
    state_->asked = false;
    state_->descriptors = static_cast<DescriptorRing*>(descriptors);
}

void Backend::build(const TriangleInput& input, BuildKind kind, const Structure& to, const Range& scratch, bool refit) {
    const D3D12_RAYTRACING_GEOMETRY_DESC geometry = geometry_of(input, state_->own_address(input));
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

BuildSizes Backend::scene_sizes(std::uint32_t instances) const {
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs{};
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    inputs.NumDescs = instances;
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{};
    state_->device->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);
    return {info.ResultDataMaxSizeInBytes, info.ScratchDataSizeInBytes, 0u};
}

void Backend::build_scene(gpu::Address instances, std::uint32_t count, const Structure& to, const Range& scratch) {
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC desc{};
    desc.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    desc.Inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    desc.Inputs.NumDescs = count;
    desc.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    desc.Inputs.InstanceDescs = instances;
    desc.DestAccelerationStructureData = to.name;
    desc.ScratchAccelerationStructureData = state_->pages[scratch.page].address + scratch.offset;
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

namespace {

constexpr DXGI_FORMAT format_of(ImageFormat format) noexcept {
    switch (format) {
    case ImageFormat::Rgba8: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case ImageFormat::Rgba16f: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case ImageFormat::Rg16f: return DXGI_FORMAT_R16G16_FLOAT;
    case ImageFormat::R16f: return DXGI_FORMAT_R16_FLOAT;
    case ImageFormat::R8: return DXGI_FORMAT_R8_UNORM;
    case ImageFormat::R32f: return DXGI_FORMAT_R32_FLOAT;
    case ImageFormat::Rgb10a2: return DXGI_FORMAT_R10G10B10A2_UNORM;
    case ImageFormat::Rgba32ui: return DXGI_FORMAT_R32G32B32A32_UINT;
    case ImageFormat::Rgba32f: return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case ImageFormat::Rg16ui: return DXGI_FORMAT_R16G16_UINT;
    }
    return DXGI_FORMAT_UNKNOWN;
}

}  // namespace

std::uint32_t Backend::make_image(ImageFormat format, std::uint32_t width, std::uint32_t height, const char* name, std::uint32_t depth) {
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = depth > 1u ? D3D12_RESOURCE_DIMENSION_TEXTURE3D : D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = width;
    rd.Height = height;
    rd.DepthOrArraySize = static_cast<UINT16>(depth > 1u ? depth : 1u);
    rd.MipLevels = 1;
    rd.Format = format_of(format);
    rd.SampleDesc.Count = 1;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    State::Image image;
    image.format = rd.Format;
    image.depth = depth > 1u ? depth : 1u;
    if (width == 0u || height == 0u || depth > 2048u ||
        FAILED(create_texture(state_->device.Get(), &heap, D3D12_HEAP_FLAG_NONE, &rd, gpu::Use::ComputeRead, nullptr,
                              IID_PPV_ARGS(&image.texture)))) {
        return kNoImage;
    }
    if (name != nullptr) {
        image.texture->SetName(std::wstring(name, name + std::strlen(name)).c_str());
    }
    for (std::uint32_t i = 0; i < state_->images.size(); ++i) {
        if (state_->images[i].texture == nullptr) {
            state_->images[i] = std::move(image);
            return i;
        }
    }
    state_->images.push_back(std::move(image));
    return static_cast<std::uint32_t>(state_->images.size() - 1u);
}

void Backend::free_image(std::uint32_t image) noexcept {
    if (state_ != nullptr && image < state_->images.size()) {
        state_->images[image] = State::Image{};
    }
}

void* Backend::image_to_copy(std::uint32_t image) {
    if (image >= state_->images.size() || state_->images[image].texture == nullptr) {
        return nullptr;
    }
    State::Image& target = state_->images[image];
    BarriersD3D12 uses{state_->commands.Get()};
    uses.texture(target.texture.Get(), target.use, gpu::Use::CopySource);
    uses.issue();
    target.use = gpu::Use::CopySource;
    return target.texture.Get();
}

void* Backend::image_to_sample(std::uint32_t image) {
    if (image >= state_->images.size() || state_->images[image].texture == nullptr) {
        return nullptr;
    }
    State::Image& target = state_->images[image];
    BarriersD3D12 uses{state_->commands.Get()};
    uses.texture(target.texture.Get(), target.use, gpu::Use::ShaderRead);
    uses.issue();
    target.use = gpu::Use::ShaderRead;
    return target.texture.Get();
}

void Backend::dispatch(RtShader shader, const PassBinds& binds, std::uint32_t groups_x, std::uint32_t groups_y) {
    State& s = *state_;
    const auto index = static_cast<unsigned>(shader);
    if (index >= static_cast<unsigned>(RtShader::Count) || s.refused[index] || s.descriptors == nullptr || groups_x == 0u || groups_y == 0u) {
        return;
    }
    if (s.pipelines[index] == nullptr) {
        const RtShaderBytes code = rt_shader(shader);
        D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};
        desc.pRootSignature = s.pass_root.Get();
        desc.CS = {code.data, code.size};
        const HRESULT made = code.size != 0u ? s.device->CreateComputePipelineState(&desc, IID_PPV_ARGS(&s.pipelines[index])) : E_FAIL;
        if (FAILED(made)) {
            s.refused[index] = true;
            std::fprintf(stderr, "[rt] pass %u: no pipeline (%08lx); the pass is left out\n", index, static_cast<unsigned long>(made));
            return;
        }
    }
    ID3D12GraphicsCommandList4* const list = s.commands.Get();
    const auto image_of = [&](std::uint32_t image) -> State::Image* {
        return image < s.images.size() && s.images[image].texture != nullptr ? &s.images[image] : nullptr;
    };
    // Each image in the use this pass has for it; one written again without a change of use waits for the write before.
    bool written_again = binds.counters;
    {
        BarriersD3D12 uses{list};
        for (const std::uint32_t read : binds.reads) {
            if (State::Image* image = image_of(read)) {
                uses.texture(image->texture.Get(), image->use, gpu::Use::ComputeRead);
                image->use = gpu::Use::ComputeRead;
            }
        }
        for (const std::uint32_t write : binds.writes) {
            if (State::Image* image = image_of(write)) {
                written_again = written_again || image->use == gpu::Use::ComputeWrite;
                uses.texture(image->texture.Get(), image->use, gpu::Use::ComputeWrite);
                image->use = gpu::Use::ComputeWrite;
            }
        }
        uses.issue();
    }
    if (binds.counters && !s.counters_bound) {
        s.transition(s.counters.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
        list->CopyBufferRegion(s.counters.Get(), 0u, s.counters_zero.Get(), 0u, std::uint64_t{kCounters} * 4u);
        s.transition(s.counters.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        s.counters_bound = true;
    } else if (written_again) {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        list->ResourceBarrier(1, &barrier);
    }

    // The views: t5-t12 then u0-u3 in one run of this frame's descriptors (a slot left alone holds the null view). A
    // pass that binds none takes no descriptors.
    constexpr unsigned kViews = kPassTextures + kPassReads + kPassWrites;
    bool views = false;
    for (unsigned i = 0; i < kPassTextures; ++i) {
        views = views || binds.textures[i].texture != nullptr;
    }
    for (unsigned i = 0; i < kPassReads; ++i) {
        views = views || image_of(binds.reads[i]) != nullptr;
    }
    for (unsigned i = 0; i < kPassWrites; ++i) {
        views = views || image_of(binds.writes[i]) != nullptr;
    }
    views = views || binds.write_texture.texture != nullptr;
    const UINT step = s.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    const DescriptorRing::Table table = views ? s.descriptors->allocate(kViews) : DescriptorRing::Table{};
    const auto cpu = [&](unsigned i) {
        D3D12_CPU_DESCRIPTOR_HANDLE handle = table.cpu;
        handle.ptr += static_cast<SIZE_T>(i) * step;
        return handle;
    };
    const auto gpu_at = [&](unsigned i) {
        D3D12_GPU_DESCRIPTOR_HANDLE handle = table.gpu;
        handle.ptr += static_cast<UINT64>(i) * step;
        return handle;
    };
    D3D12_SHADER_RESOURCE_VIEW_DESC read{};
    read.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    read.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    read.Texture2D.MipLevels = 1;
    for (unsigned i = 0; views && i < kPassTextures; ++i) {
        if (binds.textures[i].texture != nullptr) {
            read.Format = static_cast<DXGI_FORMAT>(binds.textures[i].format);
            s.device->CreateShaderResourceView(static_cast<ID3D12Resource*>(binds.textures[i].texture), &read, cpu(i));
        }
    }
    for (unsigned i = 0; views && i < kPassReads; ++i) {
        if (const State::Image* image = image_of(binds.reads[i])) {
            D3D12_SHADER_RESOURCE_VIEW_DESC own = read;
            own.Format = image->format;
            if (image->depth > 1u) {
                own.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
                own.Texture3D = {};
                own.Texture3D.MipLevels = 1;
            }
            s.device->CreateShaderResourceView(image->texture.Get(), &own, cpu(kPassTextures + i));
        }
    }
    for (unsigned i = 0; views && i < kPassWrites; ++i) {
        if (const State::Image* image = image_of(binds.writes[i])) {
            D3D12_UNORDERED_ACCESS_VIEW_DESC write{};
            write.Format = image->format;
            write.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            if (image->depth > 1u) {
                write.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
                write.Texture3D.WSize = image->depth;
            }
            s.device->CreateUnorderedAccessView(image->texture.Get(), nullptr, &write, cpu(kPassTextures + kPassReads + i));
        }
    }
    if (views && binds.write_texture.texture != nullptr) {
        D3D12_UNORDERED_ACCESS_VIEW_DESC write{};
        write.Format = static_cast<DXGI_FORMAT>(binds.write_texture.format);
        write.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        s.device->CreateUnorderedAccessView(static_cast<ID3D12Resource*>(binds.write_texture.texture), nullptr, &write,
                                            cpu(kPassTextures + kPassReads + kPassWrites - 1u));
    }

    list->SetComputeRootSignature(s.pass_root.Get());
    list->SetPipelineState(s.pipelines[index].Get());
    s.descriptors->bind(list);
    if (binds.constants.at != 0u) {
        list->SetComputeRootConstantBufferView(0, binds.constants.at);
    }
    if (binds.pass_constants.at != 0u) {
        list->SetComputeRootConstantBufferView(11u + kPassMeshBuffers, binds.pass_constants.at);
    }
    if (binds.scene.name != 0u) {
        list->SetComputeRootShaderResourceView(1, binds.scene.name);
    }
    for (unsigned i = 0; i < kPassBuffers; ++i) {
        if (binds.buffers[i].at != 0u) {
            list->SetComputeRootShaderResourceView(2u + i, binds.buffers[i].at);
        }
    }
    for (unsigned i = 0; i < kPassMeshBuffers; ++i) {
        if (binds.mesh[i].at != 0u) {
            list->SetComputeRootShaderResourceView(10u + i, binds.mesh[i].at);
        }
    }
    list->SetComputeRootDescriptorTable(10u + kPassMeshBuffers, s.descriptors->texture_gpu_start());
    if (views) {
        list->SetComputeRootDescriptorTable(6, gpu_at(0u));
        list->SetComputeRootDescriptorTable(7, gpu_at(kPassTextures + kPassReads));
    }
    if (binds.counters) {
        list->SetComputeRootUnorderedAccessView(8, s.counters->GetGPUVirtualAddress());
    }
    if (binds.write_buffer < s.buffers.size() && s.buffers[binds.write_buffer].buffer != nullptr) {
        list->SetComputeRootUnorderedAccessView(9, s.buffers[binds.write_buffer].buffer->GetGPUVirtualAddress());
    }
    list->Dispatch(groups_x, groups_y, 1);
}

void Backend::end_counters() {
    State& s = *state_;
    if (!s.counters_bound) {
        return;
    }
    s.transition(s.counters.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    s.commands->CopyResource(s.counters_read[s.frame_slot].Get(), s.counters.Get());
    s.transition(s.counters.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
}

void Backend::read_counters(unsigned frame_slot, std::uint32_t out[kCounters]) const {
    std::memcpy(out, state_->counters_mapped[frame_slot % kFramesInFlight], std::size_t{kCounters} * 4u);
}

std::uint32_t Backend::make_buffer(std::uint64_t bytes, const char* name) {
    State::OwnBuffer made;
    made.buffer = state_->buffer(bytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                 D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, L"Nebula RT Vertices");
    if (made.buffer == nullptr) {
        return kNoBuffer;
    }
    if (name != nullptr) {
        made.buffer->SetName(std::wstring(name, name + std::strlen(name)).c_str());
    }
    for (std::uint32_t i = 0; i < state_->buffers.size(); ++i) {
        if (state_->buffers[i].buffer == nullptr) {
            state_->buffers[i] = std::move(made);
            return i;
        }
    }
    state_->buffers.push_back(std::move(made));
    return static_cast<std::uint32_t>(state_->buffers.size() - 1u);
}

void Backend::free_buffer(std::uint32_t buffer) noexcept {
    if (state_ != nullptr && buffer < state_->buffers.size()) {
        state_->buffers[buffer] = State::OwnBuffer{};
    }
}

void Backend::buffer_for(std::uint32_t buffer, bool builds) {
    if (buffer >= state_->buffers.size() || state_->buffers[buffer].buffer == nullptr || state_->buffers[buffer].for_builds == builds) {
        return;
    }
    State::OwnBuffer& own = state_->buffers[buffer];
    state_->transition(own.buffer.Get(),
                       builds ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                       builds ? D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE : D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    own.for_builds = builds;
}

// A root descriptor starts at any address.
BufferView Backend::view_of(gpu::Address at, std::uint64_t bytes, std::uint32_t& offset) const {
    offset = 0u;
    return {at, bytes};
}

}  // namespace galaxy::render::rt
