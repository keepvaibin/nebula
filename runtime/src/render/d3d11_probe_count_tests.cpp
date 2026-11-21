// The screen probes' count (ledger P-332) on this machine's Direct3D 11 device: the probe shader
// (renderer_common.h screen_probe_shader_source) over points written as every renderer writes them
// (write_screen_probe_points) answers every point asked, whatever constant is bound at b0. Each of the game's three
// depth tests (the sun's 18 points, the pointer's 3, the talk test's 2) runs with another test's count bound at slot 0
// on purpose, as the faulty runs had it; every answer must be its own point's depth. Without a hardware Direct3D 11
// device it says so and passes nothing else.
#include "galaxy/render/d3d11/d3d11_context.h"
#include "galaxy/render/renderer_common.h"

#include <d3dcompiler.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

namespace {

using galaxy::render::d3d11::ComPtr;
using galaxy::render::d3d11::Context;

constexpr unsigned kWidth = 64u;
constexpr unsigned kHeight = 8u;
constexpr std::uint32_t kMostPoints = 18u;
constexpr std::uint32_t kUnwritten = 0xDEADBEEFu;

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

// The depth stored at a pixel: a number of its own, exact in a float (k / 1024).
float depth_at(unsigned x, unsigned y) {
    return static_cast<float>(y * kWidth + x + 1u) / 1024.0f;
}

struct Fixture {
    ComPtr<ID3D11ComputeShader> shader;
    ComPtr<ID3D11Texture2D> depth;
    ComPtr<ID3D11ShaderResourceView> depth_view;
    ComPtr<ID3D11Buffer> values;
    ComPtr<ID3D11UnorderedAccessView> values_view;
    ComPtr<ID3D11Buffer> readback;
};

bool make_fixture(ID3D11Device1* device, Fixture& out) {
    const std::string_view source = galaxy::render::screen_probe_shader_source();
    ComPtr<ID3DBlob> code;
    ComPtr<ID3DBlob> errors;
    if (FAILED(D3DCompile(source.data(), source.size(), "screen_probe", nullptr, nullptr, "main", "cs_5_0",
                          D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0u, &code, &errors)) ||
        FAILED(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &out.shader))) {
        std::cerr << (errors ? static_cast<const char*>(errors->GetBufferPointer()) : "no shader") << '\n';
        return false;
    }
    std::vector<float> depths(kWidth * kHeight);
    for (unsigned y = 0u; y < kHeight; ++y) {
        for (unsigned x = 0u; x < kWidth; ++x) {
            depths[y * kWidth + x] = depth_at(x, y);
        }
    }
    D3D11_TEXTURE2D_DESC texture{};
    texture.Width = kWidth;
    texture.Height = kHeight;
    texture.MipLevels = 1u;
    texture.ArraySize = 1u;
    texture.Format = DXGI_FORMAT_R32_FLOAT;
    texture.SampleDesc.Count = 1u;
    texture.Usage = D3D11_USAGE_IMMUTABLE;
    texture.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    const D3D11_SUBRESOURCE_DATA initial{depths.data(), static_cast<UINT>(kWidth * sizeof(float)), 0u};
    D3D11_BUFFER_DESC values{};
    values.ByteWidth = static_cast<UINT>(kMostPoints * sizeof(float));
    values.Usage = D3D11_USAGE_DEFAULT;
    values.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    values.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    values.StructureByteStride = sizeof(float);
    D3D11_BUFFER_DESC back{};
    back.ByteWidth = values.ByteWidth;
    back.Usage = D3D11_USAGE_STAGING;
    back.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    return SUCCEEDED(device->CreateTexture2D(&texture, &initial, &out.depth)) &&
           SUCCEEDED(device->CreateShaderResourceView(out.depth.Get(), nullptr, &out.depth_view)) &&
           SUCCEEDED(device->CreateBuffer(&values, nullptr, &out.values)) &&
           SUCCEEDED(device->CreateUnorderedAccessView(out.values.Get(), nullptr, &out.values_view)) &&
           SUCCEEDED(device->CreateBuffer(&back, nullptr, &out.readback));
}

// `count` probes spread over the depth (one off the frame, which reads 0), dispatched as the renderers dispatch them,
// with `bound` (another test's count) bound at b0 for the dispatch.
bool probes_answer(Context& context, Fixture& fixture, std::uint32_t count, std::uint32_t bound, const char* what) {
    ID3D11Device1* const device = context.device();
    ID3D11DeviceContext1* const immediate = context.immediate();
    const galaxy::render::FrameMap map(kWidth, kHeight);
    const float units = static_cast<float>(galaxy::render::kFrameUnits);
    std::vector<galaxy::render::ScreenProbe> probes(count);
    for (std::uint32_t i = 0u; i < count; ++i) {
        probes[i].x = (static_cast<float>((i * 7u) % kWidth) + 0.5f) / static_cast<float>(kWidth) * units;
        probes[i].y = (static_cast<float>((i * 3u) % kHeight) + 0.5f) / static_cast<float>(kHeight) * units;
    }
    probes[count - 1u].x = -1.0f;  // not asked: answers 0

    // The points as the renderers write them, in a buffer made as Direct3D 11's is (renderer_d3d11_scene.cpp).
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = static_cast<UINT>(galaxy::render::screen_probe_points_bytes(count));
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    desc.StructureByteStride = sizeof(galaxy::render::ScreenProbePixel);
    ComPtr<ID3D11Buffer> points;
    ComPtr<ID3D11ShaderResourceView> points_view;
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(device->CreateBuffer(&desc, nullptr, &points)) ||
        FAILED(device->CreateShaderResourceView(points.Get(), nullptr, &points_view)) ||
        FAILED(immediate->Map(points.Get(), 0u, D3D11_MAP_WRITE_DISCARD, 0u, &mapped))) {
        return expect(false, "the points' buffer");
    }
    galaxy::render::write_screen_probe_points(probes, map, mapped.pData);
    immediate->Unmap(points.Get(), 0u);

    // Another test's count, bound at b0 on purpose for the dispatch.
    const Context::ConstantPlace wrong = context.put_constants(&bound, sizeof(bound));
    if (wrong.buffer == nullptr) {
        return expect(false, "the wrong count's piece");
    }
    const UINT unwritten[4] = {kUnwritten, kUnwritten, kUnwritten, kUnwritten};
    immediate->ClearUnorderedAccessViewUint(fixture.values_view.Get(), unwritten);
    ID3D11ShaderResourceView* const views[2] = {fixture.depth_view.Get(), points_view.Get()};
    ID3D11UnorderedAccessView* const values = fixture.values_view.Get();
    immediate->CSSetShader(fixture.shader.Get(), nullptr, 0u);
    immediate->CSSetConstantBuffers1(0u, 1u, &wrong.buffer, &wrong.first, &wrong.count);
    immediate->CSSetShaderResources(0u, 2u, views);
    immediate->CSSetUnorderedAccessViews(0u, 1u, &values, nullptr);
    immediate->Dispatch((count + galaxy::render::kScreenProbeGroup - 1u) / galaxy::render::kScreenProbeGroup, 1u, 1u);
    ID3D11Buffer* const no_buffer = nullptr;
    ID3D11ShaderResourceView* const no_views[2] = {};
    ID3D11UnorderedAccessView* const no_values = nullptr;
    immediate->CSSetConstantBuffers(0u, 1u, &no_buffer);
    immediate->CSSetShaderResources(0u, 2u, no_views);
    immediate->CSSetUnorderedAccessViews(0u, 1u, &no_values, nullptr);
    immediate->CopyResource(fixture.readback.Get(), fixture.values.Get());
    (void)context.wait(context.signal());
    std::vector<std::uint32_t> answers(kMostPoints, 0u);
    if (FAILED(immediate->Map(fixture.readback.Get(), 0u, D3D11_MAP_READ, 0u, &mapped))) {
        return expect(false, "the answers' readback");
    }
    std::memcpy(answers.data(), mapped.pData, kMostPoints * sizeof(std::uint32_t));
    immediate->Unmap(fixture.readback.Get(), 0u);

    std::uint32_t wrong_answers = 0u;
    for (std::uint32_t i = 0u; i < kMostPoints; ++i) {
        std::uint32_t want = kUnwritten;  // past the points: never written
        if (i < count) {
            const galaxy::render::ScreenProbePixel pixel = galaxy::render::screen_probe_pixel(probes[i], map);
            const float value = pixel.x == ~0u ? 0.0f : std::clamp(1.0f - depth_at(pixel.x, pixel.y), 0.0f, 1.0f);
            std::memcpy(&want, &value, sizeof(want));
        }
        if (answers[i] != want) {
            if (wrong_answers < 4u) {
                float got = 0.0f;
                std::memcpy(&got, &answers[i], sizeof(got));
                std::cerr << what << ": point " << i << " answered 0x" << std::hex << answers[i] << std::dec << " (" << got
                          << "), its own depth is 0x" << std::hex << want << std::dec << '\n';
            }
            ++wrong_answers;
        }
    }
    if (wrong_answers != 0u) {
        std::cerr << what << ": " << wrong_answers << " of " << kMostPoints << " answers wrong (" << count << " points asked)\n";
    }
    return expect(wrong_answers == 0u, what);
}

}  // namespace

int main() {
    Context& context = Context::get();
    if (!context.initialize(std::string{}, false)) {
        std::cout << "no Direct3D 11 device (" << context.failure() << "): the probe count is not tested here\n";
        return 0;
    }
    Fixture fixture;
    bool passed = expect(make_fixture(context.device(), fixture), "the probe shader and its buffers");
    if (passed) {
        passed &= probes_answer(context, fixture, 18u, 2u, "the sun's 18 points with the talk test's count 2 bound");
        passed &= probes_answer(context, fixture, 3u, 2u, "the pointer's 3 points with the talk test's count 2 bound");
        passed &= probes_answer(context, fixture, 2u, 18u, "the talk test's 2 points with the sun's count 18 bound");
    }
    context.shutdown();
    std::cout << (passed ? "probe count tests passed\n" : "probe count tests FAILED\n");
    return passed ? 0 : 1;
}
