// Picture serial accounting, the scene target conversion shader, and scene target-copy geometry tests.

#include "galaxy/render/capture.h"
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

    galaxy::render::SceneCopyManager manager;
    if (!manager.initialize(device.Get(), 3u)) {
        return expect(false, "3x scene target copy manager initialization");
    }
    manager.begin_frame(0u, 2u);
    galaxy::render::CaptureRequest params{};
    params.src_width = 640u;
    params.src_height = 456u;
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
            "runtime scene target copy/peek conversion shader compiles as ps_5_0");
    }
    passed &= test_scaled_display_resource_allocation();

    {
        galaxy::render::DisplaySerialAccountingState state{};
        std::uint64_t texture_a = ~std::uint64_t{0};
        std::uint64_t texture_b = ~std::uint64_t{0};
        const auto first_100 =
            galaxy::render::account_display_present_serial(state, texture_a, 100u);
        const auto first_101 =
            galaxy::render::account_display_present_serial(state, texture_b, 101u);
        const auto replay_100 =
            galaxy::render::account_display_present_serial(state, texture_a, 100u);
        const auto replay_101 =
            galaxy::render::account_display_present_serial(state, texture_b, 101u);
        const auto repeat_101 =
            galaxy::render::account_display_present_serial(state, texture_b, 101u);
        const auto first_102 =
            galaxy::render::account_display_present_serial(state, texture_a, 102u);

        passed &= expect(
            first_100.first_presentation &&
                first_101.first_presentation && first_101.delta_one &&
                first_102.first_presentation && first_102.delta_one,
            "monotonically newer copy serials are counted on first presentation");
        passed &= expect(
            !replay_100.first_presentation &&
                replay_100.not_first_presentation &&
                !replay_101.first_presentation &&
                replay_101.not_first_presentation,
            "an A/B replay below the serial high-water mark is never counted as new");
        passed &= expect(
            replay_100.nonfirst_run == 1u &&
                replay_101.nonfirst_run == 2u &&
                repeat_101.adjacent_repeat &&
                repeat_101.nonfirst_run == 3u,
            "alternating stale serials extend the non-fresh run instead of resetting it");
    }

    {
        galaxy::render::DisplaySerialAccountingState state{};
        std::uint64_t texture_10 = ~std::uint64_t{0};
        std::uint64_t texture_12 = ~std::uint64_t{0};
        std::uint64_t texture_11 = ~std::uint64_t{0};
        static_cast<void>(
            galaxy::render::account_display_present_serial(
                state, texture_10, 10u));
        const auto first_12 =
            galaxy::render::account_display_present_serial(state, texture_12, 12u);
        const auto late_11 =
            galaxy::render::account_display_present_serial(state, texture_11, 11u);
        passed &= expect(
            first_12.first_presentation && first_12.delta_multi,
            "a skipped serial is reported as a multi-serial production gap");
        passed &= expect(
            late_11.first_presentation && late_11.below_high_water,
            "a late serial below the diagnostic high-water mark is still counted exactly on first presentation");
    }

    {
        galaxy::render::CaptureRequest bloom{};
        bloom.src_x = 480u;
        bloom.src_width = 160u;
        bloom.src_height = 114u;
        bloom.half_scale = true;
        const auto native_bloom = galaxy::render::compute_scene_copy_sampling_geometry(bloom, 80u, 57u, 6u);
        passed &= expect_near(native_bloom.src_x, 2880.0f,
            "native bloom samples the genuine scaled scene target origin");
        passed &= expect_near(native_bloom.step_x, 12.0f,
            "native bloom downsamples the scaled source instead of lowering the scene");
    }
    {
        const galaxy::render::ScaledSceneCopyExtent native =
            galaxy::render::compute_scaled_scene_copy_extent(640u, 456u, 1u);
        const galaxy::render::ScaledSceneCopyExtent double_res =
            galaxy::render::compute_scaled_scene_copy_extent(640u, 456u, 2u);
        const galaxy::render::ScaledSceneCopyExtent quadruple_res =
            galaxy::render::compute_scaled_scene_copy_extent(640u, 456u, 4u);
        const galaxy::render::ScaledSceneCopyExtent sixfold_res =
            galaxy::render::compute_scaled_scene_copy_extent(640u, 456u, 6u);
        passed &= expect(
            native.width == 640u && native.height == 456u,
            "1x display picture backing texture preserves logical copy dimensions");
        passed &= expect(
            double_res.width == 1280u && double_res.height == 912u,
            "2x display picture backing texture retains 2x scene target resolution");
        passed &= expect(
            quadruple_res.width == 2560u && quadruple_res.height == 1824u,
            "4x display picture backing texture retains 4x scene target resolution");
        passed &= expect(
            sixfold_res.width == 3840u && sixfold_res.height == 2736u,
            "6x display picture backing texture retains 6x scene target resolution");
        passed &= expect(
            galaxy::render::compute_scaled_scene_copy_extent(
                640u, 456u, 0u).width == 640u &&
                galaxy::render::compute_scaled_scene_copy_extent(
                    640u, 4096u, 99u).height == 16384u,
            "scaled display picture extent clamps scale to 1-6 and D3D12 dimensions");
    }
    {
        galaxy::render::CaptureRequest params{};
        params.src_x = 10u;
        params.src_y = 20u;
        params.src_width = 640u;
        params.src_height = 456u;
        const galaxy::render::SceneCopySamplingGeometry display =
            galaxy::render::compute_scene_copy_sampling_geometry(
                params, 1280u, 912u, 2u);
        passed &= expect_near(
            display.src_x, 20.0f,
            "2x display copy scales the scene target source X origin");
        passed &= expect_near(
            display.src_y, 40.0f,
            "2x display copy scales the scene target source Y origin");
        passed &= expect_near(
            display.step_x, 1.0f,
            "2x display copy preserves every scaled scene target column in scaled display picture");
        passed &= expect_near(
            display.step_y, 1.0f,
            "2x display copy preserves every scaled scene target row in scaled display picture");
        passed &= expect_near(
            display.filter_row_offset, 2.0f,
            "2x copy filter samples adjacent logical rows two texels apart");

        const galaxy::render::SceneCopySamplingGeometry half_scale =
            galaxy::render::compute_scene_copy_sampling_geometry(
                params, 640u, 456u, 2u);
        passed &= expect_near(
            half_scale.step_x, 2.0f,
            "2x internal half-scale texture copy downsamples its scaled source by two");
        passed &= expect_near(
            half_scale.step_y, 2.0f,
            "2x internal half-scale texture copy keeps proportional Y sampling");

        const galaxy::render::SceneCopySamplingGeometry sixfold =
            galaxy::render::compute_scene_copy_sampling_geometry(
                params, 3840u, 2736u, 6u);
        passed &= expect_near(
            sixfold.step_x, 1.0f,
            "6x display copy retains every scaled scene target column");
        passed &= expect_near(
            sixfold.step_y, 1.0f,
            "6x display copy retains every scaled scene target row");
    }
    {
        const galaxy::render::ScaledSceneRect scissor =
            galaxy::render::compute_scaled_scene_rect(
                -10, 10, 650, 600, 640u, 528u, 3u);
        passed &= expect(
            scissor.left == 0 && scissor.top == 30 &&
                scissor.right == 1920 && scissor.bottom == 1584,
            "3x scene target scissor scales and clamps to the physical scene target extent");
        passed &= expect(
            galaxy::render::compute_scaled_scene_rect(
                20, 20, 10, 10, 640u, 528u, 4u).empty(),
            "scaled inverted scissor remains an empty raster region");
        const galaxy::render::ScaledSceneRect sixfold_scissor =
            galaxy::render::compute_scaled_scene_rect(
                -10, 10, 650, 600, 640u, 528u, 6u);
        passed &= expect(
            sixfold_scissor.left == 0 && sixfold_scissor.top == 60 &&
                sixfold_scissor.right == 3840 &&
                sixfold_scissor.bottom == 3168,
            "6x scene target scissor scales and clamps to the physical scene target extent");
        const galaxy::render::ScaledSceneRect oversized_target =
            galaxy::render::compute_scaled_scene_rect(
                0, 0,
                std::numeric_limits<std::int32_t>::max(),
                std::numeric_limits<std::int32_t>::max(),
                std::numeric_limits<unsigned>::max(),
                std::numeric_limits<unsigned>::max(),
                6u);
        passed &= expect(
            oversized_target.right ==
                    std::numeric_limits<std::int32_t>::max() &&
                oversized_target.bottom ==
                    std::numeric_limits<std::int32_t>::max(),
            "6x rectangle helper saturates oversized public target extents");
    }
    {
        const float full_viewport[6] = {0.0f, 0.0f, 640.0f, 456.0f, 0.0f, 1.0f};
        const galaxy::render::ScaledViewport viewport =
            galaxy::render::compute_scaled_viewport(full_viewport, 4u);
        passed &= expect_near(
            viewport.x, 0.0f,
            "4x full viewport retains its zero X origin");
        passed &= expect_near(
            viewport.y, 0.0f,
            "4x full viewport retains its zero Y origin");
        passed &= expect_near(
            viewport.width, 2560.0f,
            "4x viewport rasterizes geometry across the scaled scene target width");
        passed &= expect_near(
            viewport.height, 1824.0f,
            "4x viewport rasterizes geometry across the scaled copy height");
        passed &= expect(
            viewport.valid() && viewport.min_depth <= viewport.max_depth,
            "scaled viewport preserves a valid reversed depth range");
        const galaxy::render::ScaledViewport sixfold_viewport =
            galaxy::render::compute_scaled_viewport(full_viewport, 6u);
        passed &= expect_near(
            sixfold_viewport.width, 3840.0f,
            "6x viewport spans the scaled scene target width");
        passed &= expect_near(
            sixfold_viewport.height, 2736.0f,
            "6x viewport spans the scaled copy height");
    }
    {
        for (const unsigned scale : {1u, 2u, 3u, 4u, 5u, 6u}) {
            const galaxy::render::ScenePeekSamplingGeometry peek =
                galaxy::render::compute_scene_peek_sampling_geometry(
                    123u, 45u, scale);
            const std::uint32_t expected_x = 123u * scale;
            const std::uint32_t expected_y = 45u * scale;
            char label[128]{};
            std::snprintf(
                label,
                sizeof(label),
                "%ux scene target peek addresses the scaled logical-pixel block",
                scale);
            passed &= expect(
                peek.src_x == static_cast<float>(expected_x) &&
                    peek.src_y == static_cast<float>(expected_y) &&
                    peek.step == static_cast<float>(scale),
                label);

            std::snprintf(
                label,
                sizeof(label),
                "%ux scene target depth peek uses deterministic center-nearest point semantics",
                scale);
            passed &= expect(
                peek.depth_texel_x == expected_x + scale / 2u &&
                    peek.depth_texel_y == expected_y + scale / 2u,
                label);

            std::snprintf(
                label,
                sizeof(label),
                "%ux scene target color peek reduces the complete scaled pixel block",
                scale);
            passed &= expect(
                peek.color_reduction_samples == scale * scale,
                label);

            const galaxy::render::ScenePeekSamplingGeometry bottom_right =
                galaxy::render::compute_scene_peek_sampling_geometry(
                    639u, 527u, scale);
            std::snprintf(
                label,
                sizeof(label),
                "%ux scene target depth point remains inside the bottom-right scaled block",
                scale);
            passed &= expect(
                bottom_right.depth_texel_x >= 639u * scale &&
                    bottom_right.depth_texel_x < 640u * scale &&
                    bottom_right.depth_texel_y >= 527u * scale &&
                    bottom_right.depth_texel_y < 528u * scale,
                label);
        }

        const galaxy::render::ScenePeekSamplingGeometry clamped_low =
            galaxy::render::compute_scene_peek_sampling_geometry(1u, 1u, 0u);
        const galaxy::render::ScenePeekSamplingGeometry clamped_high =
            galaxy::render::compute_scene_peek_sampling_geometry(1u, 1u, 99u);
        passed &= expect(
            clamped_low.depth_texel_x == 1u &&
                clamped_low.depth_texel_y == 1u &&
                clamped_high.depth_texel_x ==
                    galaxy::render::kMaxSceneScale + galaxy::render::kMaxSceneScale / 2u &&
                clamped_high.depth_texel_y ==
                    galaxy::render::kMaxSceneScale + galaxy::render::kMaxSceneScale / 2u,
            "scene target peek geometry clamps unsupported scales before selecting a texel");
    }
    passed &= expect(
        galaxy::render::scene_copy_single_channel(2u, false) && !galaxy::render::scene_copy_single_channel(2u, true) &&
            !galaxy::render::scene_copy_single_channel(galaxy::render::kCopyLayoutIntensityAlpha, false),
        "alpha captures are one-channel pictures; depth and intensity-alpha ones are not");

    if (!passed) {
        return 1;
    }

    std::cout << "scene target copy tests passed\n";
    return 0;
}
