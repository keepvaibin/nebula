// Picture serial accounting, the scene target conversion shader, and scene target-copy geometry tests.

#include "galaxy/render/frame_pass.h"
#include "galaxy/render/renderer_d3d12.h"
#include "galaxy/render/texture_store.h"

#include <d3dcompiler.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <cmath>
#include <cstdio>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

bool expect_near(float actual, float expected, const char* message) {
    return expect(std::abs(actual - expected) < 0.0001f, message);
}

bool test_scaled_display_resource_allocation() {
    using Microsoft::WRL::ComPtr;

    ComPtr<IDXGIFactory4> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        return expect(false, "DXGI factory creation for scaled display picture allocation");
    }
    ComPtr<IDXGIAdapter> warp_adapter;
    if (FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp_adapter)))) {
        return expect(false, "DXGI WARP adapter creation for scaled display picture allocation");
    }
    ComPtr<ID3D12Device> device;
    if (FAILED(D3D12CreateDevice(
            warp_adapter.Get(),
            D3D_FEATURE_LEVEL_11_0,
            IID_PPV_ARGS(&device)))) {
        return expect(false, "D3D12 WARP device creation for scaled display picture allocation");
    }

    galaxy::render::DisplayPictures manager;
    if (!manager.initialize(device.Get(), galaxy::render::FrameMap(1920u, 1368u))) {
        return expect(false, "3x scene target copy manager initialization");
    }
    manager.begin_frame(0u, 2u);
    galaxy::render::FramePassRequest params{};
    params.display = true;
    galaxy::render::DisplayTexture* display_picture = manager.acquire_display(params, 1u);
    const bool allocated = display_picture != nullptr && display_picture->texture != nullptr;
    const D3D12_RESOURCE_DESC desc =
        allocated ? display_picture->texture->GetDesc() : D3D12_RESOURCE_DESC{};
    const bool result = expect(
        allocated &&
            display_picture->slot == 0u &&
            display_picture->width == 1920u && display_picture->height == 1368u &&
            desc.Width == 1920u && desc.Height == 1368u,
        "a 3x picture keeps its logical size while allocating a 3x GPU resource");
    manager.shutdown();
    return result;
}

// The scene conversion shader compiles at shader model 5.0 too.
bool compiles_scene_conversion_shader(std::string& diagnostics) {
    const std::string_view source = galaxy::render::scene_conversion_shader_source();
    Microsoft::WRL::ComPtr<ID3DBlob> code, errors;
    const HRESULT hr = D3DCompile(source.data(), source.size(), nullptr, nullptr, nullptr, "main", "ps_5_0",
                                  D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
    if (FAILED(hr)) {
        diagnostics = errors ? static_cast<const char*>(errors->GetBufferPointer()) : "D3DCompile failed";
        return false;
    }
    return true;
}

}  // namespace

int main() {
    bool passed = true;

    {
        std::string diagnostics;
        const bool shader_valid =
            compiles_scene_conversion_shader(diagnostics);
        if (!shader_valid) {
            std::cerr << diagnostics << '\n';
        }
        passed &= expect(
            shader_valid,
            "runtime scene target capture shader compiles as ps_5_0");
    }
    passed &= test_scaled_display_resource_allocation();

    {
        const galaxy::render::FrameMap frame(1280u, 912u);
        const galaxy::render::FrameExtent whole = galaxy::render::frame_pass_extent(frame);
        passed &= expect(whole.width == 1280u && whole.height == 912u, "a capture is the frame's size");
        const auto same = galaxy::render::frame_pass_geometry(1280u, 912u, frame);
        passed &= expect_near(same.step_x, 1.0f, "a whole capture keeps every column");
        passed &= expect_near(same.step_y, 1.0f, "a whole capture keeps every row");
    }
    {
        const galaxy::render::FrameMap frame(1920u, 1368u);
        const galaxy::render::ScaledSceneRect scissor = galaxy::render::rect_pixels(-100, 32768, 70000, 70000, frame);
        passed &= expect(scissor.left == 0 && scissor.top == 684 && scissor.right == 1920 && scissor.bottom == 1368,
                         "a rectangle in frame units clamps to the target");
        passed &= expect(galaxy::render::rect_pixels(200, 200, 100, 100, frame).empty(), "an inverted rectangle is empty");
        const galaxy::render::ScaledSceneRect oversized =
            galaxy::render::rect_pixels(0, 0, std::numeric_limits<std::int32_t>::max(),
                                        std::numeric_limits<std::int32_t>::max(), frame);
        passed &= expect(oversized.right == 1920 && oversized.bottom == 1368, "a huge rectangle saturates");
    }
    {
        const float full_viewport[6] = {0.0f, 0.0f, 65536.0f, 65536.0f, 0.0f, 1.0f};
        const galaxy::render::ScaledViewport viewport =
            galaxy::render::viewport_pixels(full_viewport, galaxy::render::FrameMap(2560u, 1824u));
        passed &= expect_near(viewport.x, 0.0f, "a full viewport starts at zero across");
        passed &= expect_near(viewport.y, 0.0f, "a full viewport starts at zero down");
        passed &= expect_near(viewport.width, 2560.0f, "a full viewport spans the target's width");
        passed &= expect_near(viewport.height, 1824.0f, "a full viewport spans the target's height");
        passed &= expect(viewport.valid() && viewport.min_depth <= viewport.max_depth, "the depth range stays valid");
    }

    if (!passed) {
        return 1;
    }

    std::cout << "scene target copy tests passed\n";
    return 0;
}
