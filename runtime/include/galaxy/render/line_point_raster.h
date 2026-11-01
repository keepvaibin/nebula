#pragma once

// Line and point raster parameters for the line/point geometry shaders: the game gives both widths in sixths of a
// logical EFB pixel; the expansion works in physical pixels at the EFB scale.

#include <array>
#include <cstdint>

namespace galaxy::render {

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

// The game's line width and point size (sixths of a logical EFB pixel), each one's texture-offset selector (0 none,
// 1-5: 1/16 to 1 of a texel, 6 and 7 as 5) and the coordinates it offsets (bit n: coordinate n).
struct LinePointValues {
    std::uint8_t line_width = 0;
    std::uint8_t point_size = 0;
    std::uint8_t line_offset = 0;
    std::uint8_t point_offset = 0;
    std::uint8_t line_coords = 0;
    std::uint8_t point_coords = 0;
};

// `viewport_half_width` / `viewport_half_height` are the viewport's half extents (width > 0, height < 0); any other
// viewport (not yet set) takes the fallback EFB size.
[[nodiscard]] LinePointRasterParams make_line_point_raster_params(
    const LinePointValues& values,
    float viewport_half_width,
    float viewport_half_height,
    unsigned efb_scale,
    unsigned fallback_efb_width = 640u,
    unsigned fallback_efb_height = 456u) noexcept;

}  // namespace galaxy::render
