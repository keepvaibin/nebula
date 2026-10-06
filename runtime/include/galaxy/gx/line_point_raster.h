#pragma once

// Exact GX line/point raster state shared by the native backend and focused
// GPU tests. BP 0x22 stores both widths in sixths of one logical EFB pixel;
// the native D3D12 geometry expansion consumes physical-pixel values after
// applying the renderer's authoritative internal-resolution scale.

#include <array>
#include <cstdint>

namespace galaxy::gx {

struct LinePointRasterParams {
    float viewport_width_pixels = 0.0f;
    float viewport_height_pixels = 0.0f;
    float line_width_pixels = 0.0f;
    float point_size_pixels = 0.0f;
    std::uint32_t line_texcoord_mask = 0;
    std::uint32_t point_texcoord_mask = 0;
    std::uint32_t line_texcoord_divisor = 0;
    std::uint32_t point_texcoord_divisor = 0;
};
static_assert(sizeof(LinePointRasterParams) == 32u);

// `viewport_half_width` / `viewport_half_height` are raw XF viewport wd/ht.
// Valid GX viewports use wd > 0 and ht < 0. Before those registers are
// programmed, the renderer uses the full logical EFB fallback passed here.
[[nodiscard]] LinePointRasterParams make_line_point_raster_params(
    std::uint32_t su_lp_size,
    const std::array<std::uint32_t, 8>& texcoord_s_registers,
    float viewport_half_width,
    float viewport_half_height,
    unsigned efb_scale,
    unsigned fallback_efb_width = 640u,
    unsigned fallback_efb_height = 528u) noexcept;

}  // namespace galaxy::gx
