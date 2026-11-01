// Tests of LinePointRasterParams decoding and of the native line/point geometry stages (native_lines.hlsl, compiled
// at build) and their PSO selection, rasterized on WARP.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#pragma warning(push, 0)
#include <Windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#pragma warning(pop)

#include "galaxy/render/drawing_state.h"
#include "galaxy/render/line_point_params.h"
#include "galaxy/render/pipeline_cache.h"
#include "galaxy/render/renderer_d3d12.h"
#include "galaxy/render/native_shaders.h"
#include "line_point_test_vs.h"
#include "line_point_test_ps.h"
#include "galaxy/render/draw_constants.h"
#include "galaxy/render/render_vertex.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

namespace {

using Microsoft::WRL::ComPtr;

[[noreturn]] void throw_hr(const char* operation, HRESULT hr) {
    char message[160]{};
    std::snprintf(
        message,
        sizeof(message),
        "%s failed with HRESULT 0x%08X",
        operation,
        static_cast<unsigned>(hr));
    throw std::runtime_error(message);
}

void check_hr(HRESULT hr, const char* operation) {
    if (FAILED(hr)) {
        throw_hr(operation, hr);
    }
}

ComPtr<ID3DBlob> compile_shader(
    std::string_view source,
    const char* target,
    const char* label) {
    ComPtr<ID3DBlob> code;
    ComPtr<ID3DBlob> errors;
    const HRESULT hr = D3DCompile(
        source.data(),
        source.size(),
        label,
        nullptr,
        nullptr,
        "main",
        target,
        D3DCOMPILE_ENABLE_STRICTNESS |
            D3DCOMPILE_PACK_MATRIX_ROW_MAJOR |
            D3DCOMPILE_OPTIMIZATION_LEVEL3,
        0u,
        &code,
        &errors);
    if (FAILED(hr)) {
        const char* diagnostics = errors
            ? static_cast<const char*>(errors->GetBufferPointer())
            : "(no compiler diagnostics)";
        throw std::runtime_error(
            std::string{label} + " failed to compile: " + diagnostics);
    }
    return code;
}

D3D12_HEAP_PROPERTIES heap_properties(D3D12_HEAP_TYPE type) {
    D3D12_HEAP_PROPERTIES properties{};
    properties.Type = type;
    properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    properties.CreationNodeMask = 1u;
    properties.VisibleNodeMask = 1u;
    return properties;
}

D3D12_RESOURCE_DESC buffer_desc(UINT64 size) {
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = size;
    desc.Height = 1u;
    desc.DepthOrArraySize = 1u;
    desc.MipLevels = 1u;
    desc.SampleDesc.Count = 1u;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return desc;
}

bool expect(bool condition, const char* label) {
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", label);
        return false;
    }
    return true;
}

bool expect_near(float actual, float expected, const char* label) {
    if (std::abs(actual - expected) > 0.0001f) {
        std::fprintf(
            stderr,
            "FAILED: %s (actual=%g expected=%g)\n",
            label,
            static_cast<double>(actual),
            static_cast<double>(expected));
        return false;
    }
    return true;
}

struct RasterBounds {
    std::uint64_t pixel_count = 0u;
    UINT min_x = std::numeric_limits<UINT>::max();
    UINT min_y = std::numeric_limits<UINT>::max();
    UINT max_x = 0u;
    UINT max_y = 0u;
    std::uint64_t top_red = 0u;
    std::uint64_t bottom_red = 0u;
    std::uint64_t left_red = 0u;
    std::uint64_t right_red = 0u;
    std::uint64_t top_green = 0u;
    std::uint64_t bottom_green = 0u;
};

enum class RasterCase {
    Point,
    HorizontalLine,
    VerticalLine,
};

class WarpRasterHarness {
public:
    WarpRasterHarness() {
        ComPtr<IDXGIFactory6> factory;
        check_hr(
            CreateDXGIFactory2(0u, IID_PPV_ARGS(&factory)),
            "CreateDXGIFactory2");
        ComPtr<IDXGIAdapter> warp_adapter;
        check_hr(
            factory->EnumWarpAdapter(IID_PPV_ARGS(&warp_adapter)),
            "EnumWarpAdapter");
        check_hr(
            D3D12CreateDevice(
                warp_adapter.Get(),
                D3D_FEATURE_LEVEL_11_0,
                IID_PPV_ARGS(&device_)),
            "D3D12CreateDevice(WARP)");

        D3D12_COMMAND_QUEUE_DESC queue_desc{};
        queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        check_hr(
            device_->CreateCommandQueue(
                &queue_desc, IID_PPV_ARGS(&queue_)),
            "CreateCommandQueue");
        check_hr(
            device_->CreateFence(
                0u, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)),
            "CreateFence");
        fence_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (fence_event_ == nullptr) {
            throw std::runtime_error("CreateEventW failed");
        }

        create_root_signature();
        create_pipelines();
    }

    ~WarpRasterHarness() {
        if (fence_event_ != nullptr) {
            CloseHandle(fence_event_);
        }
    }

    WarpRasterHarness(const WarpRasterHarness&) = delete;
    WarpRasterHarness& operator=(const WarpRasterHarness&) = delete;

    [[nodiscard]] bool production_descriptor_selection_valid() const noexcept {
        return descriptor_selection_valid_;
    }

    [[nodiscard]] bool missing_geometry_shader_hard_fails() const noexcept {
        return missing_geometry_shader_hard_fails_;
    }

    RasterBounds run(
        RasterCase raster_case,
        unsigned scale,
        const galaxy::render::LinePointRasterParams& params) {
        const UINT dimension = 64u * scale;

        D3D12_RESOURCE_DESC target_desc{};
        target_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        target_desc.Width = dimension;
        target_desc.Height = dimension;
        target_desc.DepthOrArraySize = 1u;
        target_desc.MipLevels = 1u;
        target_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        target_desc.SampleDesc.Count = 1u;
        target_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        target_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        D3D12_CLEAR_VALUE clear_value{};
        clear_value.Format = target_desc.Format;

        ComPtr<ID3D12Resource> target;
        const D3D12_HEAP_PROPERTIES default_heap =
            heap_properties(D3D12_HEAP_TYPE_DEFAULT);
        check_hr(
            device_->CreateCommittedResource(
                &default_heap,
                D3D12_HEAP_FLAG_NONE,
                &target_desc,
                D3D12_RESOURCE_STATE_RENDER_TARGET,
                &clear_value,
                IID_PPV_ARGS(&target)),
            "CreateCommittedResource(target)");

        D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc{};
        rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtv_heap_desc.NumDescriptors = 1u;
        ComPtr<ID3D12DescriptorHeap> rtv_heap;
        check_hr(
            device_->CreateDescriptorHeap(
                &rtv_heap_desc, IID_PPV_ARGS(&rtv_heap)),
            "CreateDescriptorHeap(RTV)");
        const D3D12_CPU_DESCRIPTOR_HANDLE rtv =
            rtv_heap->GetCPUDescriptorHandleForHeapStart();
        device_->CreateRenderTargetView(target.Get(), nullptr, rtv);

        galaxy::render::VertexConstants constants{};
        constants.line_point_raster[0] = params.viewport_width_pixels;
        constants.line_point_raster[1] = params.viewport_height_pixels;
        constants.line_point_raster[2] = params.line_width_pixels;
        constants.line_point_raster[3] = params.point_size_pixels;
        constants.line_point_tex_offsets[0] = params.line_texcoord_mask;
        constants.line_point_tex_offsets[1] = params.point_texcoord_mask;
        constants.line_point_tex_offsets[2] = params.line_texcoord_divisor;
        constants.line_point_tex_offsets[3] = params.point_texcoord_divisor;

        const D3D12_HEAP_PROPERTIES upload_heap =
            heap_properties(D3D12_HEAP_TYPE_UPLOAD);
        const D3D12_RESOURCE_DESC constant_desc =
            buffer_desc(sizeof(constants));
        ComPtr<ID3D12Resource> constant_buffer;
        check_hr(
            device_->CreateCommittedResource(
                &upload_heap,
                D3D12_HEAP_FLAG_NONE,
                &constant_desc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(&constant_buffer)),
            "CreateCommittedResource(constants)");
        void* mapped_constants = nullptr;
        const D3D12_RANGE no_read{0u, 0u};
        check_hr(
            constant_buffer->Map(0u, &no_read, &mapped_constants),
            "Map(constants)");
        std::memcpy(mapped_constants, &constants, sizeof(constants));
        constant_buffer->Unmap(0u, nullptr);

        std::array<galaxy::render::RenderVertex, 2> vertices{};
        UINT vertex_count = 1u;
        if (raster_case == RasterCase::HorizontalLine) {
            vertices[0].position[0] = -0.5f;
            vertices[1].position[0] = 0.5f;
            vertex_count = 2u;
        } else if (raster_case == RasterCase::VerticalLine) {
            vertices[0].position[1] = -0.5f;
            vertices[1].position[1] = 0.5f;
            vertex_count = 2u;
        }
        vertices[0].position[2] = 0.5f;
        vertices[1].position[2] = 0.5f;
        const UINT vertex_bytes = static_cast<UINT>(
            static_cast<std::size_t>(vertex_count) * sizeof(vertices[0]));
        const D3D12_RESOURCE_DESC vertex_desc = buffer_desc(vertex_bytes);
        ComPtr<ID3D12Resource> vertex_buffer;
        check_hr(
            device_->CreateCommittedResource(
                &upload_heap,
                D3D12_HEAP_FLAG_NONE,
                &vertex_desc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(&vertex_buffer)),
            "CreateCommittedResource(vertices)");
        void* mapped_vertices = nullptr;
        check_hr(
            vertex_buffer->Map(0u, &no_read, &mapped_vertices),
            "Map(vertices)");
        std::memcpy(mapped_vertices, vertices.data(), vertex_bytes);
        vertex_buffer->Unmap(0u, nullptr);
        const D3D12_VERTEX_BUFFER_VIEW vertex_view{
            vertex_buffer->GetGPUVirtualAddress(),
            vertex_bytes,
            static_cast<UINT>(sizeof(galaxy::render::RenderVertex)),
        };

        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        UINT64 readback_size = 0u;
        device_->GetCopyableFootprints(
            &target_desc,
            0u,
            1u,
            0u,
            &footprint,
            nullptr,
            nullptr,
            &readback_size);
        const D3D12_HEAP_PROPERTIES readback_heap =
            heap_properties(D3D12_HEAP_TYPE_READBACK);
        const D3D12_RESOURCE_DESC readback_desc = buffer_desc(readback_size);
        ComPtr<ID3D12Resource> readback;
        check_hr(
            device_->CreateCommittedResource(
                &readback_heap,
                D3D12_HEAP_FLAG_NONE,
                &readback_desc,
                D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr,
                IID_PPV_ARGS(&readback)),
            "CreateCommittedResource(readback)");

        ComPtr<ID3D12CommandAllocator> allocator;
        check_hr(
            device_->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                IID_PPV_ARGS(&allocator)),
            "CreateCommandAllocator");
        ComPtr<ID3D12GraphicsCommandList> command_list;
        check_hr(
            device_->CreateCommandList(
                0u,
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                allocator.Get(),
                nullptr,
                IID_PPV_ARGS(&command_list)),
            "CreateCommandList");

        const float clear_color[4]{0.0f, 0.0f, 0.0f, 0.0f};
        command_list->ClearRenderTargetView(rtv, clear_color, 0u, nullptr);
        const D3D12_VIEWPORT viewport{
            0.0f,
            0.0f,
            static_cast<float>(dimension),
            static_cast<float>(dimension),
            0.0f,
            1.0f,
        };
        const D3D12_RECT scissor{
            0,
            0,
            static_cast<LONG>(dimension),
            static_cast<LONG>(dimension),
        };
        command_list->RSSetViewports(1u, &viewport);
        command_list->RSSetScissorRects(1u, &scissor);
        command_list->OMSetRenderTargets(1u, &rtv, FALSE, nullptr);
        command_list->SetGraphicsRootSignature(root_signature_.Get());
        const bool is_point = raster_case == RasterCase::Point;
        command_list->SetPipelineState(
            is_point ? point_pipeline_.Get() : line_pipeline_.Get());
        command_list->SetGraphicsRootConstantBufferView(
            0u, constant_buffer->GetGPUVirtualAddress());
        command_list->IASetVertexBuffers(0u, 1u, &vertex_view);
        command_list->IASetPrimitiveTopology(
            is_point
                ? D3D_PRIMITIVE_TOPOLOGY_POINTLIST
                : D3D_PRIMITIVE_TOPOLOGY_LINELIST);
        command_list->DrawInstanced(vertex_count, 1u, 0u, 0u);

        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = target.Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        barrier.Transition.Subresource =
            D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        command_list->ResourceBarrier(1u, &barrier);

        D3D12_TEXTURE_COPY_LOCATION source_location{};
        source_location.pResource = target.Get();
        source_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION destination_location{};
        destination_location.pResource = readback.Get();
        destination_location.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        destination_location.PlacedFootprint = footprint;
        command_list->CopyTextureRegion(
            &destination_location,
            0u,
            0u,
            0u,
            &source_location,
            nullptr);
        check_hr(command_list->Close(), "Close(command list)");
        ID3D12CommandList* command_lists[]{command_list.Get()};
        queue_->ExecuteCommandLists(1u, command_lists);
        wait_for_gpu();

        void* mapped_readback_raw = nullptr;
        const D3D12_RANGE read_range{
            0u, static_cast<SIZE_T>(readback_size)};
        check_hr(
            readback->Map(0u, &read_range, &mapped_readback_raw),
            "Map(readback)");
        const auto* mapped_readback =
            static_cast<const std::byte*>(mapped_readback_raw);
        RasterBounds bounds{};
        for (UINT y = 0u; y < dimension; ++y) {
            const auto* row = mapped_readback +
                static_cast<std::size_t>(y) * footprint.Footprint.RowPitch;
            for (UINT x = 0u; x < dimension; ++x) {
                const auto alpha = std::to_integer<std::uint8_t>(
                    row[static_cast<std::size_t>(x) * 4u + 3u]);
                if (alpha == 0u) {
                    continue;
                }
                ++bounds.pixel_count;
                bounds.min_x = (std::min)(bounds.min_x, x);
                bounds.min_y = (std::min)(bounds.min_y, y);
                bounds.max_x = (std::max)(bounds.max_x, x);
                bounds.max_y = (std::max)(bounds.max_y, y);
            }
        }
        if (bounds.pixel_count != 0u) {
            const auto channel = [mapped_readback, &footprint](
                                     UINT x,
                                     UINT y,
                                     std::size_t component) {
                const auto* row = mapped_readback +
                    static_cast<std::size_t>(y) *
                        footprint.Footprint.RowPitch;
                return std::to_integer<std::uint8_t>(
                    row[static_cast<std::size_t>(x) * 4u + component]);
            };
            for (UINT x = bounds.min_x; x <= bounds.max_x; ++x) {
                bounds.top_red += channel(x, bounds.min_y, 0u);
                bounds.bottom_red += channel(x, bounds.max_y, 0u);
                bounds.top_green += channel(x, bounds.min_y, 1u);
                bounds.bottom_green += channel(x, bounds.max_y, 1u);
            }
            for (UINT y = bounds.min_y; y <= bounds.max_y; ++y) {
                bounds.left_red += channel(bounds.min_x, y, 0u);
                bounds.right_red += channel(bounds.max_x, y, 0u);
            }
        }
        const D3D12_RANGE no_write{0u, 0u};
        readback->Unmap(0u, &no_write);
        return bounds;
    }

private:
    void create_root_signature() {
        check_hr(
            galaxy::render::create_gx_root_signature(
                device_.Get(), root_signature_.ReleaseAndGetAddressOf()),
            "create_gx_root_signature");
    }

    void create_pipelines() {
        // The test's stages (line_point_params_tests.hlsl, DXIL compiled at build) beside the build's native ones.
        const auto bytes_blob = [](const unsigned char* data, std::size_t size) {
            ComPtr<ID3DBlob> b;
            check_hr(D3DCreateBlob(size, &b), "D3DCreateBlob");
            std::memcpy(b->GetBufferPointer(), data, size);
            return b;
        };
        const ComPtr<ID3DBlob> vertex_shader = bytes_blob(kLinePointTestVs, sizeof(kLinePointTestVs));
        const ComPtr<ID3DBlob> pixel_shader = bytes_blob(kLinePointTestPs, sizeof(kLinePointTestPs));

        // The build's native geometry stages (without motion vectors), as the pipeline cache pairs them.
        const auto blob = [](const galaxy::render::NativeShaderBytes& bytes) {
            ComPtr<ID3DBlob> b;
            if (bytes.size == 0u || FAILED(D3DCreateBlob(bytes.size, &b))) {
                throw std::runtime_error("the build has no native line/point stage");
            }
            std::memcpy(b->GetBufferPointer(), bytes.data, bytes.size);
            return b;
        };
        line_geometry_shader_ = blob(galaxy::render::native_line_point_bytes(false, false));
        point_geometry_shader_ = blob(galaxy::render::native_line_point_bytes(false, true));

        galaxy::render::RenderStateKey line_state{};
        line_state.blend_bits = (1u << 25u) | (1u << 26u);
        line_state.cull = static_cast<std::uint8_t>(galaxy::render::CullMode::Back);
        line_state.primitive_topology = static_cast<std::uint8_t>(
            D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE);
        const D3D12_GRAPHICS_PIPELINE_STATE_DESC line_desc =
            galaxy::render::make_pso_desc(
                root_signature_.Get(),
                line_state,
                vertex_shader.Get(),
                pixel_shader.Get(),
                line_geometry_shader_.Get(),
                point_geometry_shader_.Get());
        descriptor_selection_valid_ =
            line_desc.GS.pShaderBytecode ==
                line_geometry_shader_->GetBufferPointer() &&
            line_desc.RasterizerState.CullMode == D3D12_CULL_MODE_NONE;
        check_hr(
            device_->CreateGraphicsPipelineState(
                &line_desc, IID_PPV_ARGS(&line_pipeline_)),
            "CreateGraphicsPipelineState(line)");

        galaxy::render::RenderStateKey point_state = line_state;
        point_state.primitive_topology = static_cast<std::uint8_t>(
            D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT);
        const D3D12_GRAPHICS_PIPELINE_STATE_DESC point_desc =
            galaxy::render::make_pso_desc(
                root_signature_.Get(),
                point_state,
                vertex_shader.Get(),
                pixel_shader.Get(),
                line_geometry_shader_.Get(),
                point_geometry_shader_.Get());
        descriptor_selection_valid_ = descriptor_selection_valid_ &&
            point_desc.GS.pShaderBytecode ==
                point_geometry_shader_->GetBufferPointer() &&
            point_desc.RasterizerState.CullMode == D3D12_CULL_MODE_NONE;
        check_hr(
            device_->CreateGraphicsPipelineState(
                &point_desc, IID_PPV_ARGS(&point_pipeline_)),
            "CreateGraphicsPipelineState(point)");

        galaxy::render::RenderStateKey triangle_state = line_state;
        triangle_state.primitive_topology = static_cast<std::uint8_t>(
            D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
        const D3D12_GRAPHICS_PIPELINE_STATE_DESC triangle_desc =
            galaxy::render::make_pso_desc(
                root_signature_.Get(),
                triangle_state,
                vertex_shader.Get(),
                pixel_shader.Get(),
                line_geometry_shader_.Get(),
                point_geometry_shader_.Get());
        descriptor_selection_valid_ = descriptor_selection_valid_ &&
            triangle_desc.GS.pShaderBytecode == nullptr;

        bool line_missing_hard_fails = false;
        try {
            (void)galaxy::render::make_pso_desc(
                root_signature_.Get(),
                line_state,
                vertex_shader.Get(),
                pixel_shader.Get(),
                nullptr,
                point_geometry_shader_.Get());
        } catch (const std::invalid_argument&) {
            line_missing_hard_fails = true;
        }
        bool point_missing_hard_fails = false;
        try {
            (void)galaxy::render::make_pso_desc(
                root_signature_.Get(),
                point_state,
                vertex_shader.Get(),
                pixel_shader.Get(),
                line_geometry_shader_.Get(),
                nullptr);
        } catch (const std::invalid_argument&) {
            point_missing_hard_fails = true;
        }
        missing_geometry_shader_hard_fails_ =
            line_missing_hard_fails && point_missing_hard_fails;
    }

    void wait_for_gpu() {
        const UINT64 value = ++fence_value_;
        check_hr(queue_->Signal(fence_.Get(), value), "Signal");
        if (fence_->GetCompletedValue() >= value) {
            return;
        }
        check_hr(
            fence_->SetEventOnCompletion(value, fence_event_),
            "SetEventOnCompletion");
        const DWORD wait_result = WaitForSingleObject(fence_event_, 30000u);
        if (wait_result != WAIT_OBJECT_0) {
            throw std::runtime_error("timed out waiting for WARP raster test");
        }
    }

    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12CommandQueue> queue_;
    ComPtr<ID3D12RootSignature> root_signature_;
    ComPtr<ID3DBlob> line_geometry_shader_;
    ComPtr<ID3DBlob> point_geometry_shader_;
    ComPtr<ID3D12PipelineState> line_pipeline_;
    ComPtr<ID3D12PipelineState> point_pipeline_;
    ComPtr<ID3D12Fence> fence_;
    HANDLE fence_event_ = nullptr;
    UINT64 fence_value_ = 0u;
    bool descriptor_selection_valid_ = false;
    bool missing_geometry_shader_hard_fails_ = false;
};

bool expect_bounds(
    const RasterBounds& actual,
    std::uint64_t pixel_count,
    UINT min_x,
    UINT min_y,
    UINT max_x,
    UINT max_y,
    const char* label) {
    if (actual.pixel_count == pixel_count &&
        actual.min_x == min_x && actual.min_y == min_y &&
        actual.max_x == max_x && actual.max_y == max_y) {
        return true;
    }
    std::fprintf(
        stderr,
        "FAILED: %s count=%llu bounds=(%u,%u)-(%u,%u); "
        "expected count=%llu bounds=(%u,%u)-(%u,%u)\n",
        label,
        static_cast<unsigned long long>(actual.pixel_count),
        actual.min_x,
        actual.min_y,
        actual.max_x,
        actual.max_y,
        static_cast<unsigned long long>(pixel_count),
        min_x,
        min_y,
        max_x,
        max_y);
    return false;
}

}  // namespace

int main() {
    bool passed = true;
    try {
        const galaxy::render::LinePointRasterParams decoded =
            galaxy::render::make_line_point_raster_params(
                {1.0f / 6.0f, 255.0f / 6.0f, 8u, 1u, 0x81u, 0x88u}, 20.0f, 40.0f, 3u);
        passed &= expect_near(
            decoded.viewport_width_pixels,
            60.0f,
            "viewport width uses the frame scale");
        passed &= expect_near(
            decoded.viewport_height_pixels,
            120.0f,
            "viewport height uses the frame scale");
        passed &= expect_near(
            decoded.line_width_pixels,
            0.5f,
            "line width keeps sixth-of-a-pixel values");
        passed &= expect_near(
            decoded.point_size_pixels,
            127.5f,
            "point size keeps sixth-of-a-pixel values");
        passed &= expect(
            decoded.line_texcoord_mask == 0x81u &&
                decoded.point_texcoord_mask == 0x88u,
            "line/point texture-coordinate masks pass through independently");
        passed &= expect(
            decoded.line_texcoord_divisor == 8u &&
                decoded.point_texcoord_divisor == 1u,
            "line/point texture-offset selectors give their divisors");

        const galaxy::render::LinePointRasterParams fallback =
            galaxy::render::make_line_point_raster_params(
                {1.0f, 2.0f}, 0.0f, 0.0f, 0u);
        passed &= expect_near(
            fallback.viewport_width_pixels,
            640.0f,
            "unset viewport uses the 1x frame width");
        passed &= expect_near(
            fallback.viewport_height_pixels,
            456.0f,
            "unset viewport uses the 1x frame height");
        const galaxy::render::LinePointRasterParams maximum_scale =
            galaxy::render::make_line_point_raster_params(
                {1.0f, 2.0f}, 64.0f, 64.0f, 99u);
        passed &= expect_near(
            maximum_scale.line_width_pixels,
            static_cast<float>(galaxy::render::kMaxEfbScale),
            "line width scale clamps to the supported maximum");
        passed &= expect_near(
            maximum_scale.point_size_pixels,
            2.0f * static_cast<float>(galaxy::render::kMaxEfbScale),
            "point size scale clamps to the supported maximum");

        galaxy::render::NativeDrawState state;
        (void)state.consume_dirty();
        galaxy::render::state::Op line{};
        line.kind = galaxy::render::state::kLineWidth;
        line.f[0] = 1.0f;  // one pixel
        state.apply(line);
        passed &= expect(
            (state.consume_dirty() & galaxy::render::kStateDirtyVertexConstants) != 0u,
            "a line width refreshes line/point GPU constants");
        galaxy::render::state::Op viewport{};
        viewport.kind = galaxy::render::state::kViewport;
        viewport.f[0] = 32.0f;
        viewport.f[1] = -32.0f;
        state.apply(viewport);
        passed &= expect(
            (state.consume_dirty() & galaxy::render::kStateDirtyVertexConstants) != 0u,
            "a viewport's width and height refresh line/point GPU constants");

        WarpRasterHarness harness;
        passed &= expect(
            harness.production_descriptor_selection_valid(),
            "production PSO path selects line/point GS and disables polygon culling");
        passed &= expect(
            harness.missing_geometry_shader_hard_fails(),
            "production PSO path hard-fails a missing required geometry shader");

        for (unsigned scale = 1u;
             scale <= galaxy::render::kMaxEfbScale; ++scale) {
            const galaxy::render::LinePointRasterParams raster =
                galaxy::render::make_line_point_raster_params(
                    {2.0f, 2.0f},
                    64.0f,
                    64.0f,
                    scale,
                    64u,
                    64u);
            const UINT center = 32u * scale;
            const UINT half_width = scale;
            const UINT line_min = 16u * scale;
            const UINT line_max = 48u * scale - 1u;
            const UINT width_min = center - half_width;
            const UINT width_max = center + half_width - 1u;

            char label[96]{};
            std::snprintf(
                label,
                sizeof(label),
                "%ux point raster bounds",
                scale);
            passed &= expect_bounds(
                harness.run(RasterCase::Point, scale, raster),
                static_cast<std::uint64_t>(2u * scale) * (2u * scale),
                width_min,
                width_min,
                width_max,
                width_max,
                label);
            std::snprintf(
                label,
                sizeof(label),
                "%ux horizontal line raster bounds",
                scale);
            passed &= expect_bounds(
                harness.run(RasterCase::HorizontalLine, scale, raster),
                static_cast<std::uint64_t>(32u * scale) * (2u * scale),
                line_min,
                width_min,
                line_max,
                width_max,
                label);
            std::snprintf(
                label,
                sizeof(label),
                "%ux vertical line raster bounds",
                scale);
            passed &= expect_bounds(
                harness.run(RasterCase::VerticalLine, scale, raster),
                static_cast<std::uint64_t>(32u * scale) * (2u * scale),
                width_min,
                line_min,
                width_max,
                line_max,
                label);
        }

        const galaxy::render::LinePointRasterParams offset_raster =
            galaxy::render::make_line_point_raster_params(
                {2.0f, 2.0f, 2u, 2u, 1u, 1u},
                64.0f,
                64.0f,
                4u,
                64u,
                64u);
        const RasterBounds horizontal_offset = harness.run(
            RasterCase::HorizontalLine, 4u, offset_raster);
        passed &= expect(
            horizontal_offset.bottom_red > horizontal_offset.top_red,
            "horizontal native line applies S offset to the screen-bottom edge");
        const RasterBounds vertical_offset = harness.run(
            RasterCase::VerticalLine, 4u, offset_raster);
        passed &= expect(
            vertical_offset.right_red > vertical_offset.left_red,
            "vertical line applies the S offset to the screen-right edge");
        const RasterBounds point_offset = harness.run(
            RasterCase::Point, 4u, offset_raster);
        passed &= expect(
            point_offset.right_red > point_offset.left_red,
            "point applies the S offset toward the screen-right edge");
        passed &= expect(
            point_offset.bottom_green > point_offset.top_green,
            "point applies the T offset toward the screen-bottom edge");

        const galaxy::render::LinePointRasterParams zero_raster =
            galaxy::render::make_line_point_raster_params(
                {}, 64.0f, 64.0f, 4u, 64u, 64u);
        passed &= expect(
            harness.run(RasterCase::Point, 4u, zero_raster).pixel_count == 0u,
            "zero point size emits no fragments");
        passed &= expect(
            harness.run(
                RasterCase::HorizontalLine, 4u, zero_raster).pixel_count == 0u,
            "zero line width emits no fragments");
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: unexpected exception: %s\n", error.what());
        passed = false;
    }
    return passed ? 0 : 1;
}
