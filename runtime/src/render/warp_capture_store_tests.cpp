// Texture decode, EFB-copy conversion, texture cache and XFB tests: CPU oracles and D3D12 WARP readbacks.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#pragma warning(push, 0)
#include <Windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#pragma warning(pop)

#include "galaxy/render/capture.h"
#include "galaxy/render/renderer_d3d12.h"
#include "galaxy/render/pipeline_cache.h"
#include "galaxy/render/draw_constants.h"
#include "galaxy/render/texture_store.h"
#include "nebula_cooked_texture.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

// An image as these tests describe it before handing its record over.
struct TestImage {
    std::uint16_t width;
    std::uint16_t height;
    std::uint64_t address;
};

// Hands bytes to the texture cache as the game hands over an image (texture_image).
void hand_over(galaxy::render::TextureCache& cache, std::uint32_t address, const std::byte* bytes, std::size_t size) {
    cache.register_image(address, std::make_shared<const std::vector<std::byte>>(bytes, bytes + size));
}

using Microsoft::WRL::ComPtr;

struct Pixel {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 0;

    bool operator==(const Pixel&) const = default;
};


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

bool test_generated_mip_uniform_alpha() {
    using galaxy::render::RGBA8;
    std::vector<RGBA8> actual;
    const auto check = [&](const std::vector<RGBA8>& source,
                           std::uint32_t width, std::uint32_t height) {
        const auto dest_width = std::max(1u, width / 2u);
        const auto dest_height = std::max(1u, height / 2u);
        galaxy::render::detail::generate_rgba8_mip_level(
            source, width, height, actual, dest_width, dest_height);
        if (!expect(actual.size() == dest_width * dest_height,
                    "generated mip retains exact output extent")) return false;
        // Oracle: the general premultiplied-alpha rule (thin and empty footprints, integer rounding), with no
        // uniform-alpha shortcut.
        for (std::uint32_t y = 0u; y < dest_height; ++y) {
            for (std::uint32_t x = 0u; x < dest_width; ++x) {
                std::array<std::uint32_t, 4> sums{};
                std::uint32_t count = 0u;
                for (std::uint32_t sy = y * 2u; sy < y * 2u + 2u && sy < height; ++sy) {
                    for (std::uint32_t sx = x * 2u; sx < x * 2u + 2u && sx < width; ++sx) {
                        const auto pixel = source[static_cast<std::size_t>(sy) * width + sx];
                        sums[0] += pixel.r * pixel.a;
                        sums[1] += pixel.g * pixel.a;
                        sums[2] += pixel.b * pixel.a;
                        sums[3] += pixel.a;
                        ++count;
                    }
                }
                count = std::max(1u, count);
                const auto divisor = sums[3] != 0u ? sums[3] : count;
                const RGBA8 expected{
                    static_cast<std::uint8_t>((sums[0] + divisor / 2u) / divisor),
                    static_cast<std::uint8_t>((sums[1] + divisor / 2u) / divisor),
                    static_cast<std::uint8_t>((sums[2] + divisor / 2u) / divisor),
                    static_cast<std::uint8_t>((sums[3] + count / 2u) / count)};
                const auto pixel = actual[y * dest_width + x];
                if (!expect(pixel.r == expected.r && pixel.g == expected.g &&
                            pixel.b == expected.b && pixel.a == expected.a,
                            "generated mip bytes match original premultiplied-alpha oracle")) return false;
            }
        }
        return true;
    };
    std::vector<RGBA8> block(4u);
    // Every representable uniform alpha and RGB sum, including each rounding boundary.
    // Zero-alpha inputs keep zero RGB.
    for (std::uint32_t alpha = 0u; alpha < 256u; ++alpha) {
        for (std::uint32_t sum = 0u; sum <= 1020u; ++sum) {
            auto remaining = sum;
            for (auto& pixel : block) {
                const auto value = std::min(255u, remaining);
                remaining -= value;
                pixel = {static_cast<std::uint8_t>(value),
                         static_cast<std::uint8_t>(255u - value),
                         static_cast<std::uint8_t>(value), static_cast<std::uint8_t>(alpha)};
            }
            if (!check(block, 2u, 2u)) return false;
        }
    }
    constexpr std::array<std::uint32_t, 9> dimensions{0u, 1u, 2u, 3u, 4u, 5u, 7u, 8u, 17u};
    for (const auto width : dimensions) for (const auto height : dimensions) {
        std::vector<RGBA8> source(width * height);
        for (std::uint32_t pattern = 0u; pattern < 4u; ++pattern) {
            for (std::size_t i = 0u; i < source.size(); ++i) {
                source[i] = {static_cast<std::uint8_t>(i * 73u + 29u),
                             static_cast<std::uint8_t>(i * 19u + 251u),
                             static_cast<std::uint8_t>(i * 113u + 17u),
                             static_cast<std::uint8_t>(pattern == 0u ? 0u :
                                 pattern == 1u ? 255u : pattern == 2u ? (i & 1u) * 255u : i * 67u + 3u)};
            }
            if (!check(source, width, height)) return false;
        }
    }
    return true;
}

bool test_flatten_texel_checker() {
    using galaxy::render::RGBA8;
    const auto checker = [](std::uint32_t width, std::uint32_t height, RGBA8 even, RGBA8 odd) {
        std::vector<RGBA8> pixels(static_cast<std::size_t>(width) * height);
        for (std::uint32_t y = 0u; y < height; ++y) {
            for (std::uint32_t x = 0u; x < width; ++x) pixels[y * width + x] = ((x + y) & 1u) == 0u ? even : odd;
        }
        return pixels;
    };
    // The sky's DotNoise (4-bit intensity, 238/255) becomes one level of 247.
    auto dots = checker(128u, 128u, {238, 238, 238, 238}, {255, 255, 255, 255});
    if (!expect(galaxy::render::detail::flatten_texel_checker(dots, 128u, 128u), "a one-texel checker is a dither")) return false;
    for (const RGBA8& pixel : dots) {
        if (!expect(pixel.r == 247u && pixel.g == 247u && pixel.b == 247u && pixel.a == 247u,
                    "a flattened checker is its two values' rounded average")) return false;
    }
    // Content is never touched: a texel off the pattern, a uniform level, a two-texel checker, a thin strip.
    auto broken = checker(64u, 64u, {242, 238, 234, 255}, {247, 243, 239, 255});
    broken[64u * 30u + 17u] = {0, 0, 0, 255};
    const auto broken_before = broken;
    auto uniform = std::vector<RGBA8>(64u, RGBA8{10, 20, 30, 255});
    std::vector<RGBA8> squares(16u * 16u);
    for (std::uint32_t y = 0u; y < 16u; ++y) {
        for (std::uint32_t x = 0u; x < 16u; ++x) {
            squares[y * 16u + x] = (((x >> 1u) + (y >> 1u)) & 1u) == 0u ? RGBA8{0, 0, 0, 255} : RGBA8{255, 255, 255, 255};
        }
    }
    auto strip = checker(64u, 2u, {0, 0, 0, 255}, {255, 255, 255, 255});
    const bool untouched = !galaxy::render::detail::flatten_texel_checker(broken, 64u, 64u) &&
        !galaxy::render::detail::flatten_texel_checker(uniform, 8u, 8u) &&
        !galaxy::render::detail::flatten_texel_checker(squares, 16u, 16u) &&
        !galaxy::render::detail::flatten_texel_checker(strip, 64u, 2u);
    bool same = true;
    for (std::size_t i = 0u; i < broken.size(); ++i) {
        same &= broken[i].r == broken_before[i].r && broken[i].g == broken_before[i].g && broken[i].b == broken_before[i].b;
    }
    return expect(untouched && same, "only a whole-level one-texel checker of at least 4x4 is flattened");
}

bool test_remove_texel_checker() {
    using galaxy::render::RGBA8;
    // Art under a baked dither: a smooth ramp in red and green with a +-6 one-texel checker, alpha a ramp of its own.
    constexpr std::uint32_t kSize = 64u;
    std::vector<RGBA8> art(kSize * kSize);
    std::vector<RGBA8> dithered(kSize * kSize);
    for (std::uint32_t y = 0u; y < kSize; ++y) {
        for (std::uint32_t x = 0u; x < kSize; ++x) {
            const RGBA8 smooth{static_cast<std::uint8_t>(40u + 2u * x), static_cast<std::uint8_t>(60u + 2u * y), 120u,
                               static_cast<std::uint8_t>(3u * x)};
            const std::int32_t k = ((x + y) & 1u) == 0u ? 6 : -6;
            art[y * kSize + x] = smooth;
            dithered[y * kSize + x] = {static_cast<std::uint8_t>(smooth.r + k), static_cast<std::uint8_t>(smooth.g + k),
                                       static_cast<std::uint8_t>(smooth.b + k), smooth.a};
        }
    }
    if (!expect(galaxy::render::detail::remove_texel_checker(dithered, kSize, kSize), "a checker over content is a dither")) {
        return false;
    }
    for (std::uint32_t y = 1u; y + 1u < kSize; ++y) {
        for (std::uint32_t x = 1u; x + 1u < kSize; ++x) {
            const RGBA8 a = art[y * kSize + x];
            const RGBA8 b = dithered[y * kSize + x];
            if (!expect(std::abs(a.r - b.r) <= 1 && std::abs(a.g - b.g) <= 1 && std::abs(a.b - b.b) <= 1 && a.a == b.a,
                        "the checker goes, the art and alpha stay")) {
                return false;
            }
        }
    }
    // Content is never touched: the smooth art alone, noise, a two-texel checker.
    auto smooth = art;
    std::vector<RGBA8> noise(kSize * kSize);
    std::uint32_t seed = 12345u;
    for (RGBA8& p : noise) {
        seed = seed * 1664525u + 1013904223u;
        p = {static_cast<std::uint8_t>(seed >> 24u), static_cast<std::uint8_t>(seed >> 16u), static_cast<std::uint8_t>(seed >> 8u), 255u};
    }
    const auto noise_before = noise;
    std::vector<RGBA8> squares(kSize * kSize);
    for (std::uint32_t y = 0u; y < kSize; ++y) {
        for (std::uint32_t x = 0u; x < kSize; ++x) {
            squares[y * kSize + x] = (((x >> 1u) + (y >> 1u)) & 1u) == 0u ? RGBA8{0, 0, 0, 255} : RGBA8{255, 255, 255, 255};
        }
    }
    const bool untouched = !galaxy::render::detail::remove_texel_checker(smooth, kSize, kSize) &&
                           !galaxy::render::detail::remove_texel_checker(noise, kSize, kSize) &&
                           !galaxy::render::detail::remove_texel_checker(squares, kSize, kSize);
    bool same = true;
    for (std::size_t i = 0u; i < noise.size(); ++i) {
        same &= noise[i].r == noise_before[i].r && noise[i].g == noise_before[i].g && noise[i].b == noise_before[i].b;
    }
    return expect(untouched && same, "only texel-scale detail in one checker phase is removed");
}

galaxy::render::CaptureRequest make_filter_params(bool filtered) {
    galaxy::render::CaptureRequest params{};
    params.src_y = 1u;
    params.src_width = 1u;
    params.src_height = 1u;
    params.filtered = filtered;
    return params;
}

// The capture shader's arithmetic in double: the filter weights the rows, luminance replaces the colour, the target
// rounds once to the nearest byte; alpha is the line's own.
Pixel oracle_convert(
    Pixel previous,
    Pixel current,
    Pixel next,
    const galaxy::render::CaptureRequest& params,
    bool intensity,
    std::uint8_t copy_format) {
    const std::array<std::uint32_t, 3> coefficients{
        capture_filter_upper(params),
        capture_filter_middle(params),
        capture_filter_lower(params),
    };
    const auto filtered = [&](std::uint8_t p, std::uint8_t c, std::uint8_t n) {
        return (static_cast<double>(p) * coefficients[0] + static_cast<double>(c) * coefficients[1] +
                static_cast<double>(n) * coefficients[2]) / 64.0;
    };
    const auto byte = [](double value) {
        return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0, 255.0)));
    };
    const double r = filtered(previous.r, current.r, next.r);
    const double g = filtered(previous.g, current.g, next.g);
    const double b = filtered(previous.b, current.b, next.b);
    Pixel raw{byte(r), byte(g), byte(b), current.a};
    if (intensity) {
        const std::uint8_t y = byte((66.0 * r + 129.0 * g + 25.0 * b) / 256.0 + 16.0);
        raw.r = raw.g = raw.b = y;
    }

    switch (copy_format) {  // the picture layout (capture.h)
    case 3u:
        return Pixel{raw.r, raw.r, raw.r, raw.r};
    case 1u:
        return Pixel{raw.r, raw.r, raw.r, raw.a};
    case 0u:
    case 255u:
        return raw;
    case 2u:
        return Pixel{raw.a, raw.a, raw.a, raw.a};
    case 4u:
        return Pixel{raw.g, raw.g, raw.g, raw.g};
    case 5u:
        return Pixel{raw.b, raw.b, raw.b, raw.b};
    case 6u:
    case 7u:
        raw.a = 255u;
        return raw;
    default:
        throw std::runtime_error("oracle received an invalid picture layout");
    }
}

Pixel oracle_from_three_rows(
    const std::array<Pixel, 3>& rows,
    const galaxy::render::CaptureRequest& params,
    bool intensity,
    std::uint8_t copy_format) {
    // Rows 0-2 are logical rows 0-2 and row 1 is converted; every sample stays inside the copy rectangle.
    const int minimum = std::clamp(static_cast<int>(params.src_y), 0, 2);
    const int maximum = std::clamp(static_cast<int>(params.src_y) + static_cast<int>(params.src_height) - 1, 0, 2);
    return oracle_convert(
        rows[static_cast<std::size_t>(std::clamp(0, minimum, maximum))],
        rows[1],
        rows[static_cast<std::size_t>(std::clamp(2, minimum, maximum))],
        params,
        intensity,
        copy_format);
}

// A depth capture's texel: the game's depth (z24 / 2^24) in every colour channel, rounded to a byte; opaque.
Pixel depth_pixel(double z24) {
    const auto v = static_cast<std::uint8_t>(std::lround(std::clamp(z24 / 16777216.0 * 255.0, 0.0, 255.0)));
    return Pixel{v, v, v, 255u};
}

// The depth rows 0-2 through the filter inside the rectangle (row 1 converted), as a depth capture's texel.
Pixel oracle_depth(const std::array<std::uint32_t, 3>& z24_rows, const galaxy::render::CaptureRequest& params) {
    const int minimum = std::clamp(static_cast<int>(params.src_y), 0, 2);
    const int maximum = std::clamp(static_cast<int>(params.src_y) + static_cast<int>(params.src_height) - 1, 0, 2);
    const auto row = [&](int i) { return static_cast<double>(z24_rows[static_cast<std::size_t>(std::clamp(i, minimum, maximum))]); };
    return depth_pixel((row(0) * capture_filter_upper(params) + row(1) * capture_filter_middle(params) +
                        row(2) * capture_filter_lower(params)) / 64.0);
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
        "warp_capture_store_tests",
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
            galaxy::render::efb_conversion_shader_source(), "ps_5_0");

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

    std::vector<Pixel> run_color(
        const std::array<Pixel, 3>& rows,
        unsigned scale,
        const galaxy::render::EfbConversionShaderConstants& constants) {
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
        const galaxy::render::EfbConversionShaderConstants& constants) {
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
        const galaxy::render::EfbPeekSamplingGeometry& sampling) {
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
            galaxy::render::make_efb_peek_conversion_constants(
                sampling, galaxy::render::ReadbackKind::Color));
        if (result.size() != 1u) {
            throw std::runtime_error("color peek returned a non-1x1 result");
        }
        return result.front();
    }

    Pixel run_depth_peek(
        const std::vector<std::uint32_t>& z24_pixels,
        unsigned width,
        unsigned height,
        const galaxy::render::EfbPeekSamplingGeometry& sampling) {
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
            galaxy::render::make_efb_peek_conversion_constants(
                sampling, galaxy::render::ReadbackKind::Depth));
        if (result.size() != 1u) {
            throw std::runtime_error("depth peek returned a non-1x1 result");
        }
        return result.front();
    }

    std::vector<Pixel> run_color_field(
        const std::vector<Pixel>& pixels, unsigned width, unsigned height, unsigned scale,
        unsigned logical_size) {
        std::vector<std::uint8_t> bytes(pixels.size() * 4u);
        for (std::size_t index = 0u; index < pixels.size(); ++index) {
            bytes[index * 4u + 0u] = pixels[index].r;
            bytes[index * 4u + 1u] = pixels[index].g;
            bytes[index * 4u + 2u] = pixels[index].b;
            bytes[index * 4u + 3u] = pixels[index].a;
        }
        auto constants = galaxy::render::make_efb_peek_conversion_constants(
            galaxy::render::compute_efb_peek_sampling_geometry(0, 0, scale),
            galaxy::render::ReadbackKind::Color);
        constants.values[15] = -1.0f;
        return run_bytes(DXGI_FORMAT_R8G8B8A8_UNORM, bytes, width, height, logical_size, constants);
    }

    std::vector<Pixel> run_depth_field(
        const std::vector<std::uint32_t>& z24_pixels, unsigned width,
        unsigned height, unsigned scale, unsigned logical_size) {
        std::vector<std::uint8_t> bytes(z24_pixels.size() * sizeof(float));
        for (std::size_t i=0; i<z24_pixels.size(); ++i) {
            const float depth=1.0f-static_cast<float>(z24_pixels[i])/16777216.0f;
            std::memcpy(bytes.data()+i*sizeof(float), &depth, sizeof(depth));
        }
        auto constants=galaxy::render::make_efb_peek_conversion_constants(
            galaxy::render::compute_efb_peek_sampling_geometry(0,0,scale),
            galaxy::render::ReadbackKind::Depth);
        constants.values[15]=-1.0f;
        return run_bytes(DXGI_FORMAT_R32_FLOAT,bytes,width,height,logical_size,constants);
    }

    bool test_descriptor_ring_allocation_identity() {
        using namespace galaxy::render;
        DescriptorRing ring;
        const auto rejects = [](auto&& action, const char* message) {
            try {
                action();
            } catch (const std::runtime_error&) {
                return expect(true, message);
            }
            return expect(false, message);
        };
        bool passed = rejects([&] { (void)ring.allocate_persistent(1u); },
            "uninitialized persistent allocation fails without dereferencing a heap");
        passed &= rejects([&] { ring.begin_frame(0u); },
            "uninitialized frame binding is rejected");
        if (!expect(ring.initialize(device_.Get(),
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 8u, 2u, 3u),
            "create descriptor ring with prefix and two frame segments")) return false;
        ComPtr<ID3D12DescriptorHeap> original_heap{ring.heap()};
        const auto cpu = original_heap->GetCPUDescriptorHandleForHeapStart();
        const auto gpu = original_heap->GetGPUDescriptorHandleForHeapStart();
        const UINT stride = device_->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        const auto at = [&](const DescriptorRing::Table& table, unsigned index) {
            return table.cpu.ptr == cpu.ptr + static_cast<SIZE_T>(index) * stride &&
                table.gpu.ptr == gpu.ptr + static_cast<UINT64>(index) * stride;
        };
        passed &= expect(at(ring.allocate_persistent(2u), 0u),
            "persistent allocation begins at actual heap CPU/GPU bases");
        passed &= rejects([&] { (void)ring.allocate_persistent(~0u); },
            "unsigned-wrap persistent request is rejected before consuming slots");
        passed &= expect(at(ring.allocate_persistent(1u), 2u),
            "rejected persistent request preserves the next slot");
        passed &= rejects([&] { (void)ring.allocate_persistent(1u); },
            "persistent prefix cannot overflow into frame descriptors");
        passed &= rejects([&] { (void)ring.allocate(1u); },
            "frame allocation requires begin_frame and cannot overlap the prefix");
        ring.begin_frame(0u);
        passed &= expect(at(ring.allocate(5u), 3u),
            "first frame starts beyond the persistent prefix");
        passed &= rejects([&] { (void)ring.allocate(~0u); },
            "unsigned-wrap frame request is rejected before consuming slots");
        passed &= expect(at(ring.allocate(1u), 8u),
            "rejected frame request preserves the next slot");
        passed &= rejects([&] { ring.begin_frame(2u); },
            "out-of-heap frame slot is rejected without rewinding active state");
        passed &= expect(at(ring.allocate(2u), 9u),
            "invalid frame slot preserves the active frame cursor");
        passed &= rejects([&] { (void)ring.allocate(1u); },
            "full first frame cannot consume second-frame descriptors");
        ring.begin_frame(1u);
        passed &= expect(at(ring.allocate(8u), 11u),
            "second frame occupies exactly its own segment");
        passed &= expect(!ring.initialize(device_.Get(),
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, ~0u, 2u, 0u) &&
            !ring.initialize(device_.Get(),
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1u, 0u, 0u) &&
            !ring.initialize(nullptr,
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1u, 1u, 0u) &&
            ring.heap() == original_heap.Get(),
            "failed initialization preserves the existing heap and bounds");
        ring.begin_frame(0u);
        passed &= expect(at(ring.allocate(8u), 3u),
            "original limits and bases remain paired after rejected initialization");
        if (!expect(ring.initialize(device_.Get(),
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 4u, 1u, 0u),
            "replace descriptor heap while original identity remains alive")) return false;
        passed &= rejects([&] { (void)ring.allocate(1u); },
            "successful heap replacement requires a fresh frame binding");
        ring.begin_frame(0u);
        const auto replacement = ring.allocate(4u);
        passed &= expect(replacement.cpu.ptr ==
                ring.heap()->GetCPUDescriptorHandleForHeapStart().ptr &&
            replacement.gpu.ptr == ring.heap()->GetGPUDescriptorHandleForHeapStart().ptr,
            "cached descriptor bases refresh on successful heap replacement");
        return passed;
    }

    bool test_persistent_conversion_sources() {
        using namespace galaxy::render;
        const auto sampling = compute_efb_peek_sampling_geometry(0u, 0u, 1u);
        const std::vector<std::uint8_t> color{18u, 52u, 86u, 120u};
        bool passed = expect_pixel(run_bytes(kEfbColorFormat, color, 1u, 1u,
            1u, make_efb_peek_conversion_constants(sampling, ReadbackKind::Color),
            true).front(), {18u, 52u, 86u, 120u},
            "persistent color source survives frame descriptor overwrite/rewind");
        // Native R24/X8 views, not the R32 float fixture used by the broader
        // shader oracle. Endpoints are exact under both UNORM and reversed Z.
        for (const auto depth : {0u, 0x00ffffffu}) {
            const std::vector<std::uint8_t> bytes{
                static_cast<std::uint8_t>(depth),
                static_cast<std::uint8_t>(depth >> 8u),
                static_cast<std::uint8_t>(depth >> 16u), 0u};
            const std::uint8_t expected = depth == 0u ? 255u : 0u;
            passed &= expect_pixel(run_bytes(kEfbDepthResourceFormat, bytes,
                1u, 1u, 1u,
                make_efb_peek_conversion_constants(sampling, ReadbackKind::Depth),
                true).front(),
                {expected, expected, expected, 255u},
                "persistent native depth view survives frame descriptor overwrite/rewind");
        }
        return passed;
    }

    bool test_identity_efb_copy() {
        using namespace galaxy::render;
        auto params = make_filter_params(false);
        params.src_x = params.src_y = 0u;
        params.src_width = params.src_height = 16u;
        params.channels = 0u;
        bool passed = expect(detail::efb_identity_copy_box(params, false, 1u,
            16u, 16u).has_value(), "identity RGBA8 filter is admitted");
        for (unsigned flag = 0u; flag < 8u; ++flag) {
            auto rejected = params;
            switch (flag) {
            case 0u: rejected.channels = 6u; break;
            case 1u: rejected.luma = true; break;
            case 2u: rejected.half_scale = true; break;
            case 3u: rejected.display = true; break;
            case 4u: rejected.src_width = 0u; break;
            case 5u: rejected.src_x = kFrameWidth - 15u; break;
            case 6u: rejected.src_y = kFrameHeight - 15u; break;
            case 7u: rejected.filtered = true; break;
            }
            passed &= expect(!detail::efb_identity_copy_box(rejected, false,
                1u, 16u, 16u), "nonidentity copy cannot bypass conversion");
        }
        passed &= expect(!detail::efb_identity_copy_box(params, true, 1u, 16u, 16u) &&
            !detail::efb_identity_copy_box(params, false, 1u, 15u, 16u) &&
            !detail::efb_identity_copy_box(params, false, 1u, 16u, 15u),
            "depth and destination resampling cannot bypass conversion");
        std::size_t checked = 0u;
        // Full EFB extents (up to 16x) stress sampler coordinate precision; only the tested rectangle and its
        // neighbours are uploaded.
        for (const unsigned scale : {1u, 3u, 6u, 16u}) {
            const UINT source_width = kFrameWidth * scale;
            const UINT source_height = kFrameHeight * scale;
            const UINT size = 16u * scale;
            for (const auto origin : {std::array<unsigned, 2>{0u, 0u},
                    std::array<unsigned, 2>{319u, 255u},
                    std::array<unsigned, 2>{624u, 440u}}) {
                params.src_x = static_cast<std::uint16_t>(origin[0]);
                params.src_y = static_cast<std::uint16_t>(origin[1]);
                const auto box = detail::efb_identity_copy_box(params, false,
                    scale, size, size);
                if (!expect(box.has_value(), "bounded scaled identity copy admitted")) return false;
                D3D12_BOX upload_box{
                    box->left == 0u ? 0u : box->left - 1u,
                    box->top == 0u ? 0u : box->top - 1u, 0u,
                    std::min(source_width, box->right + 1u),
                    std::min(source_height, box->bottom + 1u), 1u};
                const UINT upload_width = upload_box.right - upload_box.left;
                const UINT upload_height = upload_box.bottom - upload_box.top;
                const auto source_pixel = [](unsigned x, unsigned y) {
                    // All 256 byte values and alpha extremes, with independent
                    // spatial changes so accidental linear blends are visible.
                    return Pixel{static_cast<std::uint8_t>(x * 73u + y * 19u),
                        static_cast<std::uint8_t>(x * 29u + y * 113u),
                        static_cast<std::uint8_t>(x ^ (y * 37u)),
                        static_cast<std::uint8_t>(x * 151u + y * 61u)};
                };
                std::vector<std::uint8_t> bytes(upload_width * upload_height * 4u);
                for (unsigned y = 0u; y < upload_height; ++y) {
                    for (unsigned x = 0u; x < upload_width; ++x) {
                        const auto p = source_pixel(x + upload_box.left, y + upload_box.top);
                        const auto offset = (y * upload_width + x) * 4u;
                        bytes[offset] = p.r; bytes[offset + 1u] = p.g;
                        bytes[offset + 2u] = p.b; bytes[offset + 3u] = p.a;
                    }
                }
                const auto sampling = compute_efb_copy_sampling_geometry(params, size, size, scale);
                const auto constants = make_efb_conversion_constants(params,
                    false, false, false, 0u, sampling.src_x, sampling.src_y,
                    sampling.step_x, sampling.step_y, sampling.filter_row_offset,
                    scale, kFrameHeight);
                const auto reference = run_bytes(kEfbColorFormat, bytes,
                    source_width, source_height, size, constants,
                    false, nullptr, &upload_box);
                const auto copied = run_bytes(kEfbColorFormat, bytes,
                    source_width, source_height, size, constants,
                    false, &*box, &upload_box);
                for (unsigned y = 0u; y < size; ++y) {
                    for (unsigned x = 0u; x < size; ++x) {
                        const auto expected = source_pixel(box->left + x, box->top + y);
                        const auto index = y * size + x;
                        if (!expect_pixel(reference[index], expected,
                                "identity shader returns exact source RGBA8 bytes") ||
                            !expect_pixel(copied[index], reference[index],
                                "copy command matches conversion shader including alpha")) {
                            std::fprintf(stderr, "identity mismatch scale=%u origin=%u,%u pixel=%u,%u\n",
                                scale, origin[0], origin[1], x, y);
                            return false;
                        }
                        ++checked;
                    }
                }
            }
        }
        std::printf("Identity EFB copy: %zu exact shader/copy/source pixels: %s\n",
            checked, passed ? "PASS" : "FAIL");
        return passed;
    }

    bool test_conversion_rtv_slot_identity() {
        using namespace galaxy::render;
        D3D12_DESCRIPTOR_HEAP_DESC heap_desc{};
        heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        heap_desc.NumDescriptors = 2u;
        ComPtr<ID3D12DescriptorHeap> heap;
        check_hr(device_->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&heap)),
            "Create scratch RTV identity heap");
        const auto first_slot = heap->GetCPUDescriptorHandleForHeapStart();
        auto second_slot = first_slot;
        second_slot.ptr += device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        D3D12_HEAP_PROPERTIES properties{};
        properties.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = desc.Height = 8u;
        desc.DepthOrArraySize = desc.MipLevels = 1u;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1u;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        ComPtr<ID3D12Resource> a, b;
        // A std::array, not a braced list: an initializer_list's elements are const, which `auto*` cannot bind.
        std::array<ComPtr<ID3D12Resource>*, 2> targets{&a, &b};
        for (auto* target : targets) {
            check_hr(device_->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE,
                &desc, D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
                IID_PPV_ARGS(target->ReleaseAndGetAddressOf())), "Create scratch RTV identity target");
        }
        detail::ConversionRtvSlot slot;
        bool passed = expect(slot.bind(device_.Get(), a.Get(), first_slot),
            "first scratch RTV use writes its descriptor");
        passed &= expect(!slot.bind(device_.Get(), a.Get(), first_slot),
            "consecutive copies to the same resource reuse the current slot contents");
        passed &= expect(slot.bind(device_.Get(), b.Get(), first_slot) &&
            slot.bind(device_.Get(), a.Get(), first_slot),
            "A/B/A copies rewrite the shared slot on both resource changes");
        passed &= expect(!slot.bind(device_.Get(), a.Get(), first_slot) &&
            slot.bind(device_.Get(), a.Get(), second_slot),
            "same-resource reuse requires the same descriptor slot");
        bool rejected = false;
        try { (void)slot.bind(device_.Get(), nullptr, second_slot); }
        catch (const std::runtime_error&) { rejected = true; }
        passed &= expect(rejected && !slot.bind(device_.Get(), a.Get(), second_slot),
            "failed scratch RTV bind preserves the previous slot identity");
        slot.reset();
        passed &= expect(slot.bind(device_.Get(), a.Get(), second_slot),
            "heap/device lifecycle reset forces the next descriptor write");
        slot.reset();
        // Calls exercise the actual descriptor writer; no GPU work is submitted.
        return passed;
    }

    bool test_xfb_retirement_rollback() {
        using namespace galaxy::render;
        EfbCopyManager manager;
        if (!manager.initialize(device_.Get(), 1u)) {
            throw std::runtime_error("initialize XFB rollback manager");
        }
        CaptureRequest params{};
        params.src_width = params.src_height = 8u;
        params.display = true;
        // Three published pictures fill the ring; the next acquire is the first again.
        XfbTexture* shown = nullptr;
        for (std::uint64_t frame = 1u; frame <= 3u; ++frame) {
            shown = manager.acquire_xfb(params, frame);
            manager.mark_xfb_copied(*shown, frame);
        }
        auto* shown_resource = shown->texture.Get();
        const auto shown_serial = shown->copy_serial;
        // A resize before the first begin_frame has no retirement list: the ownership failure path.
        params.src_width = 16u;
        bool rejected = false;
        try { (void)manager.acquire_xfb(params, 4u); }
        catch (const std::runtime_error&) { rejected = true; }
        const auto* latest = manager.latest();
        bool passed = expect(rejected && latest == shown && latest->texture.Get() == shown_resource &&
            latest->width == 8u && latest->height == 8u && latest->copy_serial == shown_serial &&
            latest->frame_stamp == 3u,
            "failed resize retirement preserves the shown picture's resource and identity");
        manager.begin_frame(0u, kFramesInFlight);
        const auto* resized = manager.acquire_xfb(params, 4u);
        passed &= expect(resized->slot == 0u && resized->texture != nullptr && resized->width == 16u &&
            resized->height == 8u && !resized->presentable && resized->copy_serial == 0u,
            "resize succeeds after retirement storage is available without inventing a copied frame");
        // No GPU commands reference these resources in this lifecycle fixture.
        manager.shutdown();
        return passed;
    }

    bool test_xfb_idle_pool_admission() {
        using namespace galaxy::render;
        EfbCopyManager manager;
        if (!manager.initialize(device_.Get(), 1u)) {
            throw std::runtime_error("initialize XFB pool manager");
        }
        manager.begin_frame(0u, kFramesInFlight);
        CaptureRequest params{};
        params.src_height = 8u;
        params.display = true;
        // Eight one-off sizes fill the idle pool; the ninth is the current XFB.
        for (unsigned i = 0; i < 9u; ++i) {
            params.src_width = static_cast<std::uint16_t>(32u + i);
            (void)manager.acquire_xfb(params, i + 1u);
        }
        ComPtr<ID3D12Resource> wanted = manager.acquire_xfb(params, 10u)->texture;
        manager.begin_frame(0u, kFramesInFlight);
        params.src_width = 41u;
        ComPtr<ID3D12Resource> other = manager.acquire_xfb(params, 11u)->texture;
        manager.begin_frame(1u, kFramesInFlight);
        params.src_width = 40u;
        const auto* before_fence = manager.acquire_xfb(params, 12u);
        // Witness ComPtrs keep resource identities distinct, so a released resource's address
        // cannot be reused by a new allocation.
        bool passed = expect(before_fence->texture.Get() != wanted.Get(),
            "another frame slot cannot recycle the recently retired XFB");
        manager.begin_frame(0u, kFramesInFlight);
        params.src_width = 41u;
        (void)manager.acquire_xfb(params, 13u);
        params.src_width = 40u;
        const auto* reused = manager.acquire_xfb(params, 14u);
        passed &= expect(reused->texture.Get() == wanted.Get() &&
            reused->width == 40u && reused->height == 8u,
            "full idle pool admits a current size and reuses it after its owning fence");
        manager.begin_frame(1u, kFramesInFlight);
        params.src_width = 41u;
        passed &= expect(manager.acquire_xfb(params, 15u)->texture.Get() == other.Get(),
            "second size is reusable only after its own retirement slot is reclaimed");
        // No command lists are submitted; in the renderer each begin_frame follows its fence wait.
        manager.shutdown();
        return passed;
    }

    bool test_decoded_allocation_budget() {
        using namespace galaxy::render;
        auto owner = std::make_unique<TextureCache>();
        TextureCache& cache = *owner;
        if (!cache.initialize(device_.Get())) {
            throw std::runtime_error("initialize allocation budget cache");
        }
        cache.begin_frame(0u, kFramesInFlight);
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> list;
        check_hr(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&allocator)), "Create allocation budget allocator");
        check_hr(device_->CreateCommandList(0u, D3D12_COMMAND_LIST_TYPE_DIRECT,
            allocator.Get(), nullptr, IID_PPV_ARGS(&list)), "Create allocation budget list");
        cache.backend().set_upload_list(list.Get());
        // 1024x1024 block records (the disc's compressed textures, nebula_cooked_texture.h) of 512 KiB that take
        // 4 MiB on the device, so 18 of them exceed the 64 MiB budget while their record bytes stay far below it.
        const auto record_of = [](std::uint16_t w, std::uint16_t h, std::uint8_t levels, std::uint8_t layout) {
            std::vector<std::byte> r(NEBULA_TEXTURE_HEADER + nebula_texture_level_bytes(w, h, levels, layout));
            nebula_texture_init(r.data(), w, h, levels, layout, 0u);
            return r;
        };
        constexpr std::uint32_t address = 0x10000000u;
        constexpr std::uint32_t stride = 1024u * 1024u;  // the records' keys apart
        constexpr unsigned kTextures = 18u;
        std::vector<std::byte> block = record_of(1024u, 1024u, 1u, NEBULA_TEXTURE_BLOCK4);
        const std::size_t record_bytes = block.size();
        for (unsigned i = 0u; i < kTextures; ++i) {
            block[NEBULA_TEXTURE_HEADER] = static_cast<std::byte>(i);
            hand_over(cache, address + i * stride, block.data(), block.size());
        }
        TestImage image{};
        image.address = address;
        image.width = image.height = 1024u;
        TextureSampler mode{};
        mode.min_linear = false, mode.mip = 0u;
        const auto first = cache.get(image.address, mode);
        const auto desc = first.resource->GetDesc();
        const auto allocation = device_->GetResourceAllocationInfo(0u, 1u, &desc).SizeInBytes;
        bool passed = expect(allocation > first.source_bytes &&
            cache.decoded_map_stats().resident_bytes == allocation &&
            cache.decoded_entry_source_bytes() == record_bytes,
            "a texture charges its device allocation, keeping the record-byte metric");
        const auto again = cache.get(image.address, mode);
        passed &= expect(again.resource == first.resource &&
            cache.decoded_map_stats().resident_bytes == allocation,
            "ordinary texture hit does not charge allocation twice");
        TextureHandle last{};
        for (unsigned i = 1u; i < kTextures; ++i) {
            image.address = address + i * stride;
            last = cache.get(image.address, mode);
        }
        const auto stats = cache.decoded_map_stats();
        passed &= expect(stats.budget_bytes == (64ull << 20u) &&
            stats.resident_bytes <= stats.budget_bytes && stats.evicted_entries > 0u &&
            stats.resident_bytes == stats.entries * allocation &&
            stats.evicted_bytes == stats.evicted_entries * allocation &&
            stats.entries == stats.lru_entries && cache.srv_index_retired(first.srv_index),
            "allocation budget evicts entries before record bytes reach the bound");
        passed &= expect(first.resource->GetDesc().Width == 1024u &&
            first.srv_index != last.srv_index,
            "budget eviction keeps the old resource and descriptor alive for this frame");
        cache.invalidate_all();
        passed &= expect(cache.decoded_map_stats().resident_bytes == 0u &&
            cache.decoded_entry_source_bytes() == 0u && cache.decoded_map_stats().lru_entries == 0u,
            "invalidation clears allocation and record-byte accounting together");
        cache.begin_frame(1u, kFramesInFlight);
        passed &= expect(cache.srv_index_retired(first.srv_index),
            "another frame slot cannot reclaim a budget-evicted descriptor");
        const std::vector<std::byte> mip_record = record_of(128u, 128u, 8u, NEBULA_TEXTURE_L8);
        hand_over(cache, address + kTextures * stride, mip_record.data(), mip_record.size());
        image.address = address + kTextures * stride;
        image.width = image.height = 128u;
        mode.min_linear = false, mode.mip = 1u;
        mode.max_lod = (7u * 16u) / 16.0f;
        const auto mipped = cache.get(image.address, mode);
        const auto mip_desc = mipped.resource->GetDesc();
        const auto mip_allocation = device_->GetResourceAllocationInfo(0u, 1u, &mip_desc).SizeInBytes;
        passed &= expect(mip_desc.MipLevels > 1u &&
            cache.decoded_map_stats().resident_bytes == mip_allocation,
            "a mipped texture charges its complete allocation");
        std::printf("Decoded allocation budget: source=%u allocation=%llu live=%llu evicted=%llu\n",
            static_cast<unsigned>(record_bytes), static_cast<unsigned long long>(allocation),
            static_cast<unsigned long long>(stats.resident_bytes),
            static_cast<unsigned long long>(stats.evicted_bytes));
        // No list is submitted; in the renderer each begin_frame follows its slot's fence wait.
        cache.backend().set_upload_list(nullptr);
        cache.shutdown();
        passed &= expect(cache.decoded_map_stats().resident_bytes == 0u &&
            cache.decoded_entry_source_bytes() == 0u, "shutdown resets both accounting domains");
        return passed;
    }

    // Async first-use expansion uploads the same texels as the synchronous one, in the same frame, for every texture
    // record layout (RGBA8, L8, LA8, BC1 blocks; whole and clipped extents).
    bool test_async_texture_decode_matches_sync() {
        using namespace galaxy::render;
        constexpr std::uint32_t address = 0x10000000u;
        struct Case {
            std::uint8_t layout;
            std::uint16_t width;
            std::uint16_t height;
        };
        const std::vector<Case> cases{
            {NEBULA_TEXTURE_RGBA8, 16u, 8u}, {NEBULA_TEXTURE_RGBA8, 13u, 7u}, {NEBULA_TEXTURE_BLOCK4, 16u, 16u},
            {NEBULA_TEXTURE_BLOCK4, 12u, 9u}, {NEBULA_TEXTURE_LA8, 8u, 4u}, {NEBULA_TEXTURE_LA8, 5u, 6u},
            {NEBULA_TEXTURE_L8, 16u, 8u}, {NEBULA_TEXTURE_L8, 5u, 3u},
        };
        // Each case a record of pseudo-random texels, 0x400 apart.
        std::vector<std::vector<std::byte>> records;
        for (const Case& test : cases) {
            std::vector<std::byte> record(NEBULA_TEXTURE_HEADER +
                                          nebula_texture_level_bytes(test.width, test.height, 1u, test.layout));
            nebula_texture_init(record.data(), test.width, test.height, 1u, test.layout, 0u);
            for (std::size_t i = NEBULA_TEXTURE_HEADER; i < record.size(); ++i) {
                record[i] = static_cast<std::byte>((i * 2654435761u + records.size() * 97u) >> 13u);
            }
            records.push_back(std::move(record));
        }
        const auto run = [&](bool async) {
            auto cache_owner = std::make_unique<TextureCache>();
            TextureCache& cache = *cache_owner;
            if (!cache.initialize(device_.Get())) {
                throw std::runtime_error("initialize async decode cache");
            }
            cache.begin_frame(0u, kFramesInFlight);
            ComPtr<ID3D12CommandAllocator> allocator;
            ComPtr<ID3D12GraphicsCommandList> list;
            check_hr(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                IID_PPV_ARGS(&allocator)), "Create async decode allocator");
            check_hr(device_->CreateCommandList(0u, D3D12_COMMAND_LIST_TYPE_DIRECT,
                allocator.Get(), nullptr, IID_PPV_ARGS(&list)), "Create async decode list");
            cache.backend().set_upload_list(list.Get());
            for (std::size_t i = 0; i < records.size(); ++i) {
                hand_over(cache, address + static_cast<std::uint32_t>(i) * 0x400u, records[i].data(), records[i].size());
            }
            cache.set_async_decode_allowed(async);
            std::vector<TextureHandle> handles;
            std::uint32_t offset = 0u;
            for (const Case& test : cases) {
                TestImage image{};
                image.address = address + offset;
                image.width = test.width;
                image.height = test.height;
                TextureSampler mode{};
                mode.min_linear = false, mode.mip = 0u;
                handles.push_back(cache.get(image.address, mode));
                offset += 0x400u;
            }
            cache.set_async_decode_allowed(false);
            cache.finish_pending_decodes();
            if (async && cache.async_decode_jobs() != cases.size()) {
                throw std::runtime_error("async decode path was not taken");
            }

            std::vector<ComPtr<ID3D12Resource>> readbacks;
            std::vector<UINT> pitches;
            for (std::size_t i = 0; i < cases.size(); ++i) {
                const UINT pitch = (cases[i].width * 4u + 255u) & ~255u;
                pitches.push_back(pitch);
                D3D12_HEAP_PROPERTIES hp{};
                hp.Type = D3D12_HEAP_TYPE_READBACK;
                D3D12_RESOURCE_DESC rd{};
                rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
                rd.Width = static_cast<UINT64>(pitch) * cases[i].height;
                rd.Height = 1u;
                rd.DepthOrArraySize = 1u;
                rd.MipLevels = 1u;
                rd.SampleDesc.Count = 1u;
                rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
                ComPtr<ID3D12Resource> readback;
                check_hr(device_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                    D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)),
                    "Create async decode readback");
                D3D12_RESOURCE_BARRIER barrier{};
                barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                barrier.Transition.pResource = handles[i].resource;
                barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
                barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
                list->ResourceBarrier(1u, &barrier);
                D3D12_TEXTURE_COPY_LOCATION src{};
                src.pResource = handles[i].resource;
                src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                D3D12_TEXTURE_COPY_LOCATION dst{};
                dst.pResource = readback.Get();
                dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                dst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                dst.PlacedFootprint.Footprint.Width = cases[i].width;
                dst.PlacedFootprint.Footprint.Height = cases[i].height;
                dst.PlacedFootprint.Footprint.Depth = 1u;
                dst.PlacedFootprint.Footprint.RowPitch = pitch;
                list->CopyTextureRegion(&dst, 0u, 0u, 0u, &src, nullptr);
                std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
                list->ResourceBarrier(1u, &barrier);
                readbacks.push_back(readback);
            }
            check_hr(list->Close(), "Close async decode list");
            ID3D12CommandList* lists[] = {list.Get()};
            queue_->ExecuteCommandLists(1u, lists);
            ComPtr<ID3D12Fence> fence;
            check_hr(device_->CreateFence(0u, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)),
                "Create async decode fence");
            check_hr(queue_->Signal(fence.Get(), 1u), "Signal async decode fence");
            if (fence->GetCompletedValue() < 1u) {
                check_hr(fence->SetEventOnCompletion(1u, fence_event_), "Wait async decode");
                WaitForSingleObject(fence_event_, INFINITE);
            }
            std::vector<std::vector<std::uint8_t>> pixels;
            for (std::size_t i = 0; i < cases.size(); ++i) {
                void* mapped = nullptr;
                const D3D12_RANGE range{0u, static_cast<SIZE_T>(pitches[i]) * cases[i].height};
                check_hr(readbacks[i]->Map(0u, &range, &mapped), "Map async decode readback");
                std::vector<std::uint8_t> image;
                for (UINT y = 0; y < cases[i].height; ++y) {
                    const auto* row = static_cast<const std::uint8_t*>(mapped) +
                        static_cast<std::size_t>(y) * pitches[i];
                    image.insert(image.end(), row, row + cases[i].width * 4u);
                }
                const D3D12_RANGE no_write{0u, 0u};
                readbacks[i]->Unmap(0u, &no_write);
                pixels.push_back(std::move(image));
            }
            cache.backend().set_upload_list(nullptr);
            cache.shutdown();
            return pixels;
        };
        const auto sync_pixels = run(false);
        const auto async_pixels = run(true);
        bool passed = true;
        for (std::size_t i = 0; i < cases.size(); ++i) {
            passed &= expect(sync_pixels[i] == async_pixels[i],
                "async first-use decode uploads the same texels as the synchronous decode");
        }
        // Join contract: every job finishes, the first failure is rethrown
        // exactly once, and later joins are clean.
        TextureDecodePool pool(2u);
        std::atomic<int> ran{0};
        for (int i = 0; i < 64; ++i) {
            pool.submit([&ran, i] {
                ran.fetch_add(1);
                if (i == 17) throw std::runtime_error("decode failure");
            });
        }
        bool rethrown = false;
        try {
            pool.wait_all();
        } catch (const std::runtime_error&) {
            rethrown = true;
        }
        passed &= expect(rethrown && ran.load() == 64 && pool.outstanding() == 0u,
            "decode pool joins every job and rethrows the first failure");
        pool.wait_all();
        return passed;
    }

    bool test_texture_descriptor_retirement() {
        using namespace galaxy::render;
        bool passed = true;
        auto cache_owner = std::make_unique<TextureCache>();
        TextureCache& cache = *cache_owner;
        if (!cache.initialize(device_.Get())) {
            throw std::runtime_error("initialize WARP retirement cache");
        }
        cache.begin_frame(0u, kFramesInFlight);
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> list;
        check_hr(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&allocator)), "Create retirement allocator");
        check_hr(device_->CreateCommandList(0u, D3D12_COMMAND_LIST_TYPE_DIRECT,
            allocator.Get(), nullptr, IID_PPV_ARGS(&list)), "Create retirement command list");
        cache.backend().set_upload_list(list.Get());
        constexpr std::uint32_t address = 0x10000000u;
        // An 8x8 RGBA8 texture record (nebula_cooked_texture.h).
        std::vector<std::byte> bytes(NEBULA_TEXTURE_HEADER + nebula_texture_level_bytes(8u, 8u, 1u, NEBULA_TEXTURE_RGBA8));
        nebula_texture_init(bytes.data(), 8u, 8u, 1u, NEBULA_TEXTURE_RGBA8, 0u);
        hand_over(cache, address, bytes.data(), bytes.size());
        TestImage image{};
        image.address = address;
        image.width = image.height = 8u;
        TextureSampler mode{};
        mode.min_linear = false, mode.mip = 0u;
        const auto initial_revision = cache.retirement_revision();
        const auto first = cache.get(image.address, mode);
        const auto hit = cache.get(image.address, mode);
        passed &= expect(hit.srv_index == first.srv_index &&
            !cache.srv_index_retired(first.srv_index) &&
            cache.retirement_revision() == initial_revision,
            "texture hits do not invalidate cross-frame handles");
        passed &= expect(cache.forget(address) == 1u &&
            cache.srv_index_retired(first.srv_index) &&
            cache.retirement_revision() == initial_revision + 1u,
            "retirement publishes a cross-frame handle lifetime change");
        image.address = address + 128u;
        const auto same_frame = cache.get(image.address, mode);
        passed &= expect(same_frame.srv_index != first.srv_index &&
            !cache.srv_index_retired(same_frame.srv_index) &&
            first.resource->GetDesc().Width == 8u,
            "retired resource and CPU descriptor survive later lookups in the same frame");
        cache.begin_frame(1u, kFramesInFlight);
        image.address = address + 256u;
        const auto other_slot = cache.get(image.address, mode);
        passed &= expect(other_slot.srv_index != first.srv_index &&
            cache.srv_index_retired(first.srv_index) &&
            !cache.srv_index_retired(other_slot.srv_index),
            "another frame slot cannot reclaim the retired CPU descriptor");
        // No command list is submitted; in the renderer, returning to slot 0 waits for its fence first.
        cache.begin_frame(0u, kFramesInFlight);
        passed &= expect(cache.srv_index_retired(first.srv_index),
            "reclaimed slot retains its retired identity until allocation");
        image.address = address + 384u;
        const auto reclaimed = cache.get(image.address, mode);
        passed &= expect(reclaimed.srv_index == first.srv_index &&
            !cache.srv_index_retired(reclaimed.srv_index) &&
            cache.retirement_revision() == initial_revision + 1u,
            "owning slot reclaims the descriptor without another retirement");
        cache.backend().set_upload_list(nullptr);
        cache.shutdown();
        return passed;
    }

    bool test_texture_cache_ranges() {
        using namespace galaxy::render;
        bool passed = true;
        // On the heap: the store is too large for the default stack.
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
        cache.backend().set_upload_list(list.Get());
        constexpr std::uint32_t address = 0x10000000u;
        // Texture records (nebula_cooked_texture.h), the only images the cache draws: an 8x8 RGBA8 one with three
        // authored levels, an 8x8 L8 one with one, a 64x4 L8 one for generated levels.
        const auto record_of = [](std::uint16_t w, std::uint16_t h, std::uint8_t levels, std::uint8_t layout) {
            std::vector<std::byte> r(NEBULA_TEXTURE_HEADER + nebula_texture_level_bytes(w, h, levels, layout));
            nebula_texture_init(r.data(), w, h, levels, layout, 0u);
            return r;
        };
        const std::vector<std::byte> authored = record_of(8u, 8u, 3u, NEBULA_TEXTURE_RGBA8);
        hand_over(cache, address, authored.data(), authored.size());
        TestImage image{};
        image.address = address;
        image.width = image.height = 8u;
        TextureSampler mode{};
        mode.min_linear = false, mode.mip = 1u;
        mode.max_lod = (32u) / 16.0f;
        const auto decoded = cache.get(image.address, mode);
        passed &= expect(decoded.source_bytes == authored.size() && decoded.mip_levels == 3u,
            "a record's authored levels all upload, the handle sized as the record");
        const auto reused = cache.get(image.address, mode);
        passed &= expect(reused.resource == decoded.resource && reused.srv_index == decoded.srv_index,
            "a record's second lookup reuses its resource and descriptor");
        // An 8x8 L8 record with one level serves a sampler that asks for three with its one, its size the record's.
        const std::vector<std::byte> single = record_of(8u, 8u, 1u, NEBULA_TEXTURE_L8);
        constexpr auto record_address = address + 0x9000u;
        hand_over(cache, record_address, single.data(), single.size());
        auto record_image = image; record_image.address = record_address;
        const auto record = cache.get(record_image.address, mode);
        passed &= expect(record.mip_levels == 1u && record.source_bytes == single.size() && record.width == 8u,
            "a texture record uploads the levels it holds and is sized as the record");

        const std::vector<std::byte> wide = record_of(64u, 4u, 1u, NEBULA_TEXTURE_RGBA8);  // a colour record: eligible
        hand_over(cache, address + 0x2000u, wide.data(), wide.size());
        const auto original_config = get_render_config();
        auto mip_config = original_config; mip_config.enhanced_mipmaps = false;
        set_render_config(mip_config);
        auto enhanced_image = image; enhanced_image.address = address + 0x2000u;
        enhanced_image.width = 64u; enhanced_image.height = 4u;
        TextureSampler enhanced_mode{};
        enhanced_mode.wrap_s = TexWrap::Repeat;
        enhanced_mode.wrap_t = TexWrap::Mirror;
        enhanced_mode.min_linear = true, enhanced_mode.mip = 0u;
        enhanced_mode.mag_linear = true;
        const auto no_enhancement = cache.get(enhanced_image.address, enhanced_mode);
        mip_config.enhanced_mipmaps = true; set_render_config(mip_config);
        const auto enhancement = cache.get(enhanced_image.address, enhanced_mode);
        TextureSampler decal_mode = enhanced_mode;
        decal_mode.wrap_t = TexWrap::Clamp;
        const auto decal = cache.get(enhanced_image.address, decal_mode);
        set_render_config(original_config);
        passed &= expect(no_enhancement.mip_levels == 1u && !no_enhancement.generated_mips &&
            enhancement.mip_levels == 7u && enhancement.generated_mips &&
            enhancement.resource != no_enhancement.resource && enhancement.source_bytes == wide.size(),
            "eligible native mip generation still reads live options and keeps its resource separate from the base texture");
        passed &= expect(decal.mip_levels == 1u && !decal.generated_mips,
            "a clamped texture keeps its single authored level");

        // Execute the recorded uploads and wait for the GPU before retirement and shutdown.
        check_hr(list->Close(), "Close texture cache uploads");
        ID3D12CommandList* lists[]{list.Get()};
        queue_->ExecuteCommandLists(1u, lists);
        const auto value = ++fence_value_;
        check_hr(queue_->Signal(fence_.Get(), value), "Signal texture cache uploads");
        check_hr(fence_->SetEventOnCompletion(value, fence_event_), "Wait texture cache uploads");
        if (WaitForSingleObject(fence_event_, 30000u) != WAIT_OBJECT_0) {
            throw std::runtime_error("texture cache upload fence timeout");
        }
        passed &= expect(cache.forget(address + 17u) == 0u,
            "a key that names no texture retires nothing (no memory ranges: a texture is its key)");

        cache.backend().set_upload_list(nullptr);
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
        const galaxy::render::EfbConversionShaderConstants& constants,
        bool persistent_source = false, const D3D12_BOX* identity_box = nullptr,
        const D3D12_BOX* source_upload_box = nullptr) {
        const D3D12_HEAP_PROPERTIES default_heap =
            heap_properties(D3D12_HEAP_TYPE_DEFAULT);
        const D3D12_HEAP_PROPERTIES upload_heap =
            heap_properties(D3D12_HEAP_TYPE_UPLOAD);
        const D3D12_HEAP_PROPERTIES readback_heap =
            heap_properties(D3D12_HEAP_TYPE_READBACK);

        ComPtr<ID3D12RootSignature> gx_root;
        ComPtr<ID3D12PipelineState> gx_pipeline;
        ComPtr<ID3D12Resource> gx_constants;
        const bool native_depth = source_format == galaxy::render::kEfbDepthResourceFormat;
        const D3D12_RESOURCE_DESC source_desc = texture_desc(source_format,
            source_width, source_height, native_depth
                ? D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
                : (identity_box != nullptr ? D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET
                                           : D3D12_RESOURCE_FLAG_NONE));
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

        auto source_upload_desc = source_desc;
        if (source_upload_box != nullptr) {
            source_upload_desc.Width = source_upload_box->right - source_upload_box->left;
            source_upload_desc.Height = source_upload_box->bottom - source_upload_box->top;
        }
        const UINT upload_width = static_cast<UINT>(source_upload_desc.Width);
        const UINT upload_height = source_upload_desc.Height;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT source_footprint{};
        UINT source_rows = 0u;
        UINT64 source_row_size = 0u;
        UINT64 source_upload_size = 0u;
        device_->GetCopyableFootprints(
            &source_upload_desc,
            0u,
            1u,
            0u,
            &source_footprint,
            &source_rows,
            &source_row_size,
            &source_upload_size);
        static_cast<void>(source_row_size);
        if (source_rows != upload_height) {
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
            static_cast<std::size_t>(upload_width) * 4u;
        if (source_bytes.size() != tight_row_bytes * upload_height) {
            throw std::runtime_error("source byte count does not match dimensions");
        }
        auto* upload_base = static_cast<std::uint8_t*>(upload_mapping) +
            source_footprint.Offset;
        for (UINT y = 0u; y < upload_height; ++y) {
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
                identity_box != nullptr ? D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
                                        : D3D12_RESOURCE_STATE_RENDER_TARGET,
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
        D3D12_DESCRIPTOR_HEAP_DESC srv_heap_desc{};
        srv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srv_heap_desc.NumDescriptors = 1u;
        srv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        ComPtr<ID3D12DescriptorHeap> srv_heap;
        galaxy::render::DescriptorRing source_ring;
        ComPtr<ID3D12Resource> companion_source;
        D3D12_GPU_DESCRIPTOR_HANDLE source_gpu{};
        D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc{};
        srv_desc.Format = native_depth ? galaxy::render::kEfbDepthSrvFormat : source_format;
        srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv_desc.Texture2D.MipLevels = 1u;
        if (persistent_source) {
            if (!source_ring.initialize(device_.Get(),
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 4u, 2u, 2u)) {
                throw std::runtime_error("Create persistent source test ring");
            }
            const auto companion_desc = texture_desc(native_depth
                ? galaxy::render::kEfbColorFormat : galaxy::render::kEfbDepthResourceFormat,
                1u, 1u, native_depth ? D3D12_RESOURCE_FLAG_NONE
                                     : D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
            check_hr(device_->CreateCommittedResource(&default_heap,
                D3D12_HEAP_FLAG_NONE, &companion_desc,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr,
                IID_PPV_ARGS(&companion_source)), "Create companion EFB source");
            const auto views = galaxy::render::detail::create_efb_conversion_source_views(
                device_.Get(), native_depth ? companion_source.Get() : source.Get(),
                native_depth ? source.Get() : companion_source.Get(), source_ring);
            source_gpu = views[native_depth ? 1u : 0u];
            srv_heap = source_ring.heap();
            // Null every frame descriptor in both segments: only the persistent source view can produce the
            // expected pixels.
            const UINT stride = device_->GetDescriptorHandleIncrementSize(
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            for (unsigned frame = 0u; frame < 6u; ++frame) {
                source_ring.begin_frame(frame % 2u);
                auto table = source_ring.allocate(4u);
                for (unsigned i = 0u; i < 4u; ++i) {
                    device_->CreateShaderResourceView(nullptr, &srv_desc, table.cpu);
                    table.cpu.ptr += stride;
                }
            }
        } else {
            check_hr(device_->CreateDescriptorHeap(&srv_heap_desc,
                IID_PPV_ARGS(&srv_heap)), "Create SRV heap");
            device_->CreateShaderResourceView(source.Get(), &srv_desc,
                srv_heap->GetCPUDescriptorHandleForHeapStart());
            source_gpu = srv_heap->GetGPUDescriptorHandleForHeapStart();
        }
        ComPtr<ID3D12DescriptorHeap> sampler_heap;
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
            &source_location, source_upload_box != nullptr ? source_upload_box->left : 0u,
            source_upload_box != nullptr ? source_upload_box->top : 0u, 0u,
            &upload_location, nullptr);
        D3D12_RESOURCE_BARRIER source_barrier{};
        source_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        source_barrier.Transition.pResource = source.Get();
        source_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        source_barrier.Transition.StateAfter =
            identity_box != nullptr ? D3D12_RESOURCE_STATE_RENDER_TARGET
                                    : D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
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
        command_list->OMSetRenderTargets(1u, &rtv, FALSE, nullptr);
        command_list->SetGraphicsRootSignature(root_signature_.Get());
        command_list->SetPipelineState(pipeline_.Get());
        ID3D12DescriptorHeap* descriptor_heaps[] = {srv_heap.Get()};
        command_list->SetDescriptorHeaps(1u, descriptor_heaps);
        command_list->SetGraphicsRoot32BitConstants(0u, 16u, constants.values.data(), 0u);
        command_list->SetGraphicsRootDescriptorTable(1u, source_gpu);
    
        command_list->IASetPrimitiveTopology(
            D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        if (identity_box != nullptr) {
            galaxy::render::detail::record_efb_identity_copy(command_list.Get(),
                source.Get(), destination.Get(), *identity_box);
        } else {
            command_list->DrawInstanced(3u, 1u, 0u, 0u);
        }

        D3D12_RESOURCE_BARRIER dest_barrier{};
        dest_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        dest_barrier.Transition.pResource = destination.Get();
        dest_barrier.Transition.StateBefore = identity_box != nullptr
            ? D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE : D3D12_RESOURCE_STATE_RENDER_TARGET;
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

bool test_clear_precision() {
    bool passed = true;
    const auto bytes = [](const galaxy::render::EfbClearValues& v) {
        return Pixel{static_cast<std::uint8_t>(std::lround(v.color[0] * 255.0f)),
            static_cast<std::uint8_t>(std::lround(v.color[1] * 255.0f)),
            static_cast<std::uint8_t>(std::lround(v.color[2] * 255.0f)),
            static_cast<std::uint8_t>(std::lround(v.color[3] * 255.0f))};
    };
    passed &= expect_pixel(bytes(galaxy::render::make_efb_clear_values(0x13579b7fu, 0x123456u, 0u)),
        {19u, 87u, 155u, 255u}, "RGB8 clears preserve each full 8-bit color channel");
    passed &= expect_pixel(bytes(galaxy::render::make_efb_clear_values(0x13579b7fu, 0x123456u, 1u)),
        {19u, 87u, 155u, 127u}, "clears of a frame with alpha keep the values as given");
    return passed;
}

galaxy::render::EfbConversionShaderConstants constants_for_case(
    const galaxy::render::CaptureRequest& params,
    unsigned scale,
    bool is_depth,
    bool intensity,
    std::uint8_t format) {
    const galaxy::render::EfbCopySamplingGeometry sampling =
        galaxy::render::compute_efb_copy_sampling_geometry(
            params, scale, scale, scale);
    return galaxy::render::make_efb_conversion_constants(
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
    const auto params = make_filter_params(true);
    for (const unsigned scale : {1u, 2u, 6u}) {
        const auto sampling = galaxy::render::compute_xfb_sampling_geometry(
            params, scale, scale, scale);
        const auto texture = galaxy::render::compute_efb_copy_sampling_geometry(
            params, scale, scale, scale);
        passed &= expect(sampling.src_y == static_cast<float>(scale) &&
            sampling.step_x == 1.0f && sampling.step_y == 1.0f &&
            sampling.filter_row_offset == 1.0f &&
            texture.filter_row_offset == static_cast<float>(scale),
            "display filter footprint leaves source mapping and texture copies intact");
        const auto constants = galaxy::render::make_efb_conversion_constants(
            params, false, false, true, 7u, sampling.src_x, sampling.src_y,
            sampling.step_x, sampling.step_y, sampling.filter_row_offset, scale, 3u);
        const auto result = harness.run_color(rows, scale, constants);
        for (unsigned y = 0; y < scale; ++y) {
            // The rectangle starts at logical row 1: its first physical row reads itself as the row above.
            const auto red = static_cast<std::uint8_t>((255u * 32u + 255u * 32u) >> 6u);
            for (unsigned x = 0; x < scale; ++x) {
                passed &= expect_pixel(result[y * scale + x], {red, 0u, 0u, 255u},
                    "scaled display filter stays inside its rectangle, keeping brightness and opaque alpha");
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
    galaxy::render::CaptureRequest filter = make_filter_params(true);
    filter.src_y = 0u;
    filter.src_height = 2u;
    passed &= expect_pixel(
        oracle_from_three_rows(rows, filter, false, 0u),
        Pixel{25u, 35u, 45u, 77u},
        "oracle display filter: half the line above");
    filter = make_filter_params(true);
    passed &= expect_pixel(
        oracle_from_three_rows(rows, filter, false, 0u),
        Pixel{40u, 50u, 60u, 77u},
        "oracle filter at the rectangle's top keeps the line");

    const std::array<Pixel, 3> red_rows{
        Pixel{0u, 0u, 0u, 1u},
        Pixel{255u, 0u, 0u, 77u},
        Pixel{0u, 0u, 0u, 2u},
    };
    const galaxy::render::CaptureRequest identity = make_filter_params(false);
    passed &= expect_pixel(
        oracle_from_three_rows(red_rows, identity, true, 3u),
        Pixel{82u, 82u, 82u, 82u},
        "oracle luminance R8 holds Y");
    passed &= expect_pixel(
        oracle_from_three_rows(red_rows, identity, true, 4u),
        Pixel{82u, 82u, 82u, 82u},
        "oracle luminance G8 holds Y");
    passed &= expect_pixel(
        oracle_from_three_rows(red_rows, identity, true, 5u),
        Pixel{82u, 82u, 82u, 82u},
        "oracle luminance B8 holds Y");

    const std::array<std::uint32_t, 3> depth_rows{0x102030u, 0x405060u, 0x708090u};
    galaxy::render::CaptureRequest depth_filter = make_filter_params(true);
    depth_filter.src_y = 0u;
    depth_filter.src_height = 2u;
    passed &= expect_pixel(
        oracle_depth(depth_rows, depth_filter),
        Pixel{40u, 40u, 40u, 255u},
        "oracle filters the depth");
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

        for (const unsigned scale : {1u, 2u, 3u, 4u, 5u, 6u}) {
            {
                galaxy::render::CaptureRequest params = make_filter_params(true);
                const Pixel expected =
                    oracle_from_three_rows(rows, params, false, 0u);
                char label[96]{};
                std::snprintf(
                    label,
                    sizeof(label),
                    "%ux color filter inside its rectangle",
                    scale);
                passed &= expect_uniform_pixels(
                    harness.run_color(
                        rows,
                        scale,
                        constants_for_case(params, scale, false, false, 0u)),
                    scale,
                    expected,
                    label);
            }

            const galaxy::render::CaptureRequest identity = make_filter_params(false);
            constexpr std::array<std::uint8_t, 3> kIntensityFormats{
                3u, 4u, 5u};
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

            const galaxy::render::CaptureRequest depth_filter = make_filter_params(true);
            char depth_label[64]{};
            std::snprintf(
                depth_label,
                sizeof(depth_label),
                "%ux depth vertical filter",
                scale);
            passed &= expect_uniform_pixels(
                harness.run_depth(
                    z24_rows,
                    scale,
                    constants_for_case(depth_filter, scale, true, false, 0u)),
                scale,
                oracle_depth(z24_rows, depth_filter),
                depth_label);
        }

        // A depth peek reads one pixel: at a scaled resolution one fixed physical texel, never a blend. Four logical
        // pixels cover both edges and the z24 endpoints; every other subpixel holds a different depth.
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
                    const galaxy::render::EfbPeekSamplingGeometry sampling =
                        galaxy::render::compute_efb_peek_sampling_geometry(
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
                    const galaxy::render::EfbPeekSamplingGeometry sampling =
                        galaxy::render::compute_efb_peek_sampling_geometry(
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
                    "whole pointer depth field keeps each peek's exact pixel and z24");
            }

            // Color peeks keep their full NxN box reduction. The center and corner deltas cancel only when
            // every physical subpixel takes part in the reduction.
            constexpr Pixel kColorMean{64u, 96u, 128u, 160u};
            std::vector<Pixel> color_block(
                static_cast<std::size_t>(scale) * scale,
                kColorMean);
            if (scale > 1u) {
                const galaxy::render::EfbPeekSamplingGeometry sampling =
                    galaxy::render::compute_efb_peek_sampling_geometry(
                        0u, 0u, scale);
                color_block[0] = Pixel{32u, 64u, 96u, 128u};
                const std::size_t center =
                    static_cast<std::size_t>(sampling.depth_texel_y) *
                        scale +
                    sampling.depth_texel_x;
                color_block[center] = Pixel{96u, 128u, 160u, 192u};
            }
            const galaxy::render::EfbPeekSamplingGeometry color_sampling =
                galaxy::render::compute_efb_peek_sampling_geometry(
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

            // The whole colour field (colour peeks from a draw-sync token's field): every logical pixel is its own
            // block's reduction, as a single peek of that pixel. The lens flare's visibility test reads its alpha.
            constexpr std::array<Pixel, 4> kFieldColors{
                Pixel{64u, 96u, 128u, 255u}, Pixel{200u, 40u, 80u, 0u},
                Pixel{16u, 240u, 32u, 128u}, Pixel{100u, 100u, 100u, 250u}};
            std::vector<Pixel> color_grid(static_cast<std::size_t>(width) * height);
            for (unsigned y = 0u; y < height; ++y) {
                for (unsigned x = 0u; x < width; ++x) {
                    color_grid[static_cast<std::size_t>(y) * width + x] =
                        kFieldColors[(y / scale) * 2u + x / scale];
                }
            }
            if (scale > 1u) {
                // Deltas that cancel only when the whole block takes part.
                Pixel& corner = color_grid[0];
                Pixel& next = color_grid[1];
                corner.r = static_cast<std::uint8_t>(corner.r - 32u);
                next.r = static_cast<std::uint8_t>(next.r + 32u);
            }
            const auto color_field = harness.run_color_field(color_grid, width, height, scale, 2u);
            for (std::size_t i = 0; i < kFieldColors.size(); ++i) {
                passed &= expect_pixel(color_field.at(i), kFieldColors[i],
                    "whole colour field reduces each logical pixel's block, alpha included");
            }
        }
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: WARP EFB conversion suite: %s\n", error.what());
        passed = false;
    }
    return passed;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::strcmp(argv[1], "--identity-efb-copy") == 0) {
        try {
            WarpConversionHarness harness;
            return harness.test_identity_efb_copy() ? 0 : 1;
        } catch (const std::exception& error) {
            std::fprintf(stderr, "FAILED: identity EFB copy: %s\n", error.what());
            return 1;
        }
    }
    if (argc == 2 && std::strcmp(argv[1], "--decoded-allocation-budget") == 0) {
        try {
            WarpConversionHarness harness;
            return harness.test_decoded_allocation_budget() ? 0 : 1;
        } catch (const std::exception& error) {
            std::fprintf(stderr, "FAILED: decoded allocation budget: %s\n", error.what());
            return 1;
        }
    }
    // Any other argument runs the default suite.
    bool passed = test_cpu_oracle();
    passed &= test_generated_mip_uniform_alpha();
    passed &= test_flatten_texel_checker();
    passed &= test_remove_texel_checker();
    passed &= test_clear_precision();
    passed &= test_warp_against_oracle();
    try {
        WarpConversionHarness harness;
        passed &= harness.test_texture_descriptor_retirement();
        passed &= harness.test_async_texture_decode_matches_sync();
        passed &= harness.test_xfb_retirement_rollback();
        passed &= harness.test_conversion_rtv_slot_identity();
        passed &= harness.test_descriptor_ring_allocation_identity();
        passed &= harness.test_persistent_conversion_sources();
        passed &= harness.test_identity_efb_copy();
        passed &= harness.test_xfb_idle_pool_admission();
        passed &= harness.test_texture_cache_ranges();
        passed &= test_scaled_xfb_filter();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: WARP texture cache suite: %s\n", error.what());
        passed = false;
    }
    return passed ? 0 : 1;
}
