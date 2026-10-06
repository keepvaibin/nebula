#include "galaxy/gx/efb_copy.h"
#include "galaxy/gx/renderer_d3d12.h"
#include "galaxy/gx/texture_cache.h"

#include <dxgi1_4.h>

#include <cmath>
#include <cstdio>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

galaxy::gx::XfbTexture make_xfb(
    std::uint32_t guest_addr,
    std::uint64_t frame_stamp) {
    galaxy::gx::XfbTexture xfb{};
    xfb.guest_addr = guest_addr;
    xfb.frame_stamp = frame_stamp;
    xfb.width = 640;
    xfb.height = 456;
    return xfb;
}

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

bool expect_near(float actual, float expected, const char* message) {
    return expect(std::abs(actual - expected) < 0.0001f, message);
}

bool test_scaled_xfb_resource_allocation() {
    using Microsoft::WRL::ComPtr;

    ComPtr<IDXGIFactory4> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        return expect(false, "DXGI factory creation for scaled XFB allocation");
    }
    ComPtr<IDXGIAdapter> warp_adapter;
    if (FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp_adapter)))) {
        return expect(false, "DXGI WARP adapter creation for scaled XFB allocation");
    }
    ComPtr<ID3D12Device> device;
    if (FAILED(D3D12CreateDevice(
            warp_adapter.Get(),
            D3D_FEATURE_LEVEL_11_0,
            IID_PPV_ARGS(&device)))) {
        return expect(false, "D3D12 WARP device creation for scaled XFB allocation");
    }

    galaxy::gx::EfbCopyManager manager;
    if (!manager.initialize(device.Get(), 3u)) {
        return expect(false, "3x EFB copy manager initialization");
    }
    manager.begin_frame(0u, 2u);
    galaxy::gx::EfbCopyParams params{};
    params.src_width = 640u;
    params.src_height = 456u;
    params.dest_addr = 0x01000000u;
    params.dest_stride = 1280u;
    params.y_scale_raw = 256u;
    params.scale_y = true;
    params.copy_to_xfb = true;
    galaxy::gx::XfbTexture* xfb = manager.acquire_xfb(params, 1u);
    const bool allocated = xfb != nullptr && xfb->texture != nullptr;
    const D3D12_RESOURCE_DESC desc =
        allocated ? xfb->texture->GetDesc() : D3D12_RESOURCE_DESC{};
    const bool result = expect(
        allocated &&
            xfb->guest_addr == params.dest_addr &&
            xfb->width == 1920u && xfb->height == 1368u &&
            desc.Width == 1920u && desc.Height == 1368u,
        "3x XFB keeps logical guest identity while allocating a 3x GPU resource");
    manager.shutdown();
    return result;
}

}  // namespace

int main() {
    bool passed = true;

    {
        std::string diagnostics;
        const bool shader_valid =
            galaxy::gx::validate_efb_conversion_shader(diagnostics);
        if (!shader_valid) {
            std::cerr << diagnostics << '\n';
        }
        passed &= expect(
            shader_valid,
            "runtime EFB copy/peek conversion shader compiles as ps_5_0");
    }
    passed &= test_scaled_xfb_resource_allocation();

    const galaxy::gx::XfbTexture requested_current =
        make_xfb(0x01000000u, 30u);
    const galaxy::gx::XfbTexture requested_stale =
        make_xfb(0x01001000u, 11u);
    const galaxy::gx::XfbTexture requested_one_stamp_stale =
        make_xfb(0x01004000u, 29u);
    const galaxy::gx::XfbTexture latest = make_xfb(0x01002000u, 30u);
    const galaxy::gx::XfbTexture last_valid = make_xfb(0x01003000u, 9u);
    const std::uint64_t never_presented = ~std::uint64_t{0};

    {
        const galaxy::gx::XfbPresentSelection selection =
            galaxy::gx::select_xfb_for_present(
                false,
                nullptr,
                &latest,
                nullptr,
                30u,
                2u,
                never_presented);
        passed &= expect(
            selection.selected == &latest,
            "unspecified XFB request selects latest copied XFB");
        passed &= expect(
            !selection.missing && !selection.stale_preserved,
            "unspecified XFB request is not reported as missing or stale");
    }

    {
        const galaxy::gx::XfbPresentSelection selection =
            galaxy::gx::select_xfb_for_present(
                true,
                &requested_current,
                &latest,
                &last_valid,
                30u,
                2u,
                never_presented);
        passed &= expect(
            selection.selected == &requested_current,
            "explicit current VI-selected XFB is selected exactly");
        passed &= expect(
            !selection.requested_stale &&
                !selection.stale_preserved &&
                !selection.missing &&
                !selection.missing_preserved,
            "explicit current VI-selected XFB has no preservation flags");
    }

    {
        const galaxy::gx::XfbPresentSelection selection =
            galaxy::gx::select_xfb_for_present(
                true,
                &requested_stale,
                &latest,
                &last_valid,
                30u,
                20u,
                never_presented);
        passed &= expect(
            selection.selected == &requested_stale,
            "explicit stale VI-selected XFB is preserved, not replaced by latest");
        passed &= expect(
            selection.requested_stale && selection.stale_preserved,
            "explicit stale VI-selected XFB reports stale-preserved state");
        passed &= expect(
            !selection.missing && !selection.missing_preserved,
            "explicit stale VI-selected XFB is not reported as missing");
    }

    {
        const galaxy::gx::XfbPresentSelection selection =
            galaxy::gx::select_xfb_for_present(
                true,
                &requested_one_stamp_stale,
                &latest,
                &last_valid,
                30u,
                60u,
                requested_one_stamp_stale.frame_stamp);
        passed &= expect(
            selection.selected == &requested_one_stamp_stale,
            "duplicate stale VI-selected XFB remains guest-selected within the preserve window");
        passed &= expect(
            selection.requested_stale &&
                selection.stale_preserved &&
                !selection.stale_expired,
            "duplicate stale VI-selected XFB reports stale-preserved state");
    }

    {
        const galaxy::gx::XfbPresentSelection selection =
            galaxy::gx::select_xfb_for_present(
                true,
                &requested_one_stamp_stale,
                &latest,
                &last_valid,
                30u,
                60u,
                latest.frame_stamp);
        passed &= expect(
            selection.selected == &requested_one_stamp_stale,
            "older already-presented VI-selected XFB remains guest-selected within the preserve window");
        passed &= expect(
            selection.requested_stale &&
                selection.stale_preserved &&
                !selection.stale_expired,
            "older already-presented VI-selected XFB reports stale-preserved state");
    }

    {
        const galaxy::gx::XfbPresentSelection selection =
            galaxy::gx::select_xfb_for_present(
                true,
                &requested_one_stamp_stale,
                &latest,
                &last_valid,
                30u,
                60u,
                requested_one_stamp_stale.frame_stamp - 1u);
        passed &= expect(
            selection.selected == &requested_one_stamp_stale,
            "newer unpresented stale VI-selected XFB is honored within the preserve window");
        passed &= expect(
            selection.requested_stale &&
                selection.stale_preserved &&
                !selection.stale_expired,
            "newer unpresented stale VI-selected XFB reports stale-preserved state");
    }

    {
        const galaxy::gx::XfbPresentSelection selection =
            galaxy::gx::select_xfb_for_present(
                true,
                &requested_stale,
                &latest,
                &last_valid,
                30u,
                2u,
                never_presented);
        passed &= expect(
            selection.selected == &requested_stale,
            "expired stale VI-selected XFB remains guest-selected");
        passed &= expect(
            selection.requested_stale &&
                selection.stale_preserved &&
                !selection.stale_expired,
            "expired stale VI-selected XFB reports stale-preserved state");
    }

    {
        const galaxy::gx::XfbPresentSelection selection =
            galaxy::gx::select_xfb_for_present(
                true,
                &requested_one_stamp_stale,
                &latest,
                &last_valid,
                30u,
                1u,
                never_presented);
        passed &= expect(
            selection.selected == &requested_one_stamp_stale,
            "one-copy stale VI-selected XFB is still selected exactly");
        passed &= expect(
            selection.requested_stale &&
                selection.stale_preserved &&
                !selection.stale_expired,
            "one-copy stale policy reports preserved state");
    }

    {
        const galaxy::gx::XfbPresentSelection selection =
            galaxy::gx::select_xfb_for_present(
                true,
                &requested_one_stamp_stale,
                &latest,
                &last_valid,
                latest.frame_stamp,
                1u,
                never_presented);
        passed &= expect(
            selection.selected == &requested_one_stamp_stale,
            "same-clock one-copy stale VI-selected XFB is still selected");
        passed &= expect(
            selection.requested_stale &&
                selection.stale_preserved &&
                !selection.stale_expired,
            "same-clock XFB age reports one-copy stale preservation");
    }

    {
        const galaxy::gx::XfbPresentSelection selection =
            galaxy::gx::select_xfb_for_present(
                true,
                &requested_one_stamp_stale,
                &latest,
                &last_valid,
                65u,
                30u,
                requested_one_stamp_stale.frame_stamp);
        passed &= expect(
            selection.selected == &requested_one_stamp_stale,
            "present-aged stale VI-selected XFB remains guest-selected");
        passed &= expect(
            selection.requested_stale &&
                selection.stale_preserved &&
                !selection.stale_expired,
            "present-aged stale VI-selected XFB reports stale-preserved state");
    }

    {
        const galaxy::gx::XfbPresentSelection selection =
            galaxy::gx::select_xfb_for_present(
                true,
                &requested_one_stamp_stale,
                &latest,
                &last_valid,
                100u,
                60u,
                never_presented);
        passed &= expect(
            selection.selected == &requested_one_stamp_stale,
            "far-expired explicit VI-selected XFB remains guest-selected");
        passed &= expect(
            selection.requested_stale &&
                selection.stale_preserved &&
                !selection.stale_expired,
            "current-frame age preserves an explicit VI-selected XFB");
    }

    {
        const galaxy::gx::XfbPresentSelection selection =
            galaxy::gx::select_xfb_for_present(
                true,
                &requested_one_stamp_stale,
                &latest,
                &last_valid,
                30u,
                0u,
                never_presented);
        passed &= expect(
            selection.selected == &requested_one_stamp_stale,
            "zero-age stale policy still honors an explicit stale VI buffer");
        passed &= expect(
            selection.requested_stale &&
                selection.stale_preserved &&
                !selection.stale_expired,
            "zero-age stale policy reports explicit stale preservation");
    }

    {
        const galaxy::gx::XfbPresentSelection selection =
            galaxy::gx::select_xfb_for_present(
                true,
                nullptr,
                &latest,
                &last_valid,
                30u,
                30u,
                never_presented);
        passed &= expect(
            selection.selected == &last_valid,
            "unknown/missing explicit XFB preserves the last valid VI-selected XFB");
        passed &= expect(
            selection.missing && selection.missing_preserved,
            "unknown/missing explicit XFB reports missing-preserved state");
        passed &= expect(
            !selection.stale_preserved,
            "unknown/missing explicit XFB is not reported as stale-preserved");
    }

    {
        const galaxy::gx::XfbPresentSelection selection =
            galaxy::gx::select_xfb_for_present(
                true,
                nullptr,
                &latest,
                &last_valid,
                30u,
                2u,
                never_presented);
        passed &= expect(
            selection.selected == &last_valid,
            "missing explicit XFB preserves last valid selection after age expiry");
        passed &= expect(
            selection.missing &&
                selection.missing_preserved &&
                !selection.missing_expired,
            "old missing explicit XFB reports missing-preserved state");
    }

    {
        const galaxy::gx::XfbPresentSelection selection =
            galaxy::gx::select_xfb_for_present(
                true,
                nullptr,
                &latest,
                nullptr,
                30u,
                2u,
                never_presented);
        passed &= expect(
            selection.selected == nullptr,
            "missing explicit XFB with no previous VI selection does not use latest");
        passed &= expect(
            selection.missing && !selection.missing_preserved,
            "missing explicit XFB with no previous VI selection reports an uncovered miss");
    }

    {
        galaxy::gx::XfbSerialAccountingState state{};
        std::uint64_t texture_a = ~std::uint64_t{0};
        std::uint64_t texture_b = ~std::uint64_t{0};
        const auto first_100 =
            galaxy::gx::account_xfb_present_serial(state, texture_a, 100u);
        const auto first_101 =
            galaxy::gx::account_xfb_present_serial(state, texture_b, 101u);
        const auto replay_100 =
            galaxy::gx::account_xfb_present_serial(state, texture_a, 100u);
        const auto replay_101 =
            galaxy::gx::account_xfb_present_serial(state, texture_b, 101u);
        const auto repeat_101 =
            galaxy::gx::account_xfb_present_serial(state, texture_b, 101u);
        const auto first_102 =
            galaxy::gx::account_xfb_present_serial(state, texture_a, 102u);

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
        galaxy::gx::XfbSerialAccountingState state{};
        std::uint64_t texture_10 = ~std::uint64_t{0};
        std::uint64_t texture_12 = ~std::uint64_t{0};
        std::uint64_t texture_11 = ~std::uint64_t{0};
        static_cast<void>(
            galaxy::gx::account_xfb_present_serial(
                state, texture_10, 10u));
        const auto first_12 =
            galaxy::gx::account_xfb_present_serial(state, texture_12, 12u);
        const auto late_11 =
            galaxy::gx::account_xfb_present_serial(state, texture_11, 11u);
        passed &= expect(
            first_12.first_presentation && first_12.delta_multi,
            "a skipped serial is reported as a multi-serial production gap");
        passed &= expect(
            late_11.first_presentation && late_11.below_high_water,
            "a late serial below the diagnostic high-water mark is still counted exactly on first presentation");
    }

    passed &= expect(
        galaxy::gx::compute_xfb_copy_height(456u, 256u, true) == 456u,
        "enabled XFB y-scale 1.0 preserves height");
    passed &= expect(
        galaxy::gx::compute_xfb_copy_height(456u, 256u, false) == 456u,
        "disabled XFB y-scale ignores the scale register");
    passed &= expect(
        galaxy::gx::compute_xfb_copy_height(456u, 511u, true) == 228u,
        "maximum finite inverse XFB y-step shrinks height");
    passed &= expect(
        galaxy::gx::compute_xfb_copy_height(456u, 128u, true) == 911u,
        "small inverse XFB y-step expands height");
    passed &= expect(
        galaxy::gx::compute_xfb_copy_height(456u, 511u, false) == 456u,
        "disabled XFB y-scale ignores a maximum register value");
    passed &= expect(
        galaxy::gx::compute_xfb_copy_height(456u, 128u, false) == 456u,
        "disabled XFB y-scale ignores a small register value");
    passed &= expect(
        galaxy::gx::compute_xfb_copy_height(456u, 0u, false) == 456u,
        "disabled XFB y-scale never consumes an invalid scale register");
    passed &= expect(
        galaxy::gx::compute_xfb_copy_height(456u, 152u, true) == 768u &&
            galaxy::gx::compute_xfb_copy_height(456u, 192u, true) == 608u &&
            galaxy::gx::compute_xfb_copy_height(456u, 228u, true) == 512u,
        "RVL SDK XFB divisor correction preserves exact integral scale endpoints");
    passed &= expect(
        galaxy::gx::compute_xfb_copy_height(456u, 1u, true) == 1024u,
        "enabled XFB y-scale caps the guest-visible result at 1024 lines");
    {
        bool rejected_zero_scale = false;
        try {
            static_cast<void>(
                galaxy::gx::compute_xfb_copy_height(456u, 0u, true));
        } catch (const std::invalid_argument&) {
            rejected_zero_scale = true;
        }
        passed &= expect(
            rejected_zero_scale,
            "enabled XFB y-scale rejects a non-finite zero inverse step");
    }
    {
        galaxy::gx::EfbCopyParams bloom{};
        bloom.src_x = 480u;
        bloom.src_width = 160u;
        bloom.src_height = 114u;
        bloom.target_format = 4u;
        bloom.dest_stride = 1280u;
        passed &= expect(galaxy::gx::is_galaxy_bloom_workspace_copy(bloom, false),
            "observed late RGB565 bright-pass copy qualifies");
        passed &= expect(!galaxy::gx::is_galaxy_bloom_workspace_copy(bloom, true),
            "depth source at the same rectangle must remain scaled");
        bloom.src_x = 0u;
        passed &= expect(!galaxy::gx::is_galaxy_bloom_workspace_copy(bloom, false),
            "early mask at reused guest address must remain scaled");
        bloom.src_x = 480u;
        bloom.half_scale = true;
        bloom.dest_stride = 640u;
        passed &= expect(galaxy::gx::is_galaxy_bloom_workspace_copy(bloom, false),
            "observed bloom downsample qualifies");
        const auto native_bloom = galaxy::gx::compute_efb_copy_sampling_geometry(bloom, 80u, 57u, 6u);
        passed &= expect_near(native_bloom.src_x, 2880.0f,
            "native bloom samples the genuine scaled EFB origin");
        passed &= expect_near(native_bloom.step_x, 12.0f,
            "native bloom downsamples the scaled source instead of lowering the scene");
        bloom.clear = true;
        passed &= expect(!galaxy::gx::is_galaxy_bloom_workspace_copy(bloom, false),
            "copy-clear ownership cannot qualify for bloom resolution override");
        bloom.clear = false;
        bloom.src_x = 320u;
        bloom.src_y = 114u;
        bloom.src_width = 80u;
        bloom.src_height = 57u;
        bloom.half_scale = false;
        passed &= expect(galaxy::gx::is_galaxy_bloom_workspace_copy(bloom, false),
            "late small blur workspace qualifies");
        bloom.target_format = 7u;
        passed &= expect(!galaxy::gx::is_galaxy_bloom_workspace_copy(bloom, false),
            "nearby alpha/shadow workspaces remain scaled");
    }
    {
        const galaxy::gx::ScaledEfbCopyExtent native =
            galaxy::gx::compute_scaled_efb_copy_extent(640u, 456u, 1u);
        const galaxy::gx::ScaledEfbCopyExtent double_res =
            galaxy::gx::compute_scaled_efb_copy_extent(640u, 456u, 2u);
        const galaxy::gx::ScaledEfbCopyExtent quadruple_res =
            galaxy::gx::compute_scaled_efb_copy_extent(640u, 456u, 4u);
        const galaxy::gx::ScaledEfbCopyExtent sixfold_res =
            galaxy::gx::compute_scaled_efb_copy_extent(640u, 456u, 6u);
        passed &= expect(
            native.width == 640u && native.height == 456u,
            "1x XFB backing texture preserves logical copy dimensions");
        passed &= expect(
            double_res.width == 1280u && double_res.height == 912u,
            "2x XFB backing texture retains 2x EFB resolution");
        passed &= expect(
            quadruple_res.width == 2560u && quadruple_res.height == 1824u,
            "4x XFB backing texture retains 4x EFB resolution");
        passed &= expect(
            sixfold_res.width == 3840u && sixfold_res.height == 2736u,
            "6x XFB backing texture retains 6x EFB resolution");
        passed &= expect(
            galaxy::gx::compute_scaled_efb_copy_extent(
                640u, 456u, 0u).width == 640u &&
                galaxy::gx::compute_scaled_efb_copy_extent(
                    640u, 4096u, 99u).height == 16384u,
            "scaled XFB extent clamps scale to 1-6 and D3D12 dimensions");
    }
    {
        galaxy::gx::EfbCopyParams params{};
        params.src_x = 10u;
        params.src_y = 20u;
        params.src_width = 640u;
        params.src_height = 456u;
        const galaxy::gx::EfbCopySamplingGeometry display =
            galaxy::gx::compute_efb_copy_sampling_geometry(
                params, 1280u, 912u, 2u);
        passed &= expect_near(
            display.src_x, 20.0f,
            "2x display copy scales the EFB source X origin");
        passed &= expect_near(
            display.src_y, 40.0f,
            "2x display copy scales the EFB source Y origin");
        passed &= expect_near(
            display.step_x, 1.0f,
            "2x display copy preserves every scaled EFB column in scaled XFB");
        passed &= expect_near(
            display.step_y, 1.0f,
            "2x display copy preserves every scaled EFB row in scaled XFB");
        passed &= expect_near(
            display.filter_row_offset, 2.0f,
            "2x copy filter samples adjacent logical rows two texels apart");

        const galaxy::gx::EfbCopySamplingGeometry half_scale =
            galaxy::gx::compute_efb_copy_sampling_geometry(
                params, 640u, 456u, 2u);
        passed &= expect_near(
            half_scale.step_x, 2.0f,
            "2x internal half-scale texture copy downsamples its scaled source by two");
        passed &= expect_near(
            half_scale.step_y, 2.0f,
            "2x internal half-scale texture copy keeps proportional Y sampling");

        const galaxy::gx::EfbCopySamplingGeometry sixfold =
            galaxy::gx::compute_efb_copy_sampling_geometry(
                params, 3840u, 2736u, 6u);
        passed &= expect_near(
            sixfold.step_x, 1.0f,
            "6x display copy retains every scaled EFB column");
        passed &= expect_near(
            sixfold.step_y, 1.0f,
            "6x display copy retains every scaled EFB row");
    }
    {
        const galaxy::gx::ScaledEfbRect scissor =
            galaxy::gx::compute_scaled_efb_rect(
                -10, 10, 650, 600, 640u, 528u, 3u);
        passed &= expect(
            scissor.left == 0 && scissor.top == 30 &&
                scissor.right == 1920 && scissor.bottom == 1584,
            "3x EFB scissor scales and clamps to the physical EFB extent");
        passed &= expect(
            galaxy::gx::compute_scaled_efb_rect(
                20, 20, 10, 10, 640u, 528u, 4u).empty(),
            "scaled inverted scissor remains an empty raster region");
        const galaxy::gx::ScaledEfbRect sixfold_scissor =
            galaxy::gx::compute_scaled_efb_rect(
                -10, 10, 650, 600, 640u, 528u, 6u);
        passed &= expect(
            sixfold_scissor.left == 0 && sixfold_scissor.top == 60 &&
                sixfold_scissor.right == 3840 &&
                sixfold_scissor.bottom == 3168,
            "6x EFB scissor scales and clamps to the physical EFB extent");
        const galaxy::gx::ScaledEfbRect oversized_target =
            galaxy::gx::compute_scaled_efb_rect(
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
        const float full_viewport[6] = {
            320.0f,
            -228.0f,
            16777215.0f,
            662.0f,
            570.0f,
            16777215.0f};
        const galaxy::gx::ScaledGxViewport viewport =
            galaxy::gx::compute_scaled_gx_viewport(full_viewport, 4u);
        passed &= expect_near(
            viewport.x, 0.0f,
            "4x full GX viewport retains its zero X origin");
        passed &= expect_near(
            viewport.y, 0.0f,
            "4x full GX viewport retains its zero Y origin");
        passed &= expect_near(
            viewport.width, 2560.0f,
            "4x GX viewport rasterizes geometry across the scaled EFB width");
        passed &= expect_near(
            viewport.height, 1824.0f,
            "4x GX viewport rasterizes geometry across the scaled copy height");
        passed &= expect(
            viewport.valid() && viewport.min_depth <= viewport.max_depth,
            "scaled GX viewport preserves a valid reversed depth range");
        const galaxy::gx::ScaledGxViewport sixfold_viewport =
            galaxy::gx::compute_scaled_gx_viewport(full_viewport, 6u);
        passed &= expect_near(
            sixfold_viewport.width, 3840.0f,
            "6x GX viewport spans the scaled EFB width");
        passed &= expect_near(
            sixfold_viewport.height, 2736.0f,
            "6x GX viewport spans the scaled copy height");
    }
    {
        for (const unsigned scale : {1u, 2u, 3u, 4u, 5u, 6u}) {
            const galaxy::gx::EfbPeekSamplingGeometry peek =
                galaxy::gx::compute_efb_peek_sampling_geometry(
                    123u, 45u, scale);
            const std::uint32_t expected_x = 123u * scale;
            const std::uint32_t expected_y = 45u * scale;
            char label[128]{};
            std::snprintf(
                label,
                sizeof(label),
                "%ux EFB peek addresses the scaled logical-pixel block",
                scale);
            passed &= expect(
                peek.src_x == static_cast<float>(expected_x) &&
                    peek.src_y == static_cast<float>(expected_y) &&
                    peek.step == static_cast<float>(scale),
                label);

            std::snprintf(
                label,
                sizeof(label),
                "%ux EFB depth peek uses deterministic center-nearest point semantics",
                scale);
            passed &= expect(
                peek.depth_texel_x == expected_x + scale / 2u &&
                    peek.depth_texel_y == expected_y + scale / 2u,
                label);

            std::snprintf(
                label,
                sizeof(label),
                "%ux EFB color peek reduces the complete scaled pixel block",
                scale);
            passed &= expect(
                peek.color_reduction_samples == scale * scale,
                label);

            const galaxy::gx::EfbPeekSamplingGeometry bottom_right =
                galaxy::gx::compute_efb_peek_sampling_geometry(
                    639u, 527u, scale);
            std::snprintf(
                label,
                sizeof(label),
                "%ux EFB depth point remains inside the bottom-right scaled block",
                scale);
            passed &= expect(
                bottom_right.depth_texel_x >= 639u * scale &&
                    bottom_right.depth_texel_x < 640u * scale &&
                    bottom_right.depth_texel_y >= 527u * scale &&
                    bottom_right.depth_texel_y < 528u * scale,
                label);
        }

        const galaxy::gx::EfbPeekSamplingGeometry clamped_low =
            galaxy::gx::compute_efb_peek_sampling_geometry(1u, 1u, 0u);
        const galaxy::gx::EfbPeekSamplingGeometry clamped_high =
            galaxy::gx::compute_efb_peek_sampling_geometry(1u, 1u, 99u);
        passed &= expect(
            clamped_low.depth_texel_x == 1u &&
                clamped_low.depth_texel_y == 1u &&
                clamped_high.depth_texel_x ==
                    galaxy::gx::kMaxEfbScale + galaxy::gx::kMaxEfbScale / 2u &&
                clamped_high.depth_texel_y ==
                    galaxy::gx::kMaxEfbScale + galaxy::gx::kMaxEfbScale / 2u,
            "EFB peek geometry clamps unsupported scales before selecting a texel");
    }
    passed &= expect(
        galaxy::gx::efb_copy_alias_texture_format(7u) ==
            static_cast<std::uint8_t>(galaxy::gx::TexFormat::I8),
        "A8 screen-alpha EFB copies alias as I8 textures");

    if (!passed) {
        return 1;
    }

    std::cout << "XFB present selection tests passed\n";
    return 0;
}
