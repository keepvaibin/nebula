#pragma once

// GX line and point raster parameters for the line/point geometry shaders. BP 0x22 (SU_LPSIZE) gives both
// widths in sixths of a logical EFB pixel; the expansion works in physical pixels at the EFB scale.

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

// `viewport_half_width` / `viewport_half_height` are the raw XF viewport wd/ht; a valid GX viewport has wd > 0
// and ht < 0. Any other viewport (not yet programmed) takes the fallback EFB size.
[[nodiscard]] LinePointRasterParams make_line_point_raster_params(
    std::uint32_t su_lp_size,
    const std::array<std::uint32_t, 8>& texcoord_s_registers,
    float viewport_half_width,
    float viewport_half_height,
    unsigned efb_scale,
    unsigned fallback_efb_width = 640u,
    unsigned fallback_efb_height = 528u) noexcept;

}  // namespace galaxy::gx
