#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#pragma warning(push, 0)
#include <Windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#pragma warning(pop)

#include "galaxy/gx/efb_copy.h"
#include "galaxy/gx/renderer_d3d12.h"
#include "galaxy/gx/pipeline_cache.h"
#include "galaxy/gx/shader_gen.h"
#include "galaxy/gx/uber_constants.h"
#include "galaxy/gx/texture_cache.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Microsoft::WRL::ComPtr;

struct Pixel {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 0;

    bool operator==(const Pixel&) const = default;
};

#pragma warning(push)
#pragma warning(disable : 4324)
struct GxPixelCase {
    galaxy::gx::PixelShaderKey shader{};
    galaxy::gx::RenderStateKey state{};
    galaxy::gx::GxPsConstants constants{};
    Pixel texture{18u, 52u, 86u, 120u};
};
#pragma warning(pop)
struct GxPixelResult { Pixel color{}; float depth = 0.0f; };

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", message);
        return false;
    }
    return true;
}

bool expect_pixel(Pixel actual, Pixel expected, const char* message) {
    if (actual == expected) {
        return true;
    }
    std::fprintf(
        stderr,
        "FAILED: %s actual=(%u,%u,%u,%u) expected=(%u,%u,%u,%u)\n",
        message,
        static_cast<unsigned>(actual.r),
        static_cast<unsigned>(actual.g),
        static_cast<unsigned>(actual.b),
        static_cast<unsigned>(actual.a),
        static_cast<unsigned>(expected.r),
        static_cast<unsigned>(expected.g),
        static_cast<unsigned>(expected.b),
        static_cast<unsigned>(expected.a));
    return false;
}

galaxy::gx::EfbCopyParams make_filter_params(
    const std::array<std::uint32_t, 7>& taps) {
    galaxy::gx::EfbCopyParams params{};
    params.src_y = 1u;
    params.src_width = 1u;
    params.src_height = 1u;
    params.filter0_raw =
        (taps[0] & 63u) |
        ((taps[1] & 63u) << 6u) |
        ((taps[2] & 63u) << 12u) |
        ((taps[3] & 63u) << 18u);
    params.filter1_raw =
        (taps[4] & 63u) |
        ((taps[5] & 63u) << 6u) |
        ((taps[6] & 63u) << 12u);
    return params;
}

// Independent integer oracle derived from the GX copy pipeline's documented
// byte-domain behavior. Source rows are quantized before the 1/64 filter;
// sums shift instead of rounding, >=128 coefficient sums wrap to nine bits,
// and alpha is the unfiltered center sample.
Pixel oracle_convert(
    Pixel previous,
    Pixel current,
    Pixel next,
    const galaxy::gx::EfbCopyParams& params,
    bool intensity,
    std::uint8_t copy_format) {
    std::array<std::uint32_t, 3> coefficients{
        params.filter_upper(),
        params.filter_middle(),
        params.filter_lower(),
    };
    if (coefficients[0] == 0u && coefficients[1] == 0u &&
        coefficients[2] == 0u) {
        // Product startup policy while the filter registers are unprogrammed.
        coefficients[1] = 64u;
    }
    const std::uint32_t coefficient_sum =
        coefficients[0] + coefficients[1] + coefficients[2];
    const auto filtered = [&](std::uint8_t p, std::uint8_t c, std::uint8_t n) {
        std::uint32_t value =
            (static_cast<std::uint32_t>(p) * coefficients[0] +
             static_cast<std::uint32_t>(c) * coefficients[1] +
             static_cast<std::uint32_t>(n) * coefficients[2]) >> 6u;
        if (coefficient_sum >= 128u) {
            value &= 0x1ffu;
        }
        return static_cast<std::uint8_t>(std::min(value, 255u));
    };

    Pixel raw{
        filtered(previous.r, current.r, next.r),
        filtered(previous.g, current.g, next.g),
        filtered(previous.b, current.b, next.b),
        current.a,
    };

    static constexpr float kGamma[4] = {1.0f, 1.7f, 2.2f, 2.2f};
    const float gamma = kGamma[params.gamma & 3u];
    if (gamma != 1.0f) {
        const auto apply_gamma = [gamma](std::uint8_t value) {
            return static_cast<std::uint8_t>(std::lround(
                std::pow(static_cast<float>(value) / 255.0f, 1.0f / gamma) *
                255.0f));
        };
        raw.r = apply_gamma(raw.r);
        raw.g = apply_gamma(raw.g);
        raw.b = apply_gamma(raw.b);
    }

    if (intensity) {
        const int r = raw.r;
        const int g = raw.g;
        const int b = raw.b;
        const std::array<std::uint32_t, 3> numerators{
            static_cast<std::uint32_t>(66 * r + 129 * g + 25 * b + 4096),
            static_cast<std::uint32_t>(-38 * r - 74 * g + 112 * b + 32768),
            static_cast<std::uint32_t>(112 * r - 94 * g - 18 * b + 32768),
        };
        const auto round_div_256 = [](std::uint32_t value) {
            return static_cast<std::uint8_t>(
                (value >> 8u) + ((value >> 7u) & 1u));
        };
        raw.r = round_div_256(numerators[0]);
        raw.g = round_div_256(numerators[1]);
        raw.b = round_div_256(numerators[2]);
    }

    switch (copy_format) {
    case 0u: {
        const std::uint8_t i = static_cast<std::uint8_t>(
            (raw.r & 0xf0u) | (raw.r >> 4u));
        return Pixel{i, i, i, i};
    }
    case 1u:
    case 8u:
        return Pixel{raw.r, raw.r, raw.r, raw.r};
    case 2u: {
        const std::uint8_t i = static_cast<std::uint8_t>(
            (raw.r & 0xf0u) | (raw.r >> 4u));
        const std::uint8_t a = static_cast<std::uint8_t>(
            (raw.a & 0xf0u) | (raw.a >> 4u));
        return Pixel{i, i, i, a};
    }
    case 3u:
        return Pixel{raw.r, raw.r, raw.r, raw.a};
    case 4u:
        raw.r = static_cast<std::uint8_t>((raw.r & 0xf8u) | (raw.r >> 5u));
        raw.g = static_cast<std::uint8_t>((raw.g & 0xfcu) | (raw.g >> 6u));
        raw.b = static_cast<std::uint8_t>((raw.b & 0xf8u) | (raw.b >> 5u));
        raw.a = 255u;
        return raw;
    case 5u:
        if (raw.a > 224u) {
            raw.r = static_cast<std::uint8_t>((raw.r & 0xf8u) | (raw.r >> 5u));
            raw.g = static_cast<std::uint8_t>((raw.g & 0xf8u) | (raw.g >> 5u));
            raw.b = static_cast<std::uint8_t>((raw.b & 0xf8u) | (raw.b >> 5u));
            raw.a = 255u;
        } else {
            raw.r = static_cast<std::uint8_t>((raw.r & 0xf0u) | (raw.r >> 4u));
            raw.g = static_cast<std::uint8_t>((raw.g & 0xf0u) | (raw.g >> 4u));
            raw.b = static_cast<std::uint8_t>((raw.b & 0xf0u) | (raw.b >> 4u));
            raw.a = static_cast<std::uint8_t>(
                (raw.a & 0xe0u) | (raw.a >> 3u) | (raw.a >> 6u));
        }
        return raw;
    case 6u:
    case 255u:
        return raw;
    case 7u:
        return Pixel{raw.a, raw.a, raw.a, raw.a};
    case 9u:
        return Pixel{raw.g, raw.g, raw.g, raw.g};
    case 10u:
        return Pixel{raw.b, raw.b, raw.b, raw.b};
    case 11u:
        return Pixel{raw.r, raw.r, raw.r, raw.g};
    case 12u:
        return Pixel{raw.g, raw.g, raw.g, raw.b};
    case 15u:
        raw.a = 255u;
        return raw;
    default:
        throw std::runtime_error("oracle received an invalid EFB copy format");
    }
}

Pixel oracle_from_three_rows(
    const std::array<Pixel, 3>& rows,
    const galaxy::gx::EfbCopyParams& params,
    bool intensity,
    std::uint8_t copy_format) {
    const int minimum = params.clamp_top ? 1 : 0;
    const int maximum = params.clamp_bottom ? 1 : 2;
    return oracle_convert(
        rows[static_cast<std::size_t>(std::clamp(0, minimum, maximum))],
        rows[1],
        rows[static_cast<std::size_t>(std::clamp(2, minimum, maximum))],
        params,
        intensity,
        copy_format);
}

Pixel packed_depth(std::uint32_t z24) {
    z24 &= 0x00ffffffu;
    return Pixel{
        static_cast<std::uint8_t>((z24 >> 16u) & 0xffu),
        static_cast<std::uint8_t>((z24 >> 8u) & 0xffu),
        static_cast<std::uint8_t>(z24 & 0xffu),
        255u,
    };
}

void check_hr(HRESULT result, const char* operation) {
    if (FAILED(result)) {
        char message[160]{};
        std::snprintf(
            message,
            sizeof(message),
            "%s failed with HRESULT 0x%08X",
            operation,
            static_cast<unsigned>(result));
        throw std::runtime_error(message);
    }
}

ComPtr<ID3DBlob> compile_shader(
    std::string_view source,
    const char* target) {
    ComPtr<ID3DBlob> code;
    ComPtr<ID3DBlob> errors;
    const HRESULT result = D3DCompile(
        source.data(),
        source.size(),
        "efb_conversion_tests",
        nullptr,
        nullptr,
        "main",
        target,
        D3DCOMPILE_ENABLE_STRICTNESS |
            D3DCOMPILE_PACK_MATRIX_ROW_MAJOR |
            D3DCOMPILE_OPTIMIZATION_LEVEL3,
        0,
        &code,
        &errors);
    if (FAILED(result)) {
        const char* diagnostics = errors
            ? static_cast<const char*>(errors->GetBufferPointer())
            : "no compiler diagnostics";
        throw std::runtime_error(
            std::string("D3DCompile failed: ") + diagnostics);
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

D3D12_RESOURCE_DESC texture_desc(
    DXGI_FORMAT format,
    UINT width,
    UINT height,
    D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE) {
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1u;
    desc.MipLevels = 1u;
    desc.Format = format;
    desc.SampleDesc.Count = 1u;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = flags;
    return desc;
}

class WarpConversionHarness {
public:
    WarpConversionHarness() {
        ComPtr<IDXGIFactory4> factory;
        check_hr(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "CreateDXGIFactory1");
        ComPtr<IDXGIAdapter> adapter;
        check_hr(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)), "EnumWarpAdapter");
        check_hr(
            D3D12CreateDevice(
                adapter.Get(),
                D3D_FEATURE_LEVEL_11_0,
                IID_PPV_ARGS(&device_)),
            "D3D12CreateDevice(WARP)");

        D3D12_COMMAND_QUEUE_DESC queue_desc{};
        queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        check_hr(
            device_->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue_)),
            "CreateCommandQueue");

        D3D12_DESCRIPTOR_RANGE range{};
        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        range.NumDescriptors = 1u;
        range.BaseShaderRegister = 0u;
        range.OffsetInDescriptorsFromTableStart = 0u;
        D3D12_ROOT_PARAMETER root_parameters[2]{};
        root_parameters[0].ParameterType =
            D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        root_parameters[0].Constants.ShaderRegister = 0u;
        root_parameters[0].Constants.Num32BitValues = 16u;
        root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        root_parameters[1].ParameterType =
            D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        root_parameters[1].DescriptorTable.NumDescriptorRanges = 1u;
        root_parameters[1].DescriptorTable.pDescriptorRanges = &range;
        root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_STATIC_SAMPLER_DESC samplers[2]{};
        samplers[0].Filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        samplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        samplers[0].MaxLOD = D3D12_FLOAT32_MAX;
        samplers[0].ShaderRegister = 0u;
        samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        samplers[1] = samplers[0];
        samplers[1].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        samplers[1].ShaderRegister = 1u;

        D3D12_ROOT_SIGNATURE_DESC root_desc{};
        root_desc.NumParameters = 2u;
        root_desc.pParameters = root_parameters;
        root_desc.NumStaticSamplers = 2u;
        root_desc.pStaticSamplers = samplers;
        ComPtr<ID3DBlob> serialized;
        ComPtr<ID3DBlob> root_errors;
        check_hr(
            D3D12SerializeRootSignature(
                &root_desc,
                D3D_ROOT_SIGNATURE_VERSION_1,
                &serialized,
                &root_errors),
            "D3D12SerializeRootSignature");
        check_hr(
            device_->CreateRootSignature(
                0u,
                serialized->GetBufferPointer(),
                serialized->GetBufferSize(),
                IID_PPV_ARGS(&root_signature_)),
            "CreateRootSignature");

        static constexpr char kVertexShader[] = R"HLSL(
struct V { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
V main(uint id : SV_VertexID) {
    float2 uv = float2((id << 1) & 2, id & 2);
    V output;
    output.pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    output.uv = uv;
    return output;
}
)HLSL";
        const ComPtr<ID3DBlob> vs = compile_shader(kVertexShader, "vs_5_0");
        const ComPtr<ID3DBlob> ps = compile_shader(
            galaxy::gx::efb_conversion_shader_source(), "ps_5_0");

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline_desc{};
        pipeline_desc.pRootSignature = root_signature_.Get();
        pipeline_desc.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
        pipeline_desc.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
        pipeline_desc.BlendState.RenderTarget[0].RenderTargetWriteMask =
            D3D12_COLOR_WRITE_ENABLE_ALL;
        pipeline_desc.SampleMask = UINT_MAX;
        pipeline_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        pipeline_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        pipeline_desc.RasterizerState.DepthClipEnable = TRUE;
        pipeline_desc.DepthStencilState.DepthEnable = FALSE;
        pipeline_desc.DepthStencilState.DepthWriteMask =
            D3D12_DEPTH_WRITE_MASK_ZERO;
        pipeline_desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        pipeline_desc.DepthStencilState.StencilEnable = FALSE;
        pipeline_desc.PrimitiveTopologyType =
            D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pipeline_desc.NumRenderTargets = 1u;
        pipeline_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        pipeline_desc.SampleDesc.Count = 1u;
        check_hr(
            device_->CreateGraphicsPipelineState(
                &pipeline_desc, IID_PPV_ARGS(&pipeline_)),
            "CreateGraphicsPipelineState");
        check_hr(device_->CreateFence(0u, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)),
                 "CreateFence");
        fence_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (fence_event_ == nullptr) {
            throw std::runtime_error("CreateEventW failed");
        }
    }

    ~WarpConversionHarness() {
        if (fence_event_ != nullptr) {
            CloseHandle(fence_event_);
        }
    }

    GxPixelResult run_gx(const GxPixelCase& input) {
        const std::vector<std::uint8_t> bytes{
            input.texture.r, input.texture.g, input.texture.b, input.texture.a};
        GxPixelResult result;
        result.color = run_bytes(DXGI_FORMAT_R8G8B8A8_UNORM, bytes,
            1u, 1u, 1u, {}, &input, &result.depth).front();
        return result;
    }

    std::vector<Pixel> run_color(
        const std::array<Pixel, 3>& rows,
        unsigned scale,
        const galaxy::gx::EfbConversionShaderConstants& constants) {
        const UINT width = scale;
        const UINT height = 3u * scale;
        std::vector<std::uint8_t> bytes(
            static_cast<std::size_t>(width) * height * 4u);
        for (UINT y = 0u; y < height; ++y) {
            const Pixel pixel = rows[y / scale];
            for (UINT x = 0u; x < width; ++x) {
                const std::size_t offset =
                    (static_cast<std::size_t>(y) * width + x) * 4u;
                bytes[offset + 0u] = pixel.r;
                bytes[offset + 1u] = pixel.g;
                bytes[offset + 2u] = pixel.b;
                bytes[offset + 3u] = pixel.a;
            }
        }
        return run_bytes(
            DXGI_FORMAT_R8G8B8A8_UNORM,
            bytes,
            width,
            height,
            scale,
            constants);
    }

    std::vector<Pixel> run_depth(
        const std::array<std::uint32_t, 3>& z24_rows,
        unsigned scale,
        const galaxy::gx::EfbConversionShaderConstants& constants) {
        const UINT width = scale;
        const UINT height = 3u * scale;
        std::vector<std::uint8_t> bytes(
            static_cast<std::size_t>(width) * height * sizeof(float));
        for (UINT y = 0u; y < height; ++y) {
            const std::uint32_t z24 = z24_rows[y / scale] & 0x00ffffffu;
            const float depth =
                1.0f - static_cast<float>(z24) / 16777216.0f;
            for (UINT x = 0u; x < width; ++x) {
                const std::size_t offset =
                    (static_cast<std::size_t>(y) * width + x) * sizeof(float);
                std::memcpy(bytes.data() + offset, &depth, sizeof(depth));
            }
        }
        return run_bytes(
            DXGI_FORMAT_R32_FLOAT,
            bytes,
            width,
            height,
            scale,
            constants);
    }

    Pixel run_color_peek(
        const std::vector<Pixel>& pixels,
        unsigned width,
        unsigned height,
        const galaxy::gx::EfbPeekSamplingGeometry& sampling) {
        if (pixels.size() !=
            static_cast<std::size_t>(width) * height) {
            throw std::invalid_argument(
                "color-peek source size does not match dimensions");
        }
        std::vector<std::uint8_t> bytes(pixels.size() * 4u);
        for (std::size_t index = 0u; index < pixels.size(); ++index) {
            bytes[index * 4u + 0u] = pixels[index].r;
            bytes[index * 4u + 1u] = pixels[index].g;
            bytes[index * 4u + 2u] = pixels[index].b;
            bytes[index * 4u + 3u] = pixels[index].a;
        }
        const std::vector<Pixel> result = run_bytes(
            DXGI_FORMAT_R8G8B8A8_UNORM,
            bytes,
            width,
            height,
            1u,
            galaxy::gx::make_efb_peek_conversion_constants(
                sampling, galaxy::gx::EfbPeekKind::Color));
        if (result.size() != 1u) {
            throw std::runtime_error("color peek returned a non-1x1 result");
        }
        return result.front();
    }

    Pixel run_depth_peek(
        const std::vector<std::uint32_t>& z24_pixels,
        unsigned width,
        unsigned height,
        const galaxy::gx::EfbPeekSamplingGeometry& sampling) {
        if (z24_pixels.size() !=
            static_cast<std::size_t>(width) * height) {
            throw std::invalid_argument(
                "depth-peek source size does not match dimensions");
        }
        std::vector<std::uint8_t> bytes(
            z24_pixels.size() * sizeof(float));
        for (std::size_t index = 0u; index < z24_pixels.size(); ++index) {
            const std::uint32_t z24 =
                z24_pixels[index] & 0x00ffffffu;
            const float depth =
                1.0f - static_cast<float>(z24) / 16777216.0f;
            std::memcpy(
                bytes.data() + index * sizeof(float),
                &depth,
                sizeof(depth));
        }
        const std::vector<Pixel> result = run_bytes(
            DXGI_FORMAT_R32_FLOAT,
            bytes,
            width,
            height,
            1u,
            galaxy::gx::make_efb_peek_conversion_constants(
                sampling, galaxy::gx::EfbPeekKind::Depth));
        if (result.size() != 1u) {
            throw std::runtime_error("depth peek returned a non-1x1 result");
        }
        return result.front();
    }

    std::vector<Pixel> run_depth_field(
        const std::vector<std::uint32_t>& z24_pixels, unsigned width,
        unsigned height, unsigned scale, unsigned logical_size) {
        std::vector<std::uint8_t> bytes(z24_pixels.size() * sizeof(float));
        for (std::size_t i=0; i<z24_pixels.size(); ++i) {
            const float depth=1.0f-static_cast<float>(z24_pixels[i])/16777216.0f;
            std::memcpy(bytes.data()+i*sizeof(float), &depth, sizeof(depth));
        }
        auto constants=galaxy::gx::make_efb_peek_conversion_constants(
            galaxy::gx::compute_efb_peek_sampling_geometry(0,0,scale),
            galaxy::gx::EfbPeekKind::Depth);
        constants.values[15]=-1.0f;
        return run_bytes(DXGI_FORMAT_R32_FLOAT,bytes,width,height,logical_size,constants);
    }

    bool test_texture_cache_ranges() {
        using namespace galaxy::gx;
        bool passed = true;
        // Full Wii TMEM is embedded in the cache; Windows' default test stack
        // cannot hold its 1MiB bank plus the caller/harness frames.
        auto cache_owner = std::make_unique<TextureCache>();
        TextureCache& cache = *cache_owner;
        if (!cache.initialize(device_.Get())) throw std::runtime_error("initialize WARP texture cache");
        cache.begin_frame(0u, kFramesInFlight);
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> list;
        check_hr(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&allocator)), "Create texture cache allocator");
        check_hr(device_->CreateCommandList(0u, D3D12_COMMAND_LIST_TYPE_DIRECT,
            allocator.Get(), nullptr, IID_PPV_ARGS(&list)), "Create texture cache command list");
        cache.set_upload_list(list.Get());
        constexpr std::uint32_t address = 0x10000000u;
        std::array<std::byte, 2048> bytes{};
        galaxy::GuestMemoryRegionV1 region{address, static_cast<std::uint32_t>(bytes.size()), bytes.data()};
        galaxy::GuestMemoryV1 memory{};
        memory.region_count = 1u;
        memory.regions = &region;
        TexImage image{};
        image.guest_addr = address;
        image.width = image.height = 8u;
        image.format = TexFormat::RGB565;
        TexMode mode{};
        mode.min_filter = TexMinFilter::NearMipNear;
        mode.max_lod_x16 = 32u;
        const auto decoded = cache.get(image, mode, TlutRef{}, &memory);
        // Independent tiled footprint: 8x8=4*32, 4x4=32, 2x2=32.
        passed &= expect(decoded.guest_byte_size == 192u && decoded.mip_levels == 3u,
            "authored mip handle retains all three block-rounded source levels");

        for (auto format : {TexFormat::I4, TexFormat::I8, TexFormat::IA4, TexFormat::IA8,
                            TexFormat::RGB565, TexFormat::RGB5A3, TexFormat::RGBA8, TexFormat::CMPR}) {
            auto direct_image = image; direct_image.guest_addr = address + 768u;
            direct_image.format = format;
            const auto original = cache.get(direct_image, mode, TlutRef{}, &memory);
            for (auto palette_format : {TlutFormat::IA8, TlutFormat::RGB565, TlutFormat::RGB5A3})
                for (std::uint16_t slot : {std::uint16_t{0}, std::uint16_t{1}, std::uint16_t{1023}}) {
                const auto reused = cache.get(direct_image, mode, TlutRef{slot, palette_format}, &memory);
                passed &= expect(reused.resource == original.resource && reused.srv_index == original.srv_index &&
                    reused.guest_byte_size == original.guest_byte_size && reused.mip_levels == original.mip_levels,
                    "direct texture formats reuse the same resource and descriptor across irrelevant palette changes");
            }
        }
        auto indexed_image = image; indexed_image.guest_addr = address + 512u;
        indexed_image.format = TexFormat::C4;
        TexMode indexed_mode{}; indexed_mode.min_filter = TexMinFilter::Near;
        const auto indexed = cache.get(indexed_image, indexed_mode, TlutRef{0u, TlutFormat::IA8}, &memory);
        const auto other_palette_format = cache.get(indexed_image, indexed_mode, TlutRef{0u, TlutFormat::RGB565}, &memory);
        const auto other_palette_slot = cache.get(indexed_image, indexed_mode, TlutRef{1u, TlutFormat::IA8}, &memory);
        passed &= expect(indexed.srv_index != other_palette_format.srv_index &&
            indexed.srv_index != other_palette_slot.srv_index,
            "indexed textures retain palette format and slot dependencies even when palette bytes match");
        constexpr auto palette_source = address + 1536u;
        passed &= expect(!cache.load_tlut(palette_source >> 5u, 1u << 10u, &memory),
            "32 unchanged palette bytes report no binding invalidation");
        const auto after_identical_reload = cache.get(indexed_image, indexed_mode, TlutRef{0u, TlutFormat::IA8}, &memory);
        passed &= expect(after_identical_reload.resource == indexed.resource &&
            after_identical_reload.srv_index == indexed.srv_index,
            "identical palette reload retains its decoded texture and descriptor");
        bytes[1536] = std::byte{0xff};
        passed &= expect(cache.load_tlut(palette_source >> 5u, 1u << 10u, &memory),
            "changed palette bytes report required binding invalidation");
        const auto after_changed_reload = cache.get(indexed_image, indexed_mode, TlutRef{0u, TlutFormat::IA8}, &memory);
        const auto unaffected_slot = cache.get(indexed_image, indexed_mode, TlutRef{1u, TlutFormat::IA8}, &memory);
        passed &= expect(after_changed_reload.resource != indexed.resource &&
            unaffected_slot.resource == other_palette_slot.resource && unaffected_slot.srv_index == other_palette_slot.srv_index,
            "changed palette bytes retire overlapping resources while retaining an unaffected slot");

        const auto original_config = get_render_config();
        auto mip_config = original_config; mip_config.enhanced_mipmaps = false;
        set_render_config(mip_config);
        auto enhanced_image = image; enhanced_image.guest_addr = address + 1280u;
        enhanced_image.width = 64u; enhanced_image.height = 4u;
        TexMode enhanced_mode{};
        enhanced_mode.min_filter = TexMinFilter::Linear;
        enhanced_mode.mag_filter = TexMagFilter::Linear;
        const auto no_enhancement = cache.get(enhanced_image, enhanced_mode, TlutRef{}, &memory);
        mip_config.enhanced_mipmaps = true; set_render_config(mip_config);
        const auto enhancement = cache.get(enhanced_image, enhanced_mode, TlutRef{}, &memory);
        set_render_config(original_config);
        passed &= expect(no_enhancement.mip_levels == 1u && !no_enhancement.generated_mips &&
            enhancement.mip_levels == 7u && enhancement.generated_mips &&
            enhancement.resource != no_enhancement.resource && enhancement.guest_byte_size == 512u,
            "eligible native mip generation still reads live options and keeps its resource separate from the base texture");

        // Complete the actual recorded upload before retirement/shutdown. This
        // test owns its queue and waits for GPU completion, not just CPU submit.
        check_hr(list->Close(), "Close texture cache uploads");
        ID3D12CommandList* lists[]{list.Get()};
        queue_->ExecuteCommandLists(1u, lists);
        const auto value = ++fence_value_;
        check_hr(queue_->Signal(fence_.Get(), value), "Signal texture cache uploads");
        check_hr(fence_->SetEventOnCompletion(value, fence_event_), "Wait texture cache uploads");
        if (WaitForSingleObject(fence_event_, 30000u) != WAIT_OBJECT_0) {
            throw std::runtime_error("texture cache upload fence timeout");
        }
        passed &= expect(cache.invalidate_guest_range(address + 160u, 1u) == 1u,
            "write confined to a lower mip retires the decoded resource");

        const auto make_alias = [&] {
            D3D12_HEAP_PROPERTIES heap{};
            heap.Type = D3D12_HEAP_TYPE_DEFAULT;
            D3D12_RESOURCE_DESC desc{};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            desc.Width = desc.Height = 8u;
            desc.DepthOrArraySize = desc.MipLevels = 1u;
            desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.SampleDesc.Count = 1u;
            ComPtr<ID3D12Resource> texture;
            check_hr(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE,
                &desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr,
                IID_PPV_ARGS(&texture)), "Create texture cache alias");
            return texture;
        };
        EfbCopyParams copy{};
        copy.src_width = copy.src_height = 8u;
        copy.target_format = 6u;
        copy.dest_addr = address;
        copy.dest_stride = 512u;
        auto alias = make_alias();
        passed &= expect(cache.register_efb_copy(address, copy, alias), "register strided alias");
        passed &= expect(cache.invalidate_guest_range(address + 512u, 1u) == 1u,
            "write to strided second block-row invalidates its alias");
        passed &= expect(cache.register_efb_copy(address, copy, alias), "register alias again");
        copy.dest_stride = 128u;
        passed &= expect(cache.register_efb_copy(address, copy, alias),
            "same GPU resource with changed guest footprint requests outer cache refresh");
        passed &= expect(cache.invalidate_guest_range(address + 512u, 1u) == 0u,
            "updated packed footprint excludes prior stride gap");
        image.format = TexFormat::RGBA8;
        mode.min_filter = TexMinFilter::Near;
        passed &= expect(cache.get(image, mode, TlutRef{}, &memory).guest_byte_size == 256u,
            "exact alias handle publishes current packed footprint");
        bool rejected = false;
        try { (void)cache.register_efb_copy(address, copy, {}); }
        catch (const std::runtime_error&) { rejected = true; }
        passed &= expect(rejected, "null alias is rejected before retiring live entry");
        passed &= expect(cache.get(image, mode, TlutRef{}, &memory).resource == alias.Get(),
            "rejected alias leaves the previous resource owned and bindable");
        cache.set_upload_list(nullptr);
        cache.shutdown();
        return passed;
    }

private:
    std::vector<Pixel> run_bytes(
        DXGI_FORMAT source_format,
        const std::vector<std::uint8_t>& source_bytes,
        UINT source_width,
        UINT source_height,
        UINT dest_size,
        const galaxy::gx::EfbConversionShaderConstants& constants,
        const GxPixelCase* gx = nullptr, float* depth_result = nullptr) {
        const D3D12_HEAP_PROPERTIES default_heap =
            heap_properties(D3D12_HEAP_TYPE_DEFAULT);
        const D3D12_HEAP_PROPERTIES upload_heap =
            heap_properties(D3D12_HEAP_TYPE_UPLOAD);
        const D3D12_HEAP_PROPERTIES readback_heap =
            heap_properties(D3D12_HEAP_TYPE_READBACK);

        ComPtr<ID3D12RootSignature> gx_root;
        ComPtr<ID3D12PipelineState> gx_pipeline;
        ComPtr<ID3D12Resource> gx_constants;
        if (gx != nullptr) {
            check_hr(galaxy::gx::create_gx_root_signature(device_.Get(), &gx_root), "Create GX test root");
            static constexpr char kGxVs[] = R"HLSL(
struct PSIn { float4 pos : SV_Position; float4 col0 : COLOR0;
              float4 col1 : COLOR1; float3 uv[8] : TEXCOORD; };
PSIn main(uint id : SV_VertexID) {
    float2 uv = float2((id << 1) & 2, id & 2);
    PSIn p;
    p.pos = float4(uv * float2(2,-2) + float2(-1,1), 0.75, 1);
    p.col0 = p.col1 = float4(1,0,0,0.5);
    [unroll] for (uint i=0; i<8; ++i) p.uv[i] = float3(0.5,0.5,1);
    return p;
})HLSL";
            const auto vs = compile_shader(kGxVs, "vs_5_1");
            const auto ps = compile_shader(galaxy::gx::ShaderGenerator{}.generate_ps(gx->shader), "ps_5_1");
            auto desc = galaxy::gx::make_gx_pso_desc(gx_root.Get(), gx->state,
                vs.Get(), ps.Get(), nullptr, nullptr);
            // The fixture VS emits a full-screen triangle using SV_VertexID.
            desc.InputLayout = {};
            check_hr(device_->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&gx_pipeline)), "Create generated GX test PSO");
            const auto desc_constants = buffer_desc(sizeof(gx->constants));
            check_hr(device_->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE,
                &desc_constants, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                IID_PPV_ARGS(&gx_constants)), "Create GX constants");
            void* mapped = nullptr;
            const D3D12_RANGE no_read{0u, 0u};
            check_hr(gx_constants->Map(0u, &no_read, &mapped), "Map GX constants");
            std::memcpy(mapped, &gx->constants, sizeof(gx->constants));
            gx_constants->Unmap(0u, nullptr);
        }

        const D3D12_RESOURCE_DESC source_desc =
            texture_desc(source_format, source_width, source_height);
        ComPtr<ID3D12Resource> source;
        check_hr(
            device_->CreateCommittedResource(
                &default_heap,
                D3D12_HEAP_FLAG_NONE,
                &source_desc,
                D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr,
                IID_PPV_ARGS(&source)),
            "Create source texture");

        D3D12_PLACED_SUBRESOURCE_FOOTPRINT source_footprint{};
        UINT source_rows = 0u;
        UINT64 source_row_size = 0u;
        UINT64 source_upload_size = 0u;
        device_->GetCopyableFootprints(
            &source_desc,
            0u,
            1u,
            0u,
            &source_footprint,
            &source_rows,
            &source_row_size,
            &source_upload_size);
        static_cast<void>(source_row_size);
        if (source_rows != source_height) {
            throw std::runtime_error("unexpected source footprint row count");
        }
        ComPtr<ID3D12Resource> upload;
        const D3D12_RESOURCE_DESC upload_desc = buffer_desc(source_upload_size);
        check_hr(
            device_->CreateCommittedResource(
                &upload_heap,
                D3D12_HEAP_FLAG_NONE,
                &upload_desc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(&upload)),
            "Create source upload buffer");
        void* upload_mapping = nullptr;
        const D3D12_RANGE no_read{0u, 0u};
        check_hr(upload->Map(0u, &no_read, &upload_mapping), "Map source upload");
        const std::size_t tight_row_bytes =
            static_cast<std::size_t>(source_width) * 4u;
        if (source_bytes.size() != tight_row_bytes * source_height) {
            throw std::runtime_error("source byte count does not match dimensions");
        }
        auto* upload_base = static_cast<std::uint8_t*>(upload_mapping) +
            source_footprint.Offset;
        for (UINT y = 0u; y < source_height; ++y) {
            std::memcpy(
                upload_base +
                    static_cast<std::size_t>(y) *
                        source_footprint.Footprint.RowPitch,
                source_bytes.data() + static_cast<std::size_t>(y) * tight_row_bytes,
                tight_row_bytes);
        }
        const D3D12_RANGE upload_written{0u, static_cast<SIZE_T>(source_upload_size)};
        upload->Unmap(0u, &upload_written);

        const D3D12_RESOURCE_DESC dest_desc = texture_desc(
            DXGI_FORMAT_R8G8B8A8_UNORM,
            dest_size,
            dest_size,
            D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
        ComPtr<ID3D12Resource> destination;
        check_hr(
            device_->CreateCommittedResource(
                &default_heap,
                D3D12_HEAP_FLAG_NONE,
                &dest_desc,
                D3D12_RESOURCE_STATE_RENDER_TARGET,
                nullptr,
                IID_PPV_ARGS(&destination)),
            "Create conversion destination");

        D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc{};
        rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtv_heap_desc.NumDescriptors = 1u;
        ComPtr<ID3D12DescriptorHeap> rtv_heap;
        check_hr(
            device_->CreateDescriptorHeap(
                &rtv_heap_desc, IID_PPV_ARGS(&rtv_heap)),
            "Create RTV heap");
        const D3D12_CPU_DESCRIPTOR_HANDLE rtv =
            rtv_heap->GetCPUDescriptorHandleForHeapStart();
        device_->CreateRenderTargetView(destination.Get(), nullptr, rtv);

        ComPtr<ID3D12Resource> depth, depth_readback;
        ComPtr<ID3D12DescriptorHeap> dsv_heap;
        D3D12_CPU_DESCRIPTOR_HANDLE dsv{};
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT depth_footprint{};
        if (gx != nullptr) {
            const auto depth_desc = texture_desc(galaxy::gx::kEfbDepthResourceFormat,
                1u, 1u, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
            check_hr(device_->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE,
                &depth_desc, D3D12_RESOURCE_STATE_DEPTH_WRITE, nullptr, IID_PPV_ARGS(&depth)), "Create GX depth");
            D3D12_DESCRIPTOR_HEAP_DESC heap{};
            heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
            heap.NumDescriptors = 1u;
            check_hr(device_->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&dsv_heap)), "Create GX DSV heap");
            dsv = dsv_heap->GetCPUDescriptorHandleForHeapStart();
            D3D12_DEPTH_STENCIL_VIEW_DESC view{};
            view.Format = galaxy::gx::kEfbDepthFormat;
            view.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
            device_->CreateDepthStencilView(depth.Get(), &view, dsv);
            UINT64 bytes = 0u;
            device_->GetCopyableFootprints(&depth_desc, 0u, 1u, 0u,
                &depth_footprint, nullptr, nullptr, &bytes);
            const auto desc_readback = buffer_desc(bytes);
            check_hr(device_->CreateCommittedResource(&readback_heap, D3D12_HEAP_FLAG_NONE,
                &desc_readback, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                IID_PPV_ARGS(&depth_readback)), "Create GX depth readback");
        }

        D3D12_DESCRIPTOR_HEAP_DESC srv_heap_desc{};
        srv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srv_heap_desc.NumDescriptors = gx != nullptr ? 8u : 1u;
        srv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        ComPtr<ID3D12DescriptorHeap> srv_heap;
        check_hr(
            device_->CreateDescriptorHeap(
                &srv_heap_desc, IID_PPV_ARGS(&srv_heap)),
            "Create SRV heap");
        D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc{};
        srv_desc.Format = source_format;
        srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv_desc.Texture2D.MipLevels = 1u;
        device_->CreateShaderResourceView(
            source.Get(),
            &srv_desc,
            srv_heap->GetCPUDescriptorHandleForHeapStart());
        ComPtr<ID3D12DescriptorHeap> sampler_heap;
        if (gx != nullptr) {
            auto srv = srv_heap->GetCPUDescriptorHandleForHeapStart();
            const UINT stride = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            for (unsigned i = 1u; i < 8u; ++i) {
                srv.ptr += stride;
                device_->CreateShaderResourceView(source.Get(), &srv_desc, srv);
            }
            D3D12_DESCRIPTOR_HEAP_DESC heap{};
            heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
            heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            heap.NumDescriptors = 8u;
            check_hr(device_->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&sampler_heap)), "Create GX sampler heap");
            D3D12_SAMPLER_DESC sampler{};
            sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
            sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
            sampler.MaxLOD = D3D12_FLOAT32_MAX;
            auto handle = sampler_heap->GetCPUDescriptorHandleForHeapStart();
            const UINT sampler_stride = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
            for (unsigned i = 0u; i < 8u; ++i) {
                device_->CreateSampler(&sampler, handle);
                handle.ptr += sampler_stride;
            }
        }

        D3D12_PLACED_SUBRESOURCE_FOOTPRINT dest_footprint{};
        UINT dest_rows = 0u;
        UINT64 dest_row_size = 0u;
        UINT64 readback_size = 0u;
        device_->GetCopyableFootprints(
            &dest_desc,
            0u,
            1u,
            0u,
            &dest_footprint,
            &dest_rows,
            &dest_row_size,
            &readback_size);
        static_cast<void>(dest_row_size);
        if (dest_rows != dest_size) {
            throw std::runtime_error("unexpected destination footprint row count");
        }
        ComPtr<ID3D12Resource> readback;
        const D3D12_RESOURCE_DESC readback_desc = buffer_desc(readback_size);
        check_hr(
            device_->CreateCommittedResource(
                &readback_heap,
                D3D12_HEAP_FLAG_NONE,
                &readback_desc,
                D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr,
                IID_PPV_ARGS(&readback)),
            "Create destination readback");

        ComPtr<ID3D12CommandAllocator> allocator;
        check_hr(
            device_->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)),
            "Create command allocator");
        ComPtr<ID3D12GraphicsCommandList> command_list;
        check_hr(
            device_->CreateCommandList(
                0u,
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                allocator.Get(),
                nullptr,
                IID_PPV_ARGS(&command_list)),
            "Create command list");

        D3D12_TEXTURE_COPY_LOCATION upload_location{};
        upload_location.pResource = upload.Get();
        upload_location.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        upload_location.PlacedFootprint = source_footprint;
        D3D12_TEXTURE_COPY_LOCATION source_location{};
        source_location.pResource = source.Get();
        source_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        command_list->CopyTextureRegion(
            &source_location, 0u, 0u, 0u, &upload_location, nullptr);
        D3D12_RESOURCE_BARRIER source_barrier{};
        source_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        source_barrier.Transition.pResource = source.Get();
        source_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        source_barrier.Transition.StateAfter =
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        source_barrier.Transition.Subresource =
            D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        command_list->ResourceBarrier(1u, &source_barrier);

        const D3D12_VIEWPORT viewport{
            0.0f,
            0.0f,
            static_cast<float>(dest_size),
            static_cast<float>(dest_size),
            0.0f,
            1.0f,
        };
        const D3D12_RECT scissor{
            0,
            0,
            static_cast<LONG>(dest_size),
            static_cast<LONG>(dest_size),
        };
        command_list->RSSetViewports(1u, &viewport);
        command_list->RSSetScissorRects(1u, &scissor);
        command_list->OMSetRenderTargets(1u, &rtv, FALSE, gx != nullptr ? &dsv : nullptr);
        if (gx != nullptr) {
            const float background[4]{0.0f, 0.0f, 1.0f, 0.25f};
            command_list->ClearRenderTargetView(rtv, background, 0u, nullptr);
            command_list->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 0.25f, 0u, 0u, nullptr);
            command_list->SetGraphicsRootSignature(gx_root.Get());
            command_list->SetPipelineState(gx_pipeline.Get());
            ID3D12DescriptorHeap* heaps[]{srv_heap.Get(), sampler_heap.Get()};
            command_list->SetDescriptorHeaps(2u, heaps);
            command_list->SetGraphicsRootConstantBufferView(1u, gx_constants->GetGPUVirtualAddress());
            command_list->SetGraphicsRootDescriptorTable(2u, srv_heap->GetGPUDescriptorHandleForHeapStart());
            command_list->SetGraphicsRootDescriptorTable(3u, sampler_heap->GetGPUDescriptorHandleForHeapStart());
        } else {
            command_list->SetGraphicsRootSignature(root_signature_.Get());
            command_list->SetPipelineState(pipeline_.Get());
            ID3D12DescriptorHeap* descriptor_heaps[] = {srv_heap.Get()};
            command_list->SetDescriptorHeaps(1u, descriptor_heaps);
            command_list->SetGraphicsRoot32BitConstants(0u, 16u, constants.values.data(), 0u);
            command_list->SetGraphicsRootDescriptorTable(1u, srv_heap->GetGPUDescriptorHandleForHeapStart());
        }
        command_list->IASetPrimitiveTopology(
            D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        command_list->DrawInstanced(3u, 1u, 0u, 0u);

        D3D12_RESOURCE_BARRIER dest_barrier{};
        dest_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        dest_barrier.Transition.pResource = destination.Get();
        dest_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        dest_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        dest_barrier.Transition.Subresource =
            D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        command_list->ResourceBarrier(1u, &dest_barrier);
        D3D12_TEXTURE_COPY_LOCATION dest_texture_location{};
        dest_texture_location.pResource = destination.Get();
        dest_texture_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION readback_location{};
        readback_location.pResource = readback.Get();
        readback_location.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        readback_location.PlacedFootprint = dest_footprint;
        command_list->CopyTextureRegion(
            &readback_location,
            0u,
            0u,
            0u,
            &dest_texture_location,
            nullptr);
        if (gx != nullptr) {
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = depth.Get();
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            command_list->ResourceBarrier(1u, &barrier);
            D3D12_TEXTURE_COPY_LOCATION texture{}, buffer{};
            texture.pResource = depth.Get();
            texture.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            buffer.pResource = depth_readback.Get();
            buffer.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            buffer.PlacedFootprint = depth_footprint;
            command_list->CopyTextureRegion(&buffer, 0u, 0u, 0u, &texture, nullptr);
        }
        check_hr(command_list->Close(), "Close command list");
        ID3D12CommandList* lists[] = {command_list.Get()};
        queue_->ExecuteCommandLists(1u, lists);
        const UINT64 fence_value = ++fence_value_;
        check_hr(queue_->Signal(fence_.Get(), fence_value), "Signal WARP fence");
        if (fence_->GetCompletedValue() < fence_value) {
            check_hr(
                fence_->SetEventOnCompletion(fence_value, fence_event_),
                "SetEventOnCompletion");
            const DWORD wait_result = WaitForSingleObject(fence_event_, 30'000u);
            if (wait_result != WAIT_OBJECT_0) {
                throw std::runtime_error("timed out waiting for WARP conversion");
            }
        }

        void* mapped = nullptr;
        const D3D12_RANGE read_range{0u, static_cast<SIZE_T>(readback_size)};
        check_hr(readback->Map(0u, &read_range, &mapped), "Map readback");
        const auto* base = static_cast<const std::uint8_t*>(mapped) +
            dest_footprint.Offset;
        std::vector<Pixel> pixels(
            static_cast<std::size_t>(dest_size) * dest_size);
        for (UINT y = 0u; y < dest_size; ++y) {
            const auto* row = base +
                static_cast<std::size_t>(y) * dest_footprint.Footprint.RowPitch;
            for (UINT x = 0u; x < dest_size; ++x) {
                const std::size_t source_offset = static_cast<std::size_t>(x) * 4u;
                pixels[static_cast<std::size_t>(y) * dest_size + x] = Pixel{
                    row[source_offset + 0u],
                    row[source_offset + 1u],
                    row[source_offset + 2u],
                    row[source_offset + 3u],
                };
            }
        }
        const D3D12_RANGE no_write{0u, 0u};
        readback->Unmap(0u, &no_write);
        if (depth_result != nullptr) {
            void* depth_mapping = nullptr;
            const D3D12_RANGE range{0u, 4u};
            check_hr(depth_readback->Map(0u, &range, &depth_mapping), "Map GX depth readback");
            std::uint32_t encoded = 0u;
            std::memcpy(&encoded, static_cast<const std::uint8_t*>(depth_mapping) + depth_footprint.Offset, sizeof(encoded));
            *depth_result = static_cast<float>(encoded & 0xffffffu) / 16777215.0f;
            depth_readback->Unmap(0u, &no_write);
        }
        return pixels;
    }

    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12CommandQueue> queue_;
    ComPtr<ID3D12RootSignature> root_signature_;
    ComPtr<ID3D12PipelineState> pipeline_;
    ComPtr<ID3D12Fence> fence_;
    HANDLE fence_event_ = nullptr;
    UINT64 fence_value_ = 0u;
};

GxPixelCase gx_pixel_fixture() {
    GxPixelCase input;
    input.shader.num_tev_stages = 1u;
    input.shader.num_texgens = 1u;
    input.shader.num_chans = 1u;
    input.shader.alpha_comp0 = input.shader.alpha_comp1 = 7u;
    input.shader.flags = 1u; // Late testing unless a case explicitly selects early.
    input.shader.stages[0].color_env = 10u | (15u << 4u) | (15u << 8u) |
        (15u << 12u) | (1u << 19u); // D = raster, A/B/C = zero.
    input.shader.stages[0].alpha_env = (5u << 4u) | (7u << 7u) |
        (7u << 10u) | (7u << 13u) | (1u << 19u);
    input.shader.stages[0].order = 1u << 6u;
    input.shader.stages[0].ksel = (1u << 12u) | (2u << 14u) | (3u << 16u) |
        (1u << 21u) | (2u << 23u) | (3u << 25u);
    input.state.primitive_topology = 3u;
    input.state.blend_bits = (1u << 25u) | (1u << 26u);
    input.state.zmode_bits = 31u; // Enable, ALWAYS, update.
    for (auto& dims : input.constants.tex_dims) std::fill(std::begin(dims), std::end(dims), 1.0f);
    return input;
}

bool test_gx_pixel_readbacks() {
    bool passed = true;
    try {
        WarpConversionHarness harness;
        auto input = gx_pixel_fixture();
        input.shader.alpha_comp0 = 4u; // Greater than 3/4 fails for alpha=1/2.
        input.constants.alpha_refs[0] = 0.75f;
        for (bool early : {true, false}) {
            input.shader.flags = early ? 0u : 1u;
            const auto output = harness.run_gx(input);
            passed &= expect_pixel(output.color, {0u, 0u, 255u, 64u},
                "alpha-discard leaves the cleared color untouched");
            passed &= expect(std::abs(output.depth - (early ? 0.75f : 0.25f)) < 2.0f / 16777216.0f,
                "early alpha failure writes geometric depth while late failure preserves depth");
        }

        // All formats use bytes from the unmodified, last enabled texture.
        // The raw source has unequal channels so swizzle/format mistakes differ.
        constexpr std::uint32_t depths[]{120u, 30738u, 0x123456u};
        for (unsigned format = 0u; format < 3u; ++format) {
            for (bool disabled_tail : {false, true}) {
                input = gx_pixel_fixture();
                input.shader.flags = static_cast<std::uint8_t>(1u | 2u | (2u << 3u) | (format << 5u));
                // Deliberately reverse TEV's texture swap independently of Z.
                input.shader.stages[0].ksel = (input.shader.stages[0].ksel & ((1u << 18u) - 1u)) |
                    (3u << 19u) | (2u << 21u) | (1u << 23u);
                if (disabled_tail) {
                    input.shader.num_tev_stages = 2u;
                    input.shader.stages[1] = input.shader.stages[0];
                    input.shader.stages[1].order = 0u;
                }
                const auto output = harness.run_gx(input);
                const float expected = 1.0f - static_cast<float>(depths[format]) / 16777216.0f;
                passed &= expect(std::abs(output.depth - expected) < 2.0f / 16777216.0f,
                    "late Z8/Z16/Z24 depth preserves raw sample across swaps and disabled tail");
            }
        }
        input = gx_pixel_fixture();
        input.shader.flags = 1u | 2u | (2u << 3u); // Z8 replace.
        input.constants.ztex_params[0] = static_cast<float>(0xfffff0u) / 16777216.0f;
        passed &= expect(std::abs(harness.run_gx(input).depth -
            (1.0f - 104.0f / 16777216.0f)) < 2.0f / 16777216.0f,
            "Z texture plus bias wraps modulo 24 bits instead of saturating");
        input.shader.flags = 1u | 2u | (1u << 3u); // Z8 add to geometric z=0x400000.
        passed &= expect(std::abs(harness.run_gx(input).depth -
            (1.0f - static_cast<float>(0x400068u) / 16777216.0f)) < 2.0f / 16777216.0f,
            "additive Z texture includes geometric depth in the 24-bit wrapped sum");

        input = gx_pixel_fixture();
        input.texture = {128u, 0u, 0u, 32u};
        input.shader.flags = 2u | (2u << 3u) | (2u << 5u); // Early Z24 replace.
        input.shader.fog_type = 2u;
        input.constants.fog_params[0] = 1.0f;
        input.constants.fog_params[5] = 1.0f; // Green fog.
        input.constants.fog_params[7] = 1.0f; // Orthographic.
        const auto fog = harness.run_gx(input);
        // Each 255*128 contribution truncates separately in the integer
        // final sum's >>8: both red and green are 127, not float-rounded 128.
        passed &= expect_pixel(fog.color, {127u, 127u, 0u, 130u},
            "early Z-texture modifies integer fog color before physical output quantization");
        passed &= expect(std::abs(fog.depth - 0.75f) < 2.0f / 16777216.0f,
            "early Z-texture fog retains geometric depth in the EFB");

        for (bool overwrite : {false, true}) {
            input = gx_pixel_fixture();
            input.state.pixfmt = input.shader.output_flags = 1u;
            input.shader.flags = static_cast<std::uint8_t>(1u | 128u | (overwrite ? 4u : 0u));
            input.constants.alpha_refs[3] = 64.0f / 255.0f;
            input.state.blend_bits |= 1u | (4u << 2u) | (5u << 5u) | (1u << 27u) |
                (overwrite ? (1u << 28u) : 0u);
            const auto output = harness.run_gx(input);
            passed &= expect(std::abs(static_cast<int>(output.color.r) - 128) <= 1 &&
                std::abs(static_cast<int>(output.color.b) - 127) <= 1,
                "dual-source RGB blending consumes original TEV alpha in both overwrite states");
            passed &= expect(std::abs(static_cast<int>(output.color.a) - (overwrite ? 65 : 98)) <= 1,
                "constant EFB alpha overwrite is independent of dual-source routing");
        }
        input = gx_pixel_fixture();
        input.state.pixfmt = input.shader.output_flags = 1u;
        input.shader.flags = 1u | 4u; // Constant alpha without dual-source routing.
        input.constants.alpha_refs[3] = 64.0f / 255.0f;
        input.state.blend_bits |= 1u | (1u << 2u) | (1u << 28u); // ONE/ZERO RGB.
        const auto constant_only = harness.run_gx(input);
        passed &= expect_pixel(constant_only.color, {255u, 0u, 0u, 65u},
            "constant alpha overwrite works independently when RGB needs no SRC1 alpha");
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAILED: WARP GX pixel semantics: %s\n", e.what());
        passed = false;
    }
    return passed;
}

bool test_clear_precision() {
    bool passed = true;
    const auto bytes = [](const galaxy::gx::EfbClearValues& v) {
        return Pixel{static_cast<std::uint8_t>(std::lround(v.color[0] * 255.0f)),
            static_cast<std::uint8_t>(std::lround(v.color[1] * 255.0f)),
            static_cast<std::uint8_t>(std::lround(v.color[2] * 255.0f)),
            static_cast<std::uint8_t>(std::lround(v.color[3] * 255.0f))};
    };
    passed &= expect_pixel(bytes(galaxy::gx::make_efb_clear_values(0x7f13u, 0x579bu, 0x123456u, 0u)),
        {19u, 87u, 155u, 255u}, "RGB8 clears preserve each full 8-bit color channel");
    passed &= expect_pixel(bytes(galaxy::gx::make_efb_clear_values(0x7f13u, 0x579bu, 0x123456u, 1u)),
        {16u, 85u, 154u, 125u}, "RGBA6 clears quantize only to the 6-bit lattice");
    const auto rgb565 = galaxy::gx::make_efb_clear_values(0x7f13u, 0x579bu, 0x123456u, 2u);
    passed &= expect_pixel(bytes(rgb565), {16u, 85u, 156u, 255u}, "RGB565 clears use 5/6/5 precision");
    passed &= expect(rgb565.reversed_depth == 1.0f - 0x123412u / 16777216.0f,
        "RGB565 Z16 clear expands the high sixteen depth bits to Z24");
    return passed;
}

galaxy::gx::EfbConversionShaderConstants constants_for_case(
    const galaxy::gx::EfbCopyParams& params,
    unsigned scale,
    bool is_depth,
    bool intensity,
    std::uint8_t format) {
    const galaxy::gx::EfbCopySamplingGeometry sampling =
        galaxy::gx::compute_efb_copy_sampling_geometry(
            params, scale, scale, scale);
    return galaxy::gx::make_efb_conversion_constants(
        params,
        is_depth,
        intensity,
        format == 2u || format == 3u,
        format,
        sampling.src_x,
        sampling.src_y,
        sampling.step_x,
        sampling.step_y,
        sampling.filter_row_offset,
        scale,
        3u);
}

bool expect_uniform_pixels(
    const std::vector<Pixel>& pixels,
    unsigned scale,
    Pixel expected,
    const char* label) {
    if (pixels.size() != static_cast<std::size_t>(scale) * scale) {
        std::fprintf(stderr, "FAILED: %s returned the wrong pixel count\n", label);
        return false;
    }
    for (const Pixel pixel : pixels) {
        if (!(pixel == expected)) {
            return expect_pixel(pixel, expected, label);
        }
    }
    return true;
}

bool test_scaled_xfb_filter() {
    bool passed = true;
    WarpConversionHarness harness;
    const std::array<Pixel, 3> rows{
        Pixel{0u, 0u, 0u, 1u}, Pixel{255u, 0u, 0u, 77u},
        Pixel{0u, 0u, 0u, 2u}};
    const auto params = make_filter_params({8u, 8u, 10u, 12u, 10u, 8u, 8u});
    for (const unsigned scale : {1u, 2u, 6u}) {
        const auto sampling = galaxy::gx::compute_xfb_sampling_geometry(
            params, scale, scale, scale);
        const auto texture = galaxy::gx::compute_efb_copy_sampling_geometry(
            params, scale, scale, scale);
        passed &= expect(sampling.src_y == static_cast<float>(scale) &&
            sampling.step_x == 1.0f && sampling.step_y == 1.0f &&
            sampling.filter_row_offset == 1.0f &&
            texture.filter_row_offset == static_cast<float>(scale),
            "display filter footprint leaves source mapping and texture copies intact");
        const auto constants = galaxy::gx::make_efb_conversion_constants(
            params, false, false, true, 15u, sampling.src_x, sampling.src_y,
            sampling.step_x, sampling.step_y, sampling.filter_row_offset, scale, 3u);
        const auto result = harness.run_color(rows, scale, constants);
        for (unsigned y = 0; y < scale; ++y) {
            const auto red = static_cast<std::uint8_t>(
                ((y == 0 ? 0u : 255u) * 16u + 255u * 32u +
                 (y + 1u == scale ? 0u : 255u) * 16u) >> 6u);
            for (unsigned x = 0; x < scale; ++x) {
                passed &= expect_pixel(result[y * scale + x], {red, 0u, 0u, 255u},
                    "scaled XFB filter affects adjacent edge rows, retaining brightness and opaque XFB alpha");
            }
        }
    }
    return passed;
}

bool test_cpu_oracle() {
    bool passed = true;
    const std::array<Pixel, 3> rows{
        Pixel{10u, 20u, 30u, 40u},
        Pixel{40u, 50u, 60u, 77u},
        Pixel{70u, 80u, 90u, 100u},
    };
    galaxy::gx::EfbCopyParams filter = make_filter_params(
        {1u, 2u, 4u, 8u, 16u, 17u, 16u});
    passed &= expect_pixel(
        oracle_from_three_rows(rows, filter, false, 6u),
        Pixel{54u, 64u, 74u, 77u},
        "integer oracle unbounded vertical filter");
    filter.clamp_top = true;
    passed &= expect_pixel(
        oracle_from_three_rows(rows, filter, false, 6u),
        Pixel{55u, 65u, 75u, 77u},
        "integer oracle top-only clamp");
    filter.clamp_top = false;
    filter.clamp_bottom = true;
    passed &= expect_pixel(
        oracle_from_three_rows(rows, filter, false, 6u),
        Pixel{38u, 48u, 58u, 77u},
        "integer oracle bottom-only clamp");
    filter.clamp_top = true;
    passed &= expect_pixel(
        oracle_from_three_rows(rows, filter, false, 6u),
        Pixel{40u, 50u, 60u, 77u},
        "integer oracle clamps both filter edges");

    const std::array<Pixel, 3> white_rows{
        Pixel{255u, 255u, 255u, 100u},
        Pixel{255u, 255u, 255u, 200u},
        Pixel{255u, 255u, 255u, 250u},
    };
    const galaxy::gx::EfbCopyParams overflow = make_filter_params(
        {63u, 63u, 63u, 63u, 63u, 63u, 63u});
    passed &= expect_pixel(
        oracle_from_three_rows(white_rows, overflow, false, 6u),
        Pixel{221u, 221u, 221u, 200u},
        "integer oracle applies nine-bit copy-filter overflow");

    const std::array<Pixel, 3> red_rows{
        Pixel{0u, 0u, 0u, 1u},
        Pixel{255u, 0u, 0u, 77u},
        Pixel{0u, 0u, 0u, 2u},
    };
    const galaxy::gx::EfbCopyParams identity = make_filter_params(
        {0u, 0u, 21u, 22u, 21u, 0u, 0u});
    passed &= expect_pixel(
        oracle_from_three_rows(red_rows, identity, true, 8u),
        Pixel{82u, 82u, 82u, 82u},
        "integer oracle intensity R8 selects Y");
    passed &= expect_pixel(
        oracle_from_three_rows(red_rows, identity, true, 9u),
        Pixel{90u, 90u, 90u, 90u},
        "integer oracle intensity G8 selects U");
    passed &= expect_pixel(
        oracle_from_three_rows(red_rows, identity, true, 10u),
        Pixel{240u, 240u, 240u, 240u},
        "integer oracle intensity B8 selects V");
    passed &= expect_pixel(
        oracle_from_three_rows(red_rows, identity, true, 11u),
        Pixel{82u, 82u, 82u, 90u},
        "integer oracle intensity RG8 selects Y/U");
    passed &= expect_pixel(
        oracle_from_three_rows(red_rows, identity, true, 12u),
        Pixel{90u, 90u, 90u, 240u},
        "integer oracle intensity GB8 selects U/V");

    const std::array<Pixel, 3> depth_rows{
        packed_depth(0x102030u),
        packed_depth(0x405060u),
        packed_depth(0x708090u),
    };
    passed &= expect_pixel(
        oracle_from_three_rows(depth_rows, make_filter_params(
            {1u, 2u, 4u, 8u, 16u, 17u, 16u}), false, 6u),
        Pixel{86u, 102u, 118u, 255u},
        "integer oracle filters packed depth bytes");
    return passed;
}

bool test_warp_against_oracle() {
    bool passed = true;
    try {
        WarpConversionHarness harness;
        const std::array<Pixel, 3> rows{
            Pixel{10u, 20u, 30u, 40u},
            Pixel{40u, 50u, 60u, 77u},
            Pixel{70u, 80u, 90u, 100u},
        };
        const std::array<Pixel, 3> white_rows{
            Pixel{255u, 255u, 255u, 100u},
            Pixel{255u, 255u, 255u, 200u},
            Pixel{255u, 255u, 255u, 250u},
        };
        const std::array<Pixel, 3> red_rows{
            Pixel{0u, 0u, 0u, 1u},
            Pixel{255u, 0u, 0u, 77u},
            Pixel{0u, 0u, 0u, 2u},
        };
        const std::array<std::uint32_t, 3> z24_rows{
            0x102030u,
            0x405060u,
            0x708090u,
        };
        const std::array<Pixel, 3> packed_depth_rows{
            packed_depth(z24_rows[0]),
            packed_depth(z24_rows[1]),
            packed_depth(z24_rows[2]),
        };

        for (const unsigned scale : {1u, 2u, 3u, 4u, 5u, 6u}) {
            for (unsigned clamp_case = 0u; clamp_case < 4u; ++clamp_case) {
                galaxy::gx::EfbCopyParams params = make_filter_params(
                    {1u, 2u, 4u, 8u, 16u, 17u, 16u});
                params.clamp_top = (clamp_case & 1u) != 0u;
                params.clamp_bottom = (clamp_case & 2u) != 0u;
                const Pixel expected =
                    oracle_from_three_rows(rows, params, false, 6u);
                char label[96]{};
                std::snprintf(
                    label,
                    sizeof(label),
                    "%ux color filter clamp_top=%u clamp_bottom=%u",
                    scale,
                    params.clamp_top ? 1u : 0u,
                    params.clamp_bottom ? 1u : 0u);
                passed &= expect_uniform_pixels(
                    harness.run_color(
                        rows,
                        scale,
                        constants_for_case(params, scale, false, false, 6u)),
                    scale,
                    expected,
                    label);
            }

            const galaxy::gx::EfbCopyParams overflow = make_filter_params(
                {63u, 63u, 63u, 63u, 63u, 63u, 63u});
            char overflow_label[64]{};
            std::snprintf(
                overflow_label,
                sizeof(overflow_label),
                "%ux coefficient overflow",
                scale);
            passed &= expect_uniform_pixels(
                harness.run_color(
                    white_rows,
                    scale,
                    constants_for_case(overflow, scale, false, false, 6u)),
                scale,
                oracle_from_three_rows(white_rows, overflow, false, 6u),
                overflow_label);

            const galaxy::gx::EfbCopyParams identity = make_filter_params(
                {0u, 0u, 21u, 22u, 21u, 0u, 0u});
            constexpr std::array<std::uint8_t, 5> kIntensityFormats{
                8u, 9u, 10u, 11u, 12u};
            for (const std::uint8_t format : kIntensityFormats) {
                char intensity_label[64]{};
                std::snprintf(
                    intensity_label,
                    sizeof(intensity_label),
                    "%ux intensity format %u",
                    scale,
                    static_cast<unsigned>(format));
                passed &= expect_uniform_pixels(
                    harness.run_color(
                        red_rows,
                        scale,
                        constants_for_case(identity, scale, false, true, format)),
                    scale,
                    oracle_from_three_rows(red_rows, identity, true, format),
                    intensity_label);
            }

            const galaxy::gx::EfbCopyParams depth_filter = make_filter_params(
                {1u, 2u, 4u, 8u, 16u, 17u, 16u});
            char depth_label[64]{};
            std::snprintf(
                depth_label,
                sizeof(depth_label),
                "%ux packed depth vertical filter",
                scale);
            passed &= expect_uniform_pixels(
                harness.run_depth(
                    z24_rows,
                    scale,
                    constants_for_case(depth_filter, scale, true, false, 6u)),
                scale,
                oracle_from_three_rows(
                    packed_depth_rows, depth_filter, false, 6u),
                depth_label);
        }

        // GXPeekZ reads one native EFB pixel. At scaled internal resolutions
        // that maps to one deterministic physical texel, never a bilinear
        // blend. Four logical blocks exercise both edges and exact z24
        // endpoints while keeping non-selected subpixels adversarial.
        constexpr std::array<std::uint32_t, 4> kPeekDepths{
            0x000000u,
            0x123456u,
            0x654321u,
            0xffffffu,
        };
        for (const unsigned scale : {1u, 2u, 3u, 4u, 5u, 6u}) {
            const unsigned width = 2u * scale;
            const unsigned height = 2u * scale;
            std::vector<std::uint32_t> depth_grid(
                static_cast<std::size_t>(width) * height,
                0x345678u);
            for (unsigned logical_y = 0u; logical_y < 2u; ++logical_y) {
                for (unsigned logical_x = 0u; logical_x < 2u; ++logical_x) {
                    const galaxy::gx::EfbPeekSamplingGeometry sampling =
                        galaxy::gx::compute_efb_peek_sampling_geometry(
                            static_cast<std::uint16_t>(logical_x),
                            static_cast<std::uint16_t>(logical_y),
                            scale);
                    const std::size_t index =
                        static_cast<std::size_t>(sampling.depth_texel_y) *
                            width +
                        sampling.depth_texel_x;
                    depth_grid[index] =
                        kPeekDepths[logical_y * 2u + logical_x];
                }
            }

            for (unsigned logical_y = 0u; logical_y < 2u; ++logical_y) {
                for (unsigned logical_x = 0u; logical_x < 2u; ++logical_x) {
                    const galaxy::gx::EfbPeekSamplingGeometry sampling =
                        galaxy::gx::compute_efb_peek_sampling_geometry(
                            static_cast<std::uint16_t>(logical_x),
                            static_cast<std::uint16_t>(logical_y),
                            scale);
                    const std::uint32_t expected_z24 =
                        kPeekDepths[logical_y * 2u + logical_x];
                    char label[128]{};
                    std::snprintf(
                        label,
                        sizeof(label),
                        "%ux point depth peek (%u,%u) preserves exact z24",
                        scale,
                        logical_x,
                        logical_y);
                    passed &= expect_pixel(
                        harness.run_depth_peek(
                            depth_grid, width, height, sampling),
                        packed_depth(expected_z24),
                        label);
                }
            }

            const auto field=harness.run_depth_field(depth_grid,width,height,scale,2u);
            for (std::size_t i=0; i<kPeekDepths.size(); ++i) {
                passed &= expect_pixel(field.at(i),packed_depth(kPeekDepths[i]),
                    "whole pointer depth field preserves exact GXPeekZ pixel and z24");
            }

            // Color peeks intentionally retain their complete NxN box
            // reduction. The center and corner deltas cancel only when every
            // physical subpixel participates in the reduction.
            constexpr Pixel kColorMean{64u, 96u, 128u, 160u};
            std::vector<Pixel> color_block(
                static_cast<std::size_t>(scale) * scale,
                kColorMean);
            if (scale > 1u) {
                const galaxy::gx::EfbPeekSamplingGeometry sampling =
                    galaxy::gx::compute_efb_peek_sampling_geometry(
                        0u, 0u, scale);
                color_block[0] = Pixel{32u, 64u, 96u, 128u};
                const std::size_t center =
                    static_cast<std::size_t>(sampling.depth_texel_y) *
                        scale +
                    sampling.depth_texel_x;
                color_block[center] = Pixel{96u, 128u, 160u, 192u};
            }
            const galaxy::gx::EfbPeekSamplingGeometry color_sampling =
                galaxy::gx::compute_efb_peek_sampling_geometry(
                    0u, 0u, scale);
            char color_label[96]{};
            std::snprintf(
                color_label,
                sizeof(color_label),
                "%ux color peek retains full logical-pixel box reduction",
                scale);
            passed &= expect_pixel(
                harness.run_color_peek(
                    color_block, scale, scale, color_sampling),
                kColorMean,
                color_label);
        }
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: WARP EFB conversion suite: %s\n", error.what());
        passed = false;
    }
    return passed;
}

}  // namespace

int main() {
    bool passed = test_cpu_oracle();
    passed &= test_clear_precision();
    passed &= test_warp_against_oracle();
    passed &= test_gx_pixel_readbacks();
    try {
        WarpConversionHarness harness;
        passed &= harness.test_texture_cache_ranges();
        passed &= test_scaled_xfb_filter();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: WARP texture cache suite: %s\n", error.what());
        passed = false;
    }
    return passed ? 0 : 1;
}
